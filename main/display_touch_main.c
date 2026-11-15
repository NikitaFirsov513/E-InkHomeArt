#include <inttypes.h>
#include <stdio.h>
#include <string.h>

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
#include "touch_test.h"
#include "uc8179.h"

#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
#error "Display/touch assembly test uses internal RAM; use its fresh sdkconfig."
#endif
#ifndef CONFIG_DISPLAY_TOUCH_SPI_MHZ
#define CONFIG_DISPLAY_TOUCH_SPI_MHZ 1
#endif
#ifndef CONFIG_DISPLAY_TOUCH_TARGET_MS
#define CONFIG_DISPLAY_TOUCH_TARGET_MS 350
#endif
#ifndef CONFIG_DISPLAY_TOUCH_PARTIALS_BEFORE_CLEAN
#define CONFIG_DISPLAY_TOUCH_PARTIALS_BEFORE_CLEAN 5
#endif
#define EPD_POWER_IDLE_US INT64_C(200000)
#define EPD_POWER_BURST_PARTIALS 5U
#if !CONFIG_ESP_CONSOLE_UART_DEFAULT
#error "Display/touch test requires the default UART0 console on TX43/RX44."
#endif

static const char *TAG = "DISPLAY_TEST";
static const char *page_names[] = {"STATUS", "TOUCH GRID", "BIT0 / WHITE", "BIT1 / BLACK", "CHECKER"};
static touch_test_t test = { .screen = { .page = DIAG_GRID, .touch_x = -1, .touch_y = -1 } };
static bool auto_snapshot = true, screen_dirty;
static int64_t last_touch_at, last_touch_log, last_snapshot_at;
static char pressed_action;
static uint32_t pressed_page;
#if defined(CONFIG_DISPLAY_TOUCH_PARTIAL_TEST) && CONFIG_DISPLAY_TOUCH_PARTIAL_TEST
static const bool partial_bench = true;
#else
static const bool partial_bench = false;
#endif
static QueueHandle_t frame_queue, result_queue;
static gt911_handle_t touch;
static uint16_t native_width, native_height, raw_x, raw_y;
static esp_err_t gpio_error, i2c_error = ESP_ERR_NOT_FOUND;
static esp_err_t touch_error = ESP_ERR_NOT_FOUND, uart_error;

typedef struct {
    esp_err_t error;
    uint32_t completed;
    uint32_t page;
    uint32_t full_updates, partial_updates;
} frame_result_t;

static frame_result_t last_result = { .error = ESP_ERR_NOT_FINISHED, .page = UINT32_MAX };

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
    snprintf(test.screen.lines[0],DIAG_LINE_LENGTH,"PANEL 800X480 / UC8179");
    snprintf(test.screen.lines[1],DIAG_LINE_LENGTH,"SPI2 %uMHZ MOSI45 CLK48 CS47",(unsigned)CONFIG_DISPLAY_TOUCH_SPI_MHZ);
    snprintf(test.screen.lines[2],DIAG_LINE_LENGTH,"I2C SDA10 SCL11 / %s",i2c_error == ESP_OK ? "READY" : "ERROR");
    if (touch) snprintf(test.screen.lines[3],DIAG_LINE_LENGTH,"TOUCH %s %uX%u",
        touch_error == ESP_OK ? "OK" : "ERR",(unsigned)native_width,(unsigned)native_height);
    else snprintf(test.screen.lines[3],DIAG_LINE_LENGTH,"TOUCH %s",esp_err_to_name(touch_error));
    snprintf(test.screen.lines[4],DIAG_LINE_LENGTH,"D6 HEARTBEAT / GPIO6");
    snprintf(test.screen.lines[5],DIAG_LINE_LENGTH,"UART0 115200 / TX43 RX44");
    if (partial_bench) snprintf(test.screen.lines[6],DIAG_LINE_LENGTH,"PARTIAL / IDLE CLEAN AFTER %u",
        (unsigned)CONFIG_DISPLAY_TOUCH_PARTIALS_BEFORE_CLEAN);
    else snprintf(test.screen.lines[6],DIAG_LINE_LENGTH,"FULL ONLY / MIN %uS",(unsigned)CONFIG_DIAG_MIN_REFRESH_SECONDS);
    snprintf(test.screen.lines[7],DIAG_LINE_LENGTH,"EPD TX %s",esp_err_to_name(last_result.error));
    /* Avoid dragging the dirty rectangle down to the changing numeric labels
     * on every motion report. UART and STATUS keep live coordinates. */
    if (test.screen.page == DIAG_GRID && test.contacts && touch_error == ESP_OK) return;
    unsigned hits = 0, cells = 0;
    for (unsigned i = 0; i < 5; ++i) hits += (test.screen.target_mask >> i) & 1U;
    for (unsigned i = 0; i < TOUCH_TEST_CELLS; ++i) cells += (test.coverage >> i) & 1U;
    if (touch_error != ESP_OK) snprintf(test.screen.details[0],64,"TOUCH %s",esp_err_to_name(touch_error));
    else snprintf(test.screen.details[0],64,"X=%d Y=%d NOW=%u MAX=%u",
        (int)test.screen.touch_x,(int)test.screen.touch_y,(unsigned)test.contacts,(unsigned)test.max_contacts);
    snprintf(test.screen.details[1],64,"HIT=%u/5 CELL=%u/24 STROKES=%"PRIu32,
        hits,cells,test.strokes);
}

