#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "diagnostic_screen.h"
#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gt911.h"
#include "lis2dw12_idf.h"
#include "uc8179.h"

static const char *TAG = "DIAG";
static QueueHandle_t screen_queue;
static diagnostic_screen_t screen;
static i2c_master_bus_handle_t i2c_bus;
static gt911_handle_t touch;
static lis2dw12_handle_t accel;
static adc_oneshot_unit_handle_t adc_unit;
static adc_cali_handle_t calibration[BOARD_ADC_INPUT_COUNT];
static bool adc_channel_ready[BOARD_ADC_INPUT_COUNT];
static int adc_raw[BOARD_ADC_INPUT_COUNT] = {-1,-1,-1};
static int adc_mv[BOARD_ADC_INPUT_COUNT] = {-1,-1,-1};
static esp_err_t gpio_error, i2c_error, touch_error = ESP_ERR_NOT_FOUND;
static esp_err_t accel_error = ESP_ERR_NOT_FOUND, adc_error, uart_error, flash_error;
static uint32_t flash_bytes;
static bool psram_pass, have_accel_sample;
static bool usb_input_ready;
static size_t psram_bytes;
static uint16_t touch_width, touch_height;
static unsigned touch_points;
static uint16_t raw_touch_x, raw_touch_y;
static lis2dw12_sample_t acceleration;
static uint32_t lis_int_edges;
static uint32_t uart_rx_count;
static const char *page_names[] = {"STATUS", "TOUCH GRID", "BIT0 / WHITE", "BIT1 / BLACK", "CHECKER"};

static void report(const char *name, esp_err_t err)
{
    if (err == ESP_OK) ESP_LOGI(TAG,"%s: READY",name);
    else ESP_LOGW(TAG,"%s: %s (0x%x)",name,esp_err_to_name(err),(unsigned)err);
}

static esp_err_t setup_gpio(void)
{
    for (unsigned i = 0; i < BOARD_GPIO_OUTPUT_COUNT; ++i) {
        esp_err_t err = gpio_set_level(board_gpio_output_levels[i].pin,board_gpio_output_levels[i].level);
        if (err != ESP_OK) return err;
    }
    esp_err_t err = gpio_config(&board_gpio_output_config);
    if (err == ESP_OK) err = gpio_config(&board_gpio_input_config);
    if (err == ESP_OK) err = gpio_config(&board_gpio_analog_config);
    /* Q2 stays off throughout the test: do not force the charger's USB_DET low. */
    return err;
}

