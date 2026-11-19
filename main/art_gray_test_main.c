#include <inttypes.h>
#include <stdlib.h>

#include "art_frame.h"
#include "board_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gt911.h"
#include "heartbeat.h"
#include "uc8179.h"

#if !CONFIG_ESP_CONSOLE_UART_DEFAULT
#error "Art gray verification requires the UART0 console."
#endif

#define REFRESH_GAP_US INT64_C(180000000)
_Static_assert(ART_FRAME_WIDTH == UC8179_WIDTH && ART_FRAME_HEIGHT == UC8179_HEIGHT &&
               ART_FRAME_PIXEL_BYTES == UC8179_GRAY4_FRAME_BYTES, "ArtFrame/panel geometry mismatch");
static const char *TAG = "ART_GRAY";
static QueueHandle_t requests;
static gt911_handle_t touch;
static const uint8_t *photo;

static void report(const char *operation, esp_err_t error)
{
    if (error == ESP_OK) ESP_LOGI(TAG, "%s: READY", operation);
    else ESP_LOGE(TAG, "%s: %s", operation, esp_err_to_name(error));
}

static esp_err_t setup_gpio(void)
{
    const board_gpio_output_level_t levels[] = {
        {BOARD_PIN_EPD_CS, 1}, {BOARD_PIN_EPD_DC, 0},
        {BOARD_PIN_EPD_RESET, 0}, {BOARD_PIN_TOUCH_RESET, 0},
    };
    uint64_t mask = 0;
    for (unsigned i = 0; i < sizeof(levels)/sizeof(levels[0]); ++i) {
        esp_err_t err = gpio_set_level(levels[i].pin, levels[i].level);
        if (err != ESP_OK) return err;
        mask |= UINT64_C(1) << levels[i].pin;
    }
    const gpio_config_t outputs = { .pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT };
    return gpio_config(&outputs);
}

static esp_err_t setup_uart(void)
{
    esp_err_t err = uart_param_config(BOARD_DEBUG_UART_PORT, &board_debug_uart_config);
    if (err == ESP_OK) err = uart_set_pin(BOARD_DEBUG_UART_PORT,
        BOARD_PIN_DEBUG_UART_TX, BOARD_PIN_DEBUG_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err == ESP_OK) err = gpio_set_pull_mode(BOARD_PIN_DEBUG_UART_RX, GPIO_PULLUP_ONLY);
    if (err == ESP_OK && !uart_is_driver_installed(BOARD_DEBUG_UART_PORT))
        err = uart_driver_install(BOARD_DEBUG_UART_PORT, 256, 0, 0, NULL, 0);
    return err;
}

static void setup_touch(void)
{
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&board_i2c_bus_config, &bus);
    if (err == ESP_OK) {
        const gt911_config_t cfg = {
            .bus = bus, .reset_pin = BOARD_PIN_TOUCH_RESET, .interrupt_pin = BOARD_PIN_TOUCH_INT,
            .width = 0, .height = 0, .address = GT911_ADDRESS_LOW,
            .clock_speed_hz = BOARD_I2C_CLOCK_HZ, .timeout_ms = BOARD_I2C_TIMEOUT_MS,
        };
        err = gt911_new(&cfg, &touch);
    }
    report("GT911 / 0x5D", err);
}

static void load_embedded_photo(void)
{
#ifdef ART_TEST_FRAME_EMBEDDED
    extern const uint8_t packet_start[] asm("_binary_art_test_frame_start");
    extern const uint8_t packet_end[] asm("_binary_art_test_frame_end");
    art_frame_view_t view;
    size_t length = (size_t)((uintptr_t)packet_end - (uintptr_t)packet_start);
    art_frame_result_t result = art_frame_parse(packet_start, length, &view);
    if (result == ART_FRAME_OK) {
        photo = view.pixels;
        ESP_LOGI(TAG, "Embedded photo: validated, CRC=%08" PRIx32, view.pixel_crc);
    } else ESP_LOGE(TAG, "Embedded photo rejected: ArtFrame error=%d", (int)result);
#else
    ESP_LOGI(TAG, "No embedded photo; calibration available. See docs/ART_GRAY_TEST.md.");
#endif
}

static void render_calibration(uint8_t *pixels)
{
    /* Upper half: black -> white. Lower half: white -> black.
     * The 200-pixel bands align to four-pixel packets. */
    for (unsigned y = 0; y < ART_FRAME_HEIGHT; ++y) {
        for (unsigned x = 0; x < ART_FRAME_WIDTH; x += 4) {
            unsigned level = x / 200;
            if (y >= ART_FRAME_HEIGHT/2) level = 3 - level;
            pixels[(y*ART_FRAME_WIDTH+x)/4] = (uint8_t)(level * 0x55);
        }
    }
}