static void request_snapshot(void)
{
    update_model();
    test.last_activity_us = last_touch_at;
    if (frame_queue) {
        xQueueOverwrite(frame_queue,&test);
        screen_dirty = false;
        last_snapshot_at = esp_timer_get_time();
        ESP_LOGI(TAG,"SNAPSHOT queued: %s; map=%u invert=%u (%s, target %u ms)",
            page_names[test.screen.page],(unsigned)test.screen.orientation,(unsigned)test.screen.inverted,
            partial_bench ? "OTP partial window" : "full only",partial_bench ? (unsigned)CONFIG_DISPLAY_TOUCH_TARGET_MS : (unsigned)CONFIG_DIAG_MIN_REFRESH_SECONDS*1000U);
    } else ESP_LOGE(TAG,"Display task unavailable; touch and UART remain active");
}

static void display_task(void *arg)
{
    (void)arg;
    uint8_t *frame = NULL, *shown_frame = NULL;
    uc8179_handle_t panel = NULL;
    bool spi_ready = false;
    int64_t finished_at = 0, started_at = 0;
    uint32_t completed = 0, shown_page = UINT32_MAX, full_updates = 0, partial_updates = 0;
    unsigned partial_since_full = 0, power_burst_partials = 0;
    uint32_t completed_generation = 0;
    bool baseline = false, have_visible = false, power_active = false, cleanup_wait_logged = false;
    touch_test_t pending = {0};
    for (;;) {
        bool cleanup_due = partial_bench && baseline &&
            partial_since_full >= CONFIG_DISPLAY_TOUCH_PARTIALS_BEFORE_CLEAN;
        bool received = xQueueReceive(frame_queue,&pending,
            power_active || cleanup_due ? pdMS_TO_TICKS(50)+1 : portMAX_DELAY) == pdTRUE;
        esp_err_t err = gpio_error;
        if (power_active && esp_timer_get_time()-finished_at >= EPD_POWER_IDLE_US) {
            err = uc8179_power_off(panel);
            power_active = false;
            power_burst_partials = 0;
            report("EPD idle power off / RAM retained",err);
        }
        if (!received && !cleanup_due && err == ESP_OK) continue;
        const int64_t deadline = partial_bench ? started_at+(int64_t)CONFIG_DISPLAY_TOUCH_TARGET_MS*1000 :
                                 finished_at+(int64_t)CONFIG_DIAG_MIN_REFRESH_SECONDS*1000000;
        if (finished_at) while (esp_timer_get_time() < deadline) {
            int64_t left = deadline-esp_timer_get_time();
            if (left <= 0) break;
            TickType_t ticks = pdMS_TO_TICKS((left+999)/1000);
            xQueueReceive(frame_queue,&pending,ticks ? ticks : 1);
        }
        while (xQueueReceive(frame_queue,&pending,0) == pdTRUE) {}
        touch_refresh_t mode = partial_bench ? touch_test_refresh_policy(&pending,baseline,
            partial_since_full,CONFIG_DISPLAY_TOUCH_PARTIALS_BEFORE_CLEAN,
            completed_generation,esp_timer_get_time()) : TOUCH_REFRESH_FULL;
        if (err == ESP_OK && mode == TOUCH_REFRESH_WAIT) {
            if (!cleanup_wait_logged) ESP_LOGI(TAG,"EPD CLEAN PENDING: %u partials; waiting for release + 1200 ms idle",
                partial_since_full);
            cleanup_wait_logged = true;
            if (power_active) {
                err = uc8179_power_off(panel);
                power_active = false;
                power_burst_partials = 0;
                report("EPD cleanup wait power off",err);
            }
            if (err == ESP_OK) continue;
        }
        if (err == ESP_OK && !frame) {
            frame = heap_caps_malloc(DIAG_FRAME_BYTES,MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!frame) err = ESP_ERR_NO_MEM;
        }
        if (err == ESP_OK && partial_bench && !shown_frame) {
            shown_frame = heap_caps_malloc(DIAG_FRAME_BYTES,MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!shown_frame) err = ESP_ERR_NO_MEM;
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
                .busy_pin = BOARD_PIN_EPD_BUSY, .clock_speed_hz = CONFIG_DISPLAY_TOUCH_SPI_MHZ*1000000,
                .busy_timeout_ms = 20000,
            };
            err = uc8179_new(&config,&panel);
            report("EPD SPI device",err);
        }
        int64_t render_start = esp_timer_get_time();
        if (err == ESP_OK && !touch_test_screen_render(frame,DIAG_FRAME_BYTES,&pending))
            err = ESP_ERR_INVALID_ARG;
        int64_t render_us = esp_timer_get_time()-render_start;
        touch_test_region_t region = {0,0,DIAG_WIDTH,DIAG_HEIGHT};
        if (err == ESP_OK && mode == TOUCH_REFRESH_PARTIAL && have_visible &&
            !touch_test_changed_region(shown_frame,frame,DIAG_FRAME_BYTES,&region)) {
            if (power_active && !pending.contacts) {
                err = uc8179_power_off(panel);
                power_active = false;
                power_burst_partials = 0;
                report("EPD release power off",err);
            }
            if (err == ESP_OK) {
                shown_page = pending.screen.page;
                const frame_result_t unchanged = { .error = ESP_OK, .completed = completed, .page = shown_page,
                    .full_updates = full_updates, .partial_updates = partial_updates };
                xQueueOverwrite(result_queue,&unchanged);
                ESP_LOGI(TAG,"EPD unchanged: no refresh needed");
                continue;
            }
        }
        bool use_partial = mode == TOUCH_REFRESH_PARTIAL;
        started_at = esp_timer_get_time();
        if (err == ESP_OK && !use_partial) {
            cleanup_wait_logged = false;
            ESP_LOGI(TAG,"EPD FULL reason=%s partials=%u limit=%u",
                !baseline ? "baseline" : pending.full_generation != completed_generation ? "manual" : "idle-clean",
                partial_since_full,(unsigned)CONFIG_DISPLAY_TOUCH_PARTIALS_BEFORE_CLEAN);
            ESP_LOGI(TAG,"EPD init: RESET14 BUSY13 (level=%d)",gpio_get_level(BOARD_PIN_EPD_BUSY));
            err = uc8179_init(panel);
            report("EPD init",err);
        }
        if (err == ESP_OK) {
            ESP_LOGI(TAG,"EPD %s: %ux%u at %u,%u / SPI %u MHz",use_partial ? "PARTIAL" : "FULL",
                use_partial ? region.width : DIAG_WIDTH,use_partial ? region.height : DIAG_HEIGHT,
                use_partial ? region.x : 0,use_partial ? region.y : 0,(unsigned)CONFIG_DISPLAY_TOUCH_SPI_MHZ);
            err = use_partial ? uc8179_display_partial_region(panel,frame,DIAG_FRAME_BYTES,region.x,region.y,region.width,region.height) :
                                uc8179_display_frame(panel,frame,DIAG_FRAME_BYTES);
        }
        if (err == ESP_OK && partial_bench && !use_partial) {
            err = uc8179_prepare_partial(panel,frame,DIAG_FRAME_BYTES);
            baseline = err == ESP_OK;
            partial_since_full = 0;
        }
        if (err == ESP_OK) {
            power_active = true;
            ++completed;
            if (use_partial) { ++partial_updates; ++partial_since_full; ++power_burst_partials; }
            else { ++full_updates; completed_generation = pending.full_generation; power_burst_partials = 0; }
            shown_page = pending.screen.page;
            if (shown_frame) memcpy(shown_frame,frame,DIAG_FRAME_BYTES);
            have_visible = true;
            ESP_LOGI(TAG,"EPD TX OK: %s. Check the physical image.",page_names[pending.screen.page]);
            bool hold_power = partial_bench && use_partial && pending.contacts &&
                power_burst_partials < EPD_POWER_BURST_PARTIALS &&
                partial_since_full < CONFIG_DISPLAY_TOUCH_PARTIALS_BEFORE_CLEAN;
            if (!hold_power) {
                err = partial_bench ? uc8179_power_off(panel) : uc8179_sleep(panel);
                power_active = false;
                power_burst_partials = 0;
                report(partial_bench ? "EPD power off / RAM retained" : "EPD deep sleep",err);
            }
            uc8179_update_stats_t timing;
            if (uc8179_get_update_stats(panel,&timing) == ESP_OK)
                ESP_LOGI(TAG,"EPD PERF %s bytes=%"PRIu32" render=%"PRId64"ms power=%"PRId64
                    "ms spi=%"PRId64"ms busy=%"PRId64"ms off=%"PRId64"ms total=%"PRId64"ms",
                    use_partial ? "PARTIAL" : "FULL",timing.bytes,render_us/1000,timing.power_on_us/1000,
                    timing.transfer_us/1000,timing.refresh_us/1000,timing.power_off_us/1000,
                    (esp_timer_get_time()-started_at)/1000);
        }
        if (err != ESP_OK) {
            report("EPD transfer/sleep",err);
            baseline = have_visible = false;
            power_active = false;
            power_burst_partials = 0;
            shown_page = UINT32_MAX;
            if (panel) report("EPD error power off",uc8179_power_off(panel));
            gpio_set_level(BOARD_PIN_EPD_RESET,0);
            if (panel && uc8179_delete(panel) == ESP_OK) panel = NULL;
        }
        finished_at = esp_timer_get_time();
        const frame_result_t result = { .error = err, .completed = completed, .page = shown_page,
            .full_updates = full_updates, .partial_updates = partial_updates };
        xQueueOverwrite(result_queue,&result);
    }
}

