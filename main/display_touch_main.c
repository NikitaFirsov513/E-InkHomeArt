#include <inttypes.h>
#include <stdio.h>

#include "board_config.h"
#include "diagnostic_screen.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gt911.h"
#include "heartbeat.h"
#include "uc8179.h"

#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
#error "Display/touch assembly test uses internal RAM; use its fresh sdkconfig."
#endif
#if !CONFIG_ESP_CONSOLE_UART_DEFAULT
#error "Display/touch test requires the default UART0 console on TX43/RX44."
#endif

static const char *TAG = "DISPLAY_TEST";
static const char *page_names[] = {"STATUS", "TOUCH GRID", "BIT0 / WHITE", "BIT1 / BLACK", "CHECKER"};
static diagnostic_screen_t screen = { .page = DIAG_GRID, .touch_x = -1, .touch_y = -1 };
static QueueHandle_t frame_queue, result_queue;
static gt911_handle_t touch;
static uint16_t native_width, native_height, raw_x, raw_y;
static uint8_t point_count;
static esp_err_t gpio_error, i2c_error = ESP_ERR_NOT_FOUND;
static esp_err_t touch_error = ESP_ERR_NOT_FOUND, uart_error;

typedef struct {
    esp_err_t error;
    uint32_t completed;
    uint32_t page;
} frame_result_t;

static frame_result_t last_result = { .error = ESP_ERR_NOT_FINISHED };

static void report(const char *name, esp_err_t err)
{
    if (err == ESP_OK) ESP_LOGI(TAG,"%s: READY",name);
    else ESP_LOGW(TAG,"%s: %s (0x%x)",name,esp_err_to_name(err),(unsigned)err);
}

static esp_err_t setup_gpio(void)
{
    /* Only this assembly stage: no charger, ADC, LIS INT or POWER input. */
    const board_gpio_output_level_t levels[] = {
        {BOARD_PIN_EPD_CS,1}, {BOARD_PIN_EPD_DC,0},
        {BOARD_PIN_EPD_RESET,0}, {BOARD_PIN_TOUCH_RESET,0},
    };
    uint64_t mask = 0;
    for (unsigned i = 0; i < sizeof(levels)/sizeof(levels[0]); ++i) {
        esp_err_t err = gpio_set_level(levels[i].pin,levels[i].level);
        if (err != ESP_OK) return err;
        mask |= UINT64_C(1) << levels[i].pin;
    }
    const gpio_config_t outputs = { .pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT };
    const gpio_config_t busy = {
        .pin_bit_mask = UINT64_C(1) << BOARD_PIN_EPD_BUSY, .mode = GPIO_MODE_INPUT,
    };
    const gpio_config_t boot = {
        .pin_bit_mask = UINT64_C(1) << BOARD_PIN_BOOT_BUTTON, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&outputs);
    if (err == ESP_OK) err = gpio_config(&busy);
    if (err == ESP_OK) err = gpio_config(&boot);
    return err;
}