static void check_memory(void)
{
    flash_error = esp_flash_get_size(NULL,&flash_bytes);
    if (flash_error == ESP_OK && flash_bytes != 16U*1024U*1024U) flash_error = ESP_ERR_INVALID_SIZE;
    psram_bytes = esp_psram_is_initialized() ? esp_psram_get_size() : 0;
    volatile uint32_t *block = heap_caps_malloc(32768,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (block) {
        psram_pass = psram_bytes == 8U*1024U*1024U;
        for (unsigned i = 0; i < 8192; ++i) block[i] = UINT32_C(0xA55A39C6) ^ (i*UINT32_C(0x10201));
        for (unsigned i = 0; i < 8192; ++i)
            if (block[i] != (UINT32_C(0xA55A39C6) ^ (i*UINT32_C(0x10201)))) psram_pass = false;
        free((void *)block);
    }
    report("FLASH 16MB",flash_error);
    report("PSRAM 8MB / 32KB pattern",psram_pass ? ESP_OK : ESP_FAIL);
}

static void setup_uart(void)
{
    uart_error = uart_param_config(BOARD_DEBUG_UART_PORT,&board_debug_uart_config);
    if (uart_error == ESP_OK) uart_error = uart_set_pin(BOARD_DEBUG_UART_PORT,
        BOARD_PIN_DEBUG_UART_TX,BOARD_PIN_DEBUG_UART_RX,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if (uart_error == ESP_OK) uart_error = gpio_set_pull_mode(BOARD_PIN_DEBUG_UART_RX,GPIO_PULLUP_ONLY);
    if (uart_error == ESP_OK) uart_error = uart_driver_install(BOARD_DEBUG_UART_PORT,256,0,0,NULL,0);
    report("UART0 / J2 TX43 RX44",uart_error);
}

static void setup_adc(void)
{
    adc_error = adc_oneshot_new_unit(&board_adc_unit_config,&adc_unit);
    if (adc_error != ESP_OK) { report("ADC1",adc_error); return; }
    for (unsigned i = 0; i < BOARD_ADC_INPUT_COUNT; ++i) {
        esp_err_t err = adc_oneshot_config_channel(adc_unit,board_adc_inputs[i].channel,&board_adc_channel_config);
        adc_channel_ready[i] = err == ESP_OK;
        if (err != ESP_OK) { adc_error = err; continue; }
        const adc_cali_curve_fitting_config_t cal = {
            .unit_id = BOARD_ADC_UNIT,
            .chan = board_adc_inputs[i].channel,
            .atten = board_adc_channel_config.atten,
            .bitwidth = board_adc_channel_config.bitwidth,
        };
        err = adc_cali_create_scheme_curve_fitting(&cal,&calibration[i]);
        if (err != ESP_OK) ESP_LOGW(TAG,"ADC channel %u: calibration unavailable (%s), raw only",i,esp_err_to_name(err));
    }
    report("ADC1",adc_error);
}

static void sample_adc(void)
{
    esp_err_t sampling_error = ESP_OK;
    for (unsigned i = 0; i < BOARD_ADC_INPUT_COUNT; ++i) {
        adc_raw[i] = adc_mv[i] = -1;
        if (!adc_unit || !adc_channel_ready[i]) { sampling_error = ESP_ERR_INVALID_STATE; continue; }
        int value = 0, total = 0;
        esp_err_t err = adc_oneshot_read(adc_unit,board_adc_inputs[i].channel,&value);
        esp_rom_delay_us(100); /* Discard the first conversion after switching. */
        for (unsigned n = 0; n < 16 && err == ESP_OK; ++n) {
            err = adc_oneshot_read(adc_unit,board_adc_inputs[i].channel,&value);
            if (err == ESP_OK) total += value;
        }
        if (err != ESP_OK) { sampling_error = err; continue; }
        adc_raw[i] = total/16;
        if (calibration[i] && adc_cali_raw_to_voltage(calibration[i],adc_raw[i],&value) == ESP_OK)
            adc_mv[i] = value * (int)board_adc_inputs[i].voltage_scale_numerator /
                               (int)board_adc_inputs[i].voltage_scale_denominator;
    }
    if (adc_unit) adc_error = sampling_error;
}

static void setup_i2c_devices(void)
{
    i2c_error = i2c_new_master_bus(&board_i2c_bus_config,&i2c_bus);
    report("I2C0 / SDA10 SCL11",i2c_error);
    if (i2c_error != ESP_OK) return;
    const gt911_config_t touch_config = {
        .bus = i2c_bus,
        .reset_pin = BOARD_PIN_TOUCH_RESET,
        .interrupt_pin = BOARD_PIN_TOUCH_INT,
        .width = 0, .height = 0, /* Read the sensor's actual stored geometry. */
        .address = GT911_ADDRESS_LOW,
        .clock_speed_hz = BOARD_I2C_CLOCK_HZ,
        .timeout_ms = BOARD_I2C_TIMEOUT_MS,
    };
    touch_error = gt911_new(&touch_config,&touch);
    report("GT911 / 0x5D",touch_error);
    if (touch_error == ESP_OK) {
        gt911_get_native_resolution(touch,&touch_width,&touch_height);
        ESP_LOGI(TAG,"GT911 native %ux%u, automatic portrait-to-landscape mapping",touch_width,touch_height);
    }
    const lis2dw12_idf_config_t accel_config = {
        .bus = i2c_bus, .address = BOARD_LIS2DW12_I2C_ADDRESS,
        .clock_speed_hz = BOARD_I2C_CLOCK_HZ, .timeout_ms = BOARD_I2C_TIMEOUT_MS,
    };
    accel_error = lis2dw12_idf_new(&accel_config,&accel);
    if (accel_error == ESP_OK) accel_error = lis2dw12_idf_start(accel,LIS2DW12_2g,LIS2DW12_XL_ODR_25Hz);
    if (accel_error == ESP_OK) {
        const stmdev_ctx_t *st = lis2dw12_idf_context(accel);
        lis2dw12_ctrl4_int1_pad_ctrl_t route = { .int1_drdy = 1 };
        accel_error = lis2dw12_data_ready_mode_set(st,LIS2DW12_DRDY_LATCHED);
        if (accel_error == ESP_OK) accel_error = lis2dw12_pin_polarity_set(st,LIS2DW12_ACTIVE_HIGH);
        if (accel_error == ESP_OK) accel_error = lis2dw12_pin_mode_set(st,LIS2DW12_PUSH_PULL);
        if (accel_error == ESP_OK) accel_error = lis2dw12_pin_int1_route_set(st,&route);
    }
    report("LIS2DW12 / 0x19 / INT1",accel_error);
}

static void voltage_text(char *out, size_t length, int mv)
{
    if (mv < 0) snprintf(out,length,"--");
    else snprintf(out,length,"%d.%03d",mv/1000,mv%1000);
}

static void update_screen_model(void)
{
    snprintf(screen.lines[0],DIAG_LINE_LENGTH,"FLASH %s %u MB",flash_error == ESP_OK ? "OK" : "FAIL",(unsigned)(flash_bytes/1048576));
    snprintf(screen.lines[1],DIAG_LINE_LENGTH,"PSRAM %s %u MB",psram_pass ? "OK" : "FAIL",(unsigned)(psram_bytes/1048576));
    snprintf(screen.lines[2],DIAG_LINE_LENGTH,"I2C0  %s",i2c_error == ESP_OK ? "READY / 100 KHZ" : esp_err_to_name(i2c_error));
    if (touch) snprintf(screen.lines[3],DIAG_LINE_LENGTH,"TOUCH %s %uX%u",touch_error == ESP_OK ? "OK" : "ERR",touch_width,touch_height);
    else snprintf(screen.lines[3],DIAG_LINE_LENGTH,"TOUCH %s",esp_err_to_name(touch_error));
    snprintf(screen.lines[4],DIAG_LINE_LENGTH,"ACCEL %s",accel_error == ESP_OK ? "OK / 2G / 25 HZ" : esp_err_to_name(accel_error));
    unsigned calibrated = 0;
    for (unsigned i = 0; i < BOARD_ADC_INPUT_COUNT; ++i) calibrated += calibration[i] != NULL;
    snprintf(screen.lines[5],DIAG_LINE_LENGTH,"ADC1  %s / CAL %u/3",adc_error == ESP_OK ? "READY" : "ERROR",calibrated);
    snprintf(screen.lines[6],DIAG_LINE_LENGTH,"USB   %s",usb_serial_jtag_is_connected() ? "HOST CONNECTED" : "NO HOST");
    snprintf(screen.lines[7],DIAG_LINE_LENGTH,"UART0 %s",uart_error == ESP_OK ? "READY / 115200" : esp_err_to_name(uart_error));
    if (screen.page == DIAG_GRID) {
        unsigned hits = 0;
        for (unsigned i = 0; i < 5; ++i) hits += (screen.target_mask >> i) & 1U;
        snprintf(screen.details[0],64,"X=%d Y=%d PTS=%u HIT=%u/5",(int)screen.touch_x,(int)screen.touch_y,touch_points,hits);
        snprintf(screen.details[1],64,"RAW=%u,%u MAP=%u FRAMES=%"PRIu32,raw_touch_x,raw_touch_y,(unsigned)screen.orientation,screen.touch_samples);
    } else {
        char battery[12], cc1[12], cc2[12];
        voltage_text(battery,sizeof(battery),adc_mv[BOARD_ADC_BATTERY]);
        voltage_text(cc1,sizeof(cc1),adc_mv[BOARD_ADC_USB_CC1]);
        voltage_text(cc2,sizeof(cc2),adc_mv[BOARD_ADC_USB_CC2]);
        snprintf(screen.details[0],64,"BAT=%sV  CC1=%sV  CC2=%sV",battery,cc1,cc2);
        if (have_accel_sample) snprintf(screen.details[1],64,"AX=%d AY=%d AZ=%d MG  TOUCH=%"PRIu32,
            (int)acceleration.mg[0],(int)acceleration.mg[1],(int)acceleration.mg[2],screen.touch_samples);
        else snprintf(screen.details[1],64,"ACCEL: NO SAMPLE  TOUCH FRAMES=%"PRIu32,screen.touch_samples);
    }
}

static void request_snapshot(void)
{
    update_screen_model();
    if (screen_queue) xQueueOverwrite(screen_queue,&screen);
    ESP_LOGI(TAG,"Snapshot requested: %s, inverted=%u, map=%u",page_names[screen.page],
             (unsigned)screen.inverted,(unsigned)screen.orientation);
}

static void display_task(void *arg)
{
    (void)arg;
    uint8_t *frame = heap_caps_malloc(DIAG_FRAME_BYTES,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frame) frame = heap_caps_malloc(DIAG_FRAME_BYTES,MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uc8179_handle_t panel = NULL;
    bool spi_ready = false;
    int64_t last_frame = 0;
    diagnostic_screen_t pending;
    while (xQueueReceive(screen_queue,&pending,portMAX_DELAY) == pdTRUE) {
        if (!frame || gpio_error != ESP_OK) { ESP_LOGE(TAG,"E-Ink unavailable: framebuffer/GPIO error"); continue; }
        const int64_t interval = (int64_t)CONFIG_DIAG_MIN_REFRESH_SECONDS*1000000;
        while (last_frame && esp_timer_get_time() < last_frame + interval) {
            ESP_LOGI(TAG,"Refresh queued; wait %d s",(int)((last_frame+interval-esp_timer_get_time())/1000000+1));
            xQueueReceive(screen_queue,&pending,pdMS_TO_TICKS(1000));
        }
        while (xQueueReceive(screen_queue,&pending,0) == pdTRUE) {}
        esp_err_t err = ESP_OK;
        if (!spi_ready) {
            err = spi_bus_initialize(BOARD_EPD_SPI_HOST,&board_epd_spi_bus_config,BOARD_EPD_SPI_DMA);
            spi_ready = err == ESP_OK;
        }
        if (err == ESP_OK && !panel) {
            const uc8179_config_t config = {
                .host = BOARD_EPD_SPI_HOST, .cs_pin = BOARD_PIN_EPD_CS,
                .dc_pin = BOARD_PIN_EPD_DC, .reset_pin = BOARD_PIN_EPD_RESET,
                .busy_pin = BOARD_PIN_EPD_BUSY, .clock_speed_hz = BOARD_EPD_SPI_CLOCK_HZ,
                .busy_timeout_ms = 20000,
            };
            err = uc8179_new(&config,&panel);
        }
        if (err == ESP_OK && !diagnostic_screen_render(frame,DIAG_FRAME_BYTES,&pending)) err = ESP_ERR_INVALID_ARG;
        if (err == ESP_OK) err = uc8179_init(panel);
        if (err == ESP_OK) err = uc8179_display_frame(panel,frame,DIAG_FRAME_BYTES);
        if (err == ESP_OK) {
            ESP_LOGI(TAG,"E-Ink transfer complete: %s. Verify the physical image.",page_names[pending.page]);
            err = uc8179_sleep(panel);
            report("E-Ink sleep",err);
        }
        if (err != ESP_OK) {
            report("E-Ink",err);
            gpio_set_level(BOARD_PIN_EPD_RESET,0);
            if (panel && uc8179_delete(panel) == ESP_OK) panel = NULL;
        }
        last_frame = esp_timer_get_time();
    }
}

static void help(void)
{
    const char *message = "Commands: r snapshot | n next page | i invert | t touch map 0..7 | c clear targets | ? help\r\n";
    ESP_LOGI(TAG,"%s",message);
    if (uart_error == ESP_OK) uart_write_bytes(BOARD_DEBUG_UART_PORT,message,strlen(message));
}

static void clear_touch(void)
{
    screen.touch_samples = screen.target_mask = 0;
    screen.touch_x = screen.touch_y = -1;
}

static void command(char c)
{
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
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

typedef struct { bool raw, stable, long_sent; int64_t changed, pressed; } button_t;

static void poll_button(button_t *b, gpio_num_t pin, int64_t now, char short_action, char long_action)
{
    bool pressed = gpio_get_level(pin) == BOARD_BUTTON_ACTIVE_LEVEL;
    if (pressed != b->raw) { b->raw = pressed; b->changed = now; }
    if (b->stable != b->raw && now - b->changed >= 35000) {
        b->stable = b->raw;
        if (b->stable) { b->pressed = now; b->long_sent = false; }
        else if (!b->long_sent) command(short_action);
    }
    if (b->stable && !b->long_sent && now - b->pressed >= 1500000) {
        b->long_sent = true;
        command(long_action);
    }
}

static void poll_touch(void)
{
    if (!touch) return;
    esp_lcd_touch_point_data_t points[GT911_MAX_POINTS];
    uint8_t count = 0;
    touch_error = gt911_read(touch,points,&count);
    if (touch_error != ESP_OK) return;
    touch_points = count;
    if (!count) return;
    ++screen.touch_samples;
    for (unsigned i = 0; i < count; ++i) {
        int x,y;
        if (!diagnostic_map_touch(points[i].x,points[i].y,touch_width,touch_height,screen.orientation,&x,&y)) continue;
        screen.target_mask |= diagnostic_target_hit(x,y);
        if (i == 0) {
            raw_touch_x = points[i].x; raw_touch_y = points[i].y;
            screen.touch_x = x; screen.touch_y = y;
        }
    }
    ESP_LOGI(TAG,"TOUCH raw=%u,%u mapped=%d,%d points=%u targets=0x%02x map=%u",
        raw_touch_x,raw_touch_y,(int)screen.touch_x,(int)screen.touch_y,count,
        (unsigned)screen.target_mask,(unsigned)screen.orientation);
}

void app_main(void)
{
    screen.touch_x = screen.touch_y = -1;
    gpio_error = setup_gpio();
    report("GPIO",gpio_error);
    check_memory();
    const usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_is_driver_installed()) usb_input_ready = true;
    else {
        usb_serial_jtag_driver_config_t config = usb;
        usb_input_ready = usb_serial_jtag_driver_install(&config) == ESP_OK;
    }
    setup_uart();
    setup_adc();
    if (gpio_error == ESP_OK) setup_i2c_devices();
    else i2c_error = gpio_error;
    sample_adc();
    if (accel && accel_error == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(80)+2);
        have_accel_sample = lis2dw12_idf_read(accel,&acceleration) == ESP_OK;
    }
    screen_queue = xQueueCreate(1,sizeof(screen));
    if (!screen_queue || xTaskCreate(display_task,"diag_epd",6144,NULL,4,NULL) != pdPASS) {
        ESP_LOGE(TAG,"Display task/queue allocation failed; console diagnostics remain active");
        if (screen_queue) { vQueueDelete(screen_queue); screen_queue = NULL; }
    }
    request_snapshot();
    ESP_LOGI(TAG,"DIAGNOSTICS READY: E-InkHomeArt / no automatic reboot or screen refresh");
    help();
    button_t power = {0}, boot = {0};
    unsigned uart_line_length = 0;
    char uart_command = 0;
    int64_t sample_time = 0, log_time = 0;
    int previous_lis_int = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        if (gpio_error == ESP_OK) {
            gpio_set_level(BOARD_PIN_STATUS_LED,(now/500000)%2 == 0);
            poll_button(&power,BOARD_PIN_POWER_BUTTON,now,'n','c');
            poll_button(&boot,BOARD_PIN_BOOT_BUTTON,now,'r','i');
            int level = gpio_get_level(BOARD_PIN_LIS_INT);
            if (accel_error == ESP_OK && level && !previous_lis_int) ++lis_int_edges;
            previous_lis_int = level;
        }
        char c;
        if (usb_input_ready) for (unsigned n = 0; n < 16 && usb_serial_jtag_read_bytes(&c,1,0) == 1; ++n) command(c);
        if (uart_error == ESP_OK) for (unsigned n = 0; n < 32 && uart_read_bytes(BOARD_DEBUG_UART_PORT,&c,1,0) == 1; ++n) {
            ++uart_rx_count;
            /* UART commands require a one-character line. Telemetry fed back
             * during a physical TX/RX loopback must never execute commands. */
            if (c == '\r' || c == '\n') {
                if (uart_line_length == 1) {
                    char reply[] = "UART command received\r\n";
                    uart_write_bytes(BOARD_DEBUG_UART_PORT,reply,sizeof(reply)-1);
                    command(uart_command);
                }
                uart_line_length = 0;
            } else {
                if (!uart_line_length) uart_command = c;
                if (uart_line_length < 2) ++uart_line_length;
            }
        }
        poll_touch();
        if (now - sample_time >= 1000000) {
            sample_time = now;
            sample_adc();
            if (accel && accel_error == ESP_OK) {
                lis2dw12_sample_t candidate;
                esp_err_t err = lis2dw12_idf_read(accel,&candidate);
                if (err == ESP_OK) { acceleration = candidate; have_accel_sample = true; }
                else if (err != ESP_ERR_NOT_FINISHED) { accel_error = err; have_accel_sample = false; report("ACCEL read",err); }
            }
        }
        if (now - log_time >= 5000000) {
            log_time = now;
            char live[256];
            snprintf(live,sizeof(live),"LIVE uptime=%"PRId64"s adc_raw=%d,%d,%d mv=%d,%d,%d accel=%d,%d,%dmg INT1=%"PRIu32" touch=%s UART_RX=%"PRIu32"\r\n",
                now/1000000,adc_raw[0],adc_raw[1],adc_raw[2],adc_mv[0],adc_mv[1],adc_mv[2],
                (int)acceleration.mg[0],(int)acceleration.mg[1],(int)acceleration.mg[2],lis_int_edges,esp_err_to_name(touch_error),uart_rx_count);
            ESP_LOGI(TAG,"%s",live);
            if (uart_error == ESP_OK) uart_write_bytes(BOARD_DEBUG_UART_PORT,live,strlen(live));
        }
        vTaskDelay(pdMS_TO_TICKS(20)+1);
    }
}