static void clear_touch(void)
{
    touch_test_clear(&test);
    raw_x = raw_y = 0;
    pressed_action = 0;
    ESP_LOGI(TAG,"TEST_RESET map=%u",(unsigned)test.screen.orientation);
}

static void help(void)
{
    ESP_LOGI(TAG,"UART + Enter: r snapshot | f full clean | g grid | n patterns | i invert | t map | c clear | a auto | s stats | ? help");
}

static void stats(void)
{
    ESP_LOGI(TAG,"RESULT touch=%s frames=%"PRIu32" strokes=%"PRIu32" max=%"PRIu32
             " targets=0x%02x cells=0x%06"PRIx32" errors=%"PRIu32" map=%u EPD=%s completed=%"PRIu32
             " full=%"PRIu32" partial=%"PRIu32" shown=%d requested=%u mode=%s",
             esp_err_to_name(touch_error),test.screen.touch_samples,test.strokes,test.max_contacts,
             (unsigned)test.screen.target_mask,test.coverage,test.errors,(unsigned)test.screen.orientation,
             esp_err_to_name(last_result.error),last_result.completed,last_result.full_updates,last_result.partial_updates,
             last_result.page < DIAG_PAGE_COUNT ? (int)last_result.page : -1,(unsigned)test.screen.page,
             partial_bench ? "partial-bench" : "full-only");
}