static void setup_uart(void)
{
    uart_error = uart_param_config(BOARD_DEBUG_UART_PORT,&board_debug_uart_config);
    if (uart_error == ESP_OK) uart_error = uart_set_pin(BOARD_DEBUG_UART_PORT,
        BOARD_PIN_DEBUG_UART_TX,BOARD_PIN_DEBUG_UART_RX,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if (uart_error == ESP_OK) uart_error = gpio_set_pull_mode(BOARD_PIN_DEBUG_UART_RX,GPIO_PULLUP_ONLY);
    if (uart_error == ESP_OK && !uart_is_driver_installed(BOARD_DEBUG_UART_PORT))
        uart_error = uart_driver_install(BOARD_DEBUG_UART_PORT,256,0,0,NULL,0);
    report("UART0 / TX43 RX44 / 115200",uart_error);
}

static void setup_touch(void)
{
    if (gpio_error != ESP_OK) { i2c_error = touch_error = gpio_error; return; }
    i2c_master_bus_handle_t bus = NULL;
    i2c_error = i2c_new_master_bus(&board_i2c_bus_config,&bus);
    report("I2C0 / SDA10 SCL11 / external pull-ups",i2c_error);
    if (i2c_error != ESP_OK) { touch_error = i2c_error; return; }
    const gt911_config_t config = {
        .bus = bus, .reset_pin = BOARD_PIN_TOUCH_RESET, .interrupt_pin = BOARD_PIN_TOUCH_INT,
        .width = 0, .height = 0, .address = GT911_ADDRESS_LOW,
        .clock_speed_hz = BOARD_I2C_CLOCK_HZ, .timeout_ms = BOARD_I2C_TIMEOUT_MS,
    };
    touch_error = gt911_new(&config,&touch);
    report("GT911 / 0x5D",touch_error);
    if (touch) {
        gt911_get_native_resolution(touch,&native_width,&native_height);
        ESP_LOGI(TAG,"GT911 native=%ux%u; touch-map=0, change with t + Enter",
                 (unsigned)native_width,(unsigned)native_height);
    }
}

static void update_model(void)
{
    snprintf(screen.lines[0],DIAG_LINE_LENGTH,"PANEL 800X480 / UC8179");
    snprintf(screen.lines[1],DIAG_LINE_LENGTH,"SPI2 1MHZ MOSI45 CLK48 CS47");
    snprintf(screen.lines[2],DIAG_LINE_LENGTH,"I2C SDA10 SCL11 / %s",i2c_error == ESP_OK ? "READY" : "ERROR");
    if (touch) snprintf(screen.lines[3],DIAG_LINE_LENGTH,"TOUCH %s %uX%u",
        touch_error == ESP_OK ? "OK" : "ERR",(unsigned)native_width,(unsigned)native_height);
    else snprintf(screen.lines[3],DIAG_LINE_LENGTH,"TOUCH %s",esp_err_to_name(touch_error));
    snprintf(screen.lines[4],DIAG_LINE_LENGTH,"D6 HEARTBEAT / GPIO6");
    snprintf(screen.lines[5],DIAG_LINE_LENGTH,"UART0 115200 / TX43 RX44");
    snprintf(screen.lines[6],DIAG_LINE_LENGTH,"REFRESH MANUAL / MIN %uS",(unsigned)CONFIG_DIAG_MIN_REFRESH_SECONDS);
    snprintf(screen.lines[7],DIAG_LINE_LENGTH,"EPD TX %s",esp_err_to_name(last_result.error));
    unsigned hits = 0;
    for (unsigned i = 0; i < 5; ++i) hits += (screen.target_mask >> i) & 1U;
    if (touch_error != ESP_OK) snprintf(screen.details[0],64,"TOUCH %s",esp_err_to_name(touch_error));
    else snprintf(screen.details[0],64,"X=%d Y=%d PTS=%u HIT=%u/5",
        (int)screen.touch_x,(int)screen.touch_y,(unsigned)point_count,hits);
    snprintf(screen.details[1],64,"RAW=%u,%u MAP=%u FRAMES=%"PRIu32,
        (unsigned)raw_x,(unsigned)raw_y,(unsigned)screen.orientation,screen.touch_samples);
}

static void request_snapshot(void)
{
    update_model();
    if (frame_queue) {
        xQueueOverwrite(frame_queue,&screen);
        ESP_LOGI(TAG,"SNAPSHOT queued: %s; map=%u invert=%u (minimum refresh interval %u s)",
            page_names[screen.page],(unsigned)screen.orientation,(unsigned)screen.inverted,
            (unsigned)CONFIG_DIAG_MIN_REFRESH_SECONDS);
    } else ESP_LOGE(TAG,"Display task unavailable; touch and UART remain active");
}

static void display_task(void *arg)
{
    (void)arg;
    uint8_t *frame = NULL;
    uc8179_handle_t panel = NULL;
    bool spi_ready = false;
    int64_t finished_at = 0;
    uint32_t completed = 0;
    diagnostic_screen_t pending;
    while (xQueueReceive(frame_queue,&pending,portMAX_DELAY) == pdTRUE) {
        const int64_t interval = (int64_t)CONFIG_DIAG_MIN_REFRESH_SECONDS*1000000;
        if (finished_at && esp_timer_get_time() < finished_at + interval) {
            ESP_LOGI(TAG,"EPD waiting approximately %d s; latest snapshot replaces earlier requests",
                (int)((finished_at+interval-esp_timer_get_time())/1000000+1));
            while (esp_timer_get_time() < finished_at + interval)
                xQueueReceive(frame_queue,&pending,pdMS_TO_TICKS(1000));
        }
        while (xQueueReceive(frame_queue,&pending,0) == pdTRUE) {}
        esp_err_t err = gpio_error;
        if (err == ESP_OK && !frame) {
            frame = heap_caps_malloc(DIAG_FRAME_BYTES,MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!frame) err = ESP_ERR_NO_MEM;
        }
        if (err == ESP_OK && !spi_ready) {
            err = spi_bus_initialize(BOARD_EPD_SPI_HOST,&board_epd_spi_bus_config,BOARD_EPD_SPI_DMA);
            spi_ready = err == ESP_OK;
            report("EPD SPI2 bus",err);
        }
        if (err == ESP_OK && !panel) {
            const uc8179_config_t config = {
                .host = BOARD_EPD_SPI_HOST, .cs_pin = BOARD_PIN_EPD_CS,
                .dc_pin = BOARD_PIN_EPD_DC, .reset_pin = BOARD_PIN_EPD_RESET,
                .busy_pin = BOARD_PIN_EPD_BUSY, .clock_speed_hz = BOARD_EPD_SPI_CLOCK_HZ,
                .busy_timeout_ms = 20000,
            };
            err = uc8179_new(&config,&panel);
            report("EPD SPI device",err);
        }
        if (err == ESP_OK && !display_touch_screen_render(frame,DIAG_FRAME_BYTES,&pending))
            err = ESP_ERR_INVALID_ARG;
        if (err == ESP_OK) {
            ESP_LOGI(TAG,"EPD init: RESET14 BUSY13 (level=%d)",gpio_get_level(BOARD_PIN_EPD_BUSY));
            err = uc8179_init(panel);
            report("EPD init",err);
        }
        if (err == ESP_OK) {
            ESP_LOGI(TAG,"EPD frame: 48000 bytes, waiting for full refresh");
            err = uc8179_display_frame(panel,frame,DIAG_FRAME_BYTES);
        }
        if (err == ESP_OK) {
            ++completed;
            ESP_LOGI(TAG,"EPD TX OK: %s. Check the physical image.",page_names[pending.page]);
            err = uc8179_sleep(panel);
            report("EPD deep sleep",err);
        }
        if (err != ESP_OK) {
            report("EPD transfer/sleep",err);
            gpio_set_level(BOARD_PIN_EPD_RESET,0);
            if (panel && uc8179_delete(panel) == ESP_OK) panel = NULL;
        }
        finished_at = esp_timer_get_time();
        const frame_result_t result = { .error = err, .completed = completed, .page = pending.page };
        xQueueOverwrite(result_queue,&result);
    }
}

static void clear_touch(void)
{
    screen.touch_samples = screen.target_mask = 0;
    screen.touch_x = screen.touch_y = -1;
    raw_x = raw_y = 0;
    point_count = 0;
}

static void help(void)
{
    ESP_LOGI(TAG,"UART commands (one character + Enter): r snapshot | n next page | i invert | t touch map | c clear targets | ? help");
}

static void command(char c)
{
    if (c >= 'A' && c <= 'Z') c += 'a'-'A';
    switch (c) {
    case 'r': request_snapshot(); break;
    case 'n': screen.page = (screen.page+1)%DIAG_PAGE_COUNT; request_snapshot(); break;
    case 'i': screen.inverted ^= 1; request_snapshot(); break;
    case 't': screen.orientation = (screen.orientation+1)%8; clear_touch(); request_snapshot(); break;
    case 'c': clear_touch(); request_snapshot(); break;
    case '?': help(); break;
    default: break;
    }
}

static void poll_touch(void)
{
    if (!touch) return;
    esp_lcd_touch_point_data_t points[GT911_MAX_POINTS];
    uint8_t count = 0;
    esp_err_t err = gt911_read(touch,points,&count);
    if (err != touch_error) report("GT911 read",err);
    touch_error = err;
    point_count = count;
    if (err != ESP_OK || !count) return;
    ++screen.touch_samples;
    for (unsigned i = 0; i < count; ++i) {
        int x,y;
        if (!diagnostic_map_touch(points[i].x,points[i].y,native_width,native_height,screen.orientation,&x,&y)) continue;
        screen.target_mask |= diagnostic_target_hit(x,y);
        if (!i) {
            raw_x = points[i].x; raw_y = points[i].y;
            screen.touch_x = x; screen.touch_y = y;
        }
    }
    ESP_LOGI(TAG,"TOUCH raw=%u,%u mapped=%d,%d points=%u targets=0x%02x map=%u",
        (unsigned)raw_x,(unsigned)raw_y,(int)screen.touch_x,(int)screen.touch_y,
        (unsigned)count,(unsigned)screen.target_mask,(unsigned)screen.orientation);
}

typedef struct { bool raw, stable, long_sent; int64_t changed, pressed; } boot_button_t;

static void poll_boot(boot_button_t *button, int64_t now)
{
    if (gpio_error != ESP_OK) return;
    bool pressed = gpio_get_level(BOARD_PIN_BOOT_BUTTON) == BOARD_BUTTON_ACTIVE_LEVEL;
    if (pressed != button->raw) { button->raw = pressed; button->changed = now; }
    if (button->stable != button->raw && now-button->changed >= 35000) {
        button->stable = button->raw;
        if (button->stable) { button->pressed = now; button->long_sent = false; }
        else if (!button->long_sent) command('r');
    }
    if (button->stable && !button->long_sent && now-button->pressed >= 1500000) {
        button->long_sent = true;
        command('i');
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(heartbeat_start());
    gpio_error = setup_gpio();
    report("Display/touch GPIO",gpio_error);
    setup_uart();
    setup_touch();
    frame_queue = xQueueCreate(1,sizeof(screen));
    result_queue = xQueueCreate(1,sizeof(frame_result_t));
    if (!frame_queue || !result_queue || xTaskCreate(display_task,"display_test",6144,NULL,1,NULL) != pdPASS) {
        ESP_LOGE(TAG,"Display task/queue allocation failed");
        if (frame_queue) vQueueDelete(frame_queue);
        if (result_queue) vQueueDelete(result_queue);
        frame_queue = result_queue = NULL;
        last_result.error = ESP_ERR_NO_MEM;
    }
    request_snapshot();
    ESP_LOGI(TAG,"DISPLAY TOUCH TEST READY: UART0 115200, D6 heartbeat, no automatic refresh");
    help();
    boot_button_t boot = {0};
    unsigned line_length = 0;
    char line_command = 0;
    int64_t last_live = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        if (result_queue) xQueueReceive(result_queue,&last_result,0);
        poll_boot(&boot,now);
        char c;
        if (uart_error == ESP_OK) for (unsigned n = 0; n < 32 && uart_read_bytes(BOARD_DEBUG_UART_PORT,&c,1,0) == 1; ++n) {
            if (c == '\r' || c == '\n') {
                if (line_length == 1) command(line_command);
                line_length = 0;
            } else {
                if (!line_length) line_command = c;
                if (line_length < 2) ++line_length;
            }
        }
        poll_touch();
        if (now-last_live >= 5000000) {
            last_live = now;
            ESP_LOGI(TAG,"LIVE uptime=%"PRId64"s touch=%s frames=%"PRIu32" targets=0x%02x EPD=%s completed=%"PRIu32,
                now/1000000,esp_err_to_name(touch_error),screen.touch_samples,(unsigned)screen.target_mask,
                esp_err_to_name(last_result.error),last_result.completed);
        }
        vTaskDelay(pdMS_TO_TICKS(20)+1);
    }
}