static void display_worker(void *arg)
{
    (void)arg;
    uint8_t *calibration = heap_caps_malloc(ART_FRAME_PIXEL_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!calibration) {
        report("Calibration buffer", ESP_ERR_NO_MEM);
        vTaskDelete(NULL);
        return;
    }
    render_calibration(calibration);
    esp_err_t err = spi_bus_initialize(BOARD_EPD_SPI_HOST, &board_epd_spi_bus_config, BOARD_EPD_SPI_DMA);
    uc8179_handle_t panel = NULL;
    if (err == ESP_OK) {
        const uc8179_config_t cfg = {
            .host = BOARD_EPD_SPI_HOST, .cs_pin = BOARD_PIN_EPD_CS, .dc_pin = BOARD_PIN_EPD_DC,
            .reset_pin = BOARD_PIN_EPD_RESET, .busy_pin = BOARD_PIN_EPD_BUSY,
            .clock_speed_hz = 8000000, .busy_timeout_ms = 20000,
        };
        err = uc8179_new(&cfg, &panel);
    }
    report("EPD SPI2 / 8 MHz", err);
    if (err != ESP_OK) {
        free(calibration);
        vTaskDelete(NULL);
        return;
    }
    int64_t next_refresh = 0;
    char selected;
    for (;;) {
        if (xQueueReceive(requests, &selected, portMAX_DELAY) != pdTRUE) continue;
        /* Latest command wins while cooling down. UART, touch and heartbeat
         * run independently; no periodic refreshes or automatic retries. */
        if (esp_timer_get_time() < next_refresh) {
            ESP_LOGI(TAG, "Full refresh queued; waiting %" PRIi64 " s", (next_refresh-esp_timer_get_time()+999999)/1000000);
            while (esp_timer_get_time() < next_refresh) {
                char newer;
                if (xQueueReceive(requests, &newer, pdMS_TO_TICKS(100)) == pdTRUE) selected = newer;
            }
        }
        const uint8_t *pixels = selected == 'p' ? photo : calibration;
        if (!pixels) { ESP_LOGW(TAG, "Photo unavailable; use g for calibration"); continue; }
        const int64_t start = esp_timer_get_time();
        next_refresh = start + REFRESH_GAP_US;
        ESP_LOGI(TAG, "BEGIN %s / 800x480 / 4 gray / 96000 bytes", selected == 'p' ? "PHOTO" : "CALIBRATION");
        err = uc8179_init_gray4(panel);
        if (err == ESP_OK) err = uc8179_display_gray4(panel, pixels, ART_FRAME_PIXEL_BYTES);
        esp_err_t shutdown = err == ESP_OK ? uc8179_sleep(panel) : uc8179_power_off(panel);
        report("Panel shutdown", shutdown);
        if (err == ESP_OK) err = shutdown;
        uc8179_update_stats_t stats;
        if (uc8179_get_update_stats(panel, &stats) == ESP_OK)
            ESP_LOGI(TAG, "PERF bytes=%" PRIu32 " power=%" PRIi64 "ms spi=%" PRIi64 "ms busy=%" PRIi64 "ms total=%" PRIi64 "ms",
                     stats.bytes, stats.power_on_us/1000, stats.transfer_us/1000, stats.refresh_us/1000,
                     (esp_timer_get_time()-start)/1000);
        report("GRAY4 transfer + BUSY + shutdown", err);
        ESP_LOGI(TAG, "Check physical shades; no automatic refresh. g=calibration p=photo");
    }
}

void app_main(void)
{
    report("D6 heartbeat", heartbeat_start());
    esp_err_t err = setup_gpio();
    report("Panel/touch GPIO", err);
    const bool gpio_ready = err == ESP_OK;
    esp_err_t uart_error = setup_uart();
    report("UART0 / TX43 RX44 / 115200", uart_error);
    load_embedded_photo();
    requests = xQueueCreate(1, sizeof(char));
    if (gpio_ready) setup_touch();
    if (gpio_ready && requests) {
        char command = 'g';
        xQueueOverwrite(requests, &command);
        if (xTaskCreate(display_worker, "art_gray_epd", 4096, NULL, 4, NULL) != pdPASS)
            report("Display task", ESP_ERR_NO_MEM);
    } else report("Display setup", gpio_ready ? ESP_ERR_NO_MEM : err);
    ESP_LOGI(TAG, "ART GRAY TEST READY / g calibration | p embedded photo | ? help / gap 180 s");
    int64_t last_live = 0, last_touch_log = 0;
    for (;;) {
        char command;
        if (uart_error == ESP_OK) while (uart_read_bytes(BOARD_DEBUG_UART_PORT, &command, 1, 0) == 1) {
            if (command == 'g' || command == 'p') {
                if (command == 'p' && !photo) ESP_LOGW(TAG, "No valid embedded photo");
                else if (requests) xQueueOverwrite(requests, &command);
            } else if (command == '?') ESP_LOGI(TAG, "g calibration | p embedded photo; full refreshes >=180 s apart");
        }
        int64_t now = esp_timer_get_time();
        if (touch) {
            esp_lcd_touch_point_data_t points[GT911_MAX_POINTS];
            uint8_t count;
            bool fresh;
            esp_err_t touch_error = gt911_read_frame(touch, points, &count, &fresh);
            if (touch_error == ESP_OK && fresh && now-last_touch_log >= 200000) {
                if (count) ESP_LOGI(TAG, "TOUCH count=%u x=%u y=%u", (unsigned)count,
                                   (unsigned)points[0].x, (unsigned)points[0].y);
                last_touch_log = now;
            }
        }
        if (now-last_live >= 5000000) {
            ESP_LOGI(TAG, "LIVE uptime=%" PRIi64 "s; touch does not trigger gray refresh", now/1000000);
            last_live = now;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