static void command(char c)
{
    if (c >= 'A' && c <= 'Z') c += 'a'-'A';
    switch (c) {
    case 'r': request_snapshot(); break;
    case 'f': ++test.full_generation; request_snapshot(); break;
    case 'g': test.screen.page = DIAG_GRID; request_snapshot(); break;
    case 'n': test.screen.page = (test.screen.page+1)%DIAG_PAGE_COUNT; request_snapshot(); break;
    case 'i': test.screen.inverted ^= 1; request_snapshot(); break;
    case 't': test.screen.orientation = (test.screen.orientation+1)%8; clear_touch(); request_snapshot(); break;
    case 'c': clear_touch(); request_snapshot(); break;
    case 'a': auto_snapshot = !auto_snapshot; ESP_LOGI(TAG,"AUTO_SNAPSHOT %s",auto_snapshot ? "ON" : "OFF"); break;
    case 's': stats(); break;
    case '?': help(); break;
    default: break;
    }
}

static void poll_touch(void)
{
    if (!touch) return;
    esp_lcd_touch_point_data_t points[GT911_MAX_POINTS];
    uint8_t count = 0;
    bool fresh = false;
    esp_err_t err = gt911_read_frame(touch,points,&count,&fresh);
    if (err != touch_error) report("GT911 read",err);
    touch_error = err;
    if (err != ESP_OK) {
        ++test.errors;
        test.trace_active = false;
        pressed_action = 0;
        return;
    }
    if (!fresh) return; /* A quiet poll is not a pen-up event. */
    touch_test_point_t raw[TOUCH_TEST_POINTS];
    for (unsigned i = 0; i < count; ++i)
        raw[i] = (touch_test_point_t){points[i].x,points[i].y,points[i].strength,points[i].track_id};
    unsigned previous = test.contacts;
    uint32_t previous_targets = test.screen.target_mask, previous_cells = test.coverage;
    if (!touch_test_accept(&test,raw,count,native_width,native_height)) {
        touch_error = ESP_ERR_INVALID_RESPONSE;
        ++test.errors;
        pressed_action = 0;
        test.trace_active = false;
        return;
    }
    if (!count && !previous) return;
    int64_t now = esp_timer_get_time();
    last_touch_at = now;
    screen_dirty = true;
    if (count) { raw_x = points[0].x; raw_y = points[0].y; }
    bool controls = touch_test_controls_ready(last_result.page,test.screen.page);
    char action = count == 1 && controls ? touch_test_button(test.points[0].x,test.points[0].y) : 0;
    if (!previous && count) { pressed_action = action; pressed_page = last_result.page; }
    if (count && (count != 1 || action != pressed_action)) pressed_action = 0;
    if (count != previous || !count || now-last_touch_log >= 100000 ||
        test.screen.target_mask != previous_targets || test.coverage != previous_cells) {
        char encoded[160] = {0};
        size_t used = 0;
        for (unsigned i = 0; i < count; ++i) {
            const touch_test_point_t *p = &test.points[i];
            int n = snprintf(encoded+used,sizeof(encoded)-used,"%s%u:%u:%u:%u",
                i ? "," : "",p->id,p->x,p->y,p->size);
            if (n < 0 || (size_t)n >= sizeof(encoded)-used) break;
            used += (size_t)n;
        }
        ESP_LOGI(TAG,"TOUCH2 count=%u map=%u targets=%"PRIu32" cells=%"PRIu32
                 " strokes=%"PRIu32" max=%"PRIu32" points=%s raw=%u,%u",
                 (unsigned)count,(unsigned)test.screen.orientation,test.screen.target_mask,test.coverage,
                 test.strokes,test.max_contacts,encoded,(unsigned)raw_x,(unsigned)raw_y);
        last_touch_log = now;
    }
    if (previous_targets != 31 && test.screen.target_mask == 31) ESP_LOGI(TAG,"TARGETS PASS: 5/5");
    if (previous_cells != TOUCH_TEST_ALL_CELLS && test.coverage == TOUCH_TEST_ALL_CELLS)
        ESP_LOGI(TAG,"COVERAGE PASS: 24/24 test-area cells");
    if (!count && previous) {
        char released = pressed_action;
        pressed_action = 0;
        if (pressed_page != last_result.page || test.screen.page != last_result.page) released = 0;
        if (released == 'n') { test.screen.page = touch_test_next_ui_page(last_result.page); request_snapshot(); }
        else if (released) command(released);
    }
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
    frame_queue = xQueueCreate(1,sizeof(test));
    result_queue = xQueueCreate(1,sizeof(frame_result_t));
    if (!frame_queue || !result_queue || xTaskCreate(display_task,"display_test",6144,NULL,1,NULL) != pdPASS) {
        ESP_LOGE(TAG,"Display task/queue allocation failed");
        if (frame_queue) vQueueDelete(frame_queue);
        if (result_queue) vQueueDelete(result_queue);
        frame_queue = result_queue = NULL;
        last_result.error = ESP_ERR_NO_MEM;
    }
    request_snapshot();
    ESP_LOGI(TAG,"DISPLAY TOUCH TEST READY: V5 / UART0 115200 / D6 / SPI %u MHz / %s / clean limit %u",
             (unsigned)CONFIG_DISPLAY_TOUCH_SPI_MHZ,
             partial_bench ? "OTP partial / idle cleanup" : "full only / 180s minimum",
             (unsigned)CONFIG_DISPLAY_TOUCH_PARTIALS_BEFORE_CLEAN);
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
        if (auto_snapshot && screen_dirty &&
            (now-last_touch_at >= 40000 || now-last_snapshot_at >= 100000)) request_snapshot();
        if (now-last_live >= 5000000) {
            last_live = now;
            stats();
            ESP_LOGI(TAG,"LIVE uptime=%"PRId64"s touch=%s frames=%"PRIu32" targets=0x%02x EPD=%s completed=%"PRIu32,
                now/1000000,esp_err_to_name(touch_error),test.screen.touch_samples,(unsigned)test.screen.target_mask,
                esp_err_to_name(last_result.error),last_result.completed);
        }
        vTaskDelay(pdMS_TO_TICKS(20)+1);
    }
}
