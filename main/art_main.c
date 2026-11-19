#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "art_http.h"
#include "art_manager.h"
#include "board_config.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gt911.h"
#include "heartbeat.h"
#include "nvs_flash.h"

#if !CONFIG_ESP_CONSOLE_UART_DEFAULT
#error "Art MVP uses UART0 TX43/RX44 for setup details."
#endif

static const char *TAG = "ART_APP";
static char ap_ssid[33];
static gt911_handle_t touch;

static void report(const char *name, esp_err_t err)
{
    if (err == ESP_OK) ESP_LOGI(TAG,"%s: READY",name);
    else ESP_LOGE(TAG,"%s: %s",name,esp_err_to_name(err));
}

static esp_err_t setup_gpio(void)
{
    const board_gpio_output_level_t levels[] = {
        {BOARD_PIN_EPD_CS,1},{BOARD_PIN_EPD_DC,0},{BOARD_PIN_EPD_RESET,0},{BOARD_PIN_TOUCH_RESET,0},
    };
    uint64_t mask = 0;
    for (unsigned i=0; i<sizeof(levels)/sizeof(levels[0]); ++i) {
        esp_err_t err = gpio_set_level(levels[i].pin,levels[i].level);
        if (err != ESP_OK) return err;
        mask |= UINT64_C(1)<<levels[i].pin;
    }
    const gpio_config_t outputs = {.pin_bit_mask=mask,.mode=GPIO_MODE_OUTPUT};
    return gpio_config(&outputs);
}

static esp_err_t setup_uart(void)
{
    esp_err_t err = uart_param_config(BOARD_DEBUG_UART_PORT,&board_debug_uart_config);
    if (err == ESP_OK) err = uart_set_pin(BOARD_DEBUG_UART_PORT,BOARD_PIN_DEBUG_UART_TX,
        BOARD_PIN_DEBUG_UART_RX,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if (err == ESP_OK) err = gpio_set_pull_mode(BOARD_PIN_DEBUG_UART_RX,GPIO_PULLUP_ONLY);
    if (err == ESP_OK && !uart_is_driver_installed(BOARD_DEBUG_UART_PORT))
        err = uart_driver_install(BOARD_DEBUG_UART_PORT,256,0,0,NULL,0);
    return err;
}

static void setup_touch(void)
{
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&board_i2c_bus_config,&bus);
    if (err == ESP_OK) {
        const gt911_config_t cfg = {
            .bus=bus,.reset_pin=BOARD_PIN_TOUCH_RESET,.interrupt_pin=BOARD_PIN_TOUCH_INT,
            .address=GT911_ADDRESS_LOW,.clock_speed_hz=BOARD_I2C_CLOCK_HZ,.timeout_ms=BOARD_I2C_TIMEOUT_MS,
        };
        err = gt911_new(&cfg,&touch);
    }
    report("GT911 / 0x5D",err);
}

static esp_err_t setup_ap(void)
{
    /* Preserve NVS on initialization errors. Never erase user data to fix Wi-Fi. */
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    err = esp_netif_init();
    if (err == ESP_OK) err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_ap()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    uint8_t mac[6];
    err = esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) return err;
    snprintf(ap_ssid,sizeof(ap_ssid),"EInkHomeArt-%02X%02X%02X",mac[3],mac[4],mac[5]);
    wifi_config_t config = {.ap={.channel=1,.authmode=WIFI_AUTH_OPEN,.max_connection=2}};
    memcpy(config.ap.ssid,ap_ssid,strlen(ap_ssid));
    config.ap.ssid_len = (uint8_t)strlen(ap_ssid);
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP,&config);
    if (err == ESP_OK) err = esp_wifi_start();
    return err;
}

static void print_setup(void)
{
    ESP_LOGI(TAG,"Wi-Fi SSID: %s",ap_ssid);
    ESP_LOGI(TAG,"Wi-Fi: open network, no password");
    ESP_LOGI(TAG,"Photo page: http://192.168.4.1 (stay connected without Internet)");
}

void app_main(void)
{
    report("D6 heartbeat",heartbeat_start());
    esp_err_t uart_error = setup_uart();
    report("UART0 / TX43 RX44 / 115200",uart_error);
    esp_err_t err = setup_gpio();
    report("Panel/touch GPIO",err);
    uc8179_handle_t panel = NULL;
    if (err == ESP_OK) {
        setup_touch();
        err = spi_bus_initialize(BOARD_EPD_SPI_HOST,&board_epd_spi_bus_config,BOARD_EPD_SPI_DMA);
        if (err == ESP_OK) {
            const uc8179_config_t cfg = {
                .host=BOARD_EPD_SPI_HOST,.cs_pin=BOARD_PIN_EPD_CS,.dc_pin=BOARD_PIN_EPD_DC,
                .reset_pin=BOARD_PIN_EPD_RESET,.busy_pin=BOARD_PIN_EPD_BUSY,
                .clock_speed_hz=8000000,.busy_timeout_ms=20000,
            };
            err = uc8179_new(&cfg,&panel);
        }
        report("EPD SPI2 / 8 MHz",err);
    }
    /* Reserve the single packet buffer before Wi-Fi fragments internal RAM. */
    report("ArtManager",art_manager_start(panel));
    esp_err_t network = setup_ap();
    report("Wi-Fi SoftAP",network);
    if (network == ESP_OK) {
        print_setup();
        report("Photo HTTP server",art_http_start());
    }
    ESP_LOGI(TAG,"ART APP READY / persistent gray4 / ? setup / no boot calibration");
    int64_t last_live=0,last_touch=0;
    for (;;) {
        char command;
        if (uart_error == ESP_OK) while (uart_read_bytes(BOARD_DEBUG_UART_PORT,&command,1,0) == 1)
            if (command == '?' && network == ESP_OK) print_setup();
        int64_t now=esp_timer_get_time();
        if (touch) {
            esp_lcd_touch_point_data_t points[GT911_MAX_POINTS];
            uint8_t count;
            bool fresh;
            if (gt911_read_frame(touch,points,&count,&fresh) == ESP_OK && fresh && count && now-last_touch>=200000) {
                ESP_LOGI(TAG,"TOUCH count=%u x=%u y=%u",(unsigned)count,(unsigned)points[0].x,(unsigned)points[0].y);
                last_touch=now;
            }
        }
        if (now-last_live>=10000000) {
            art_manager_status_t s;
            art_manager_status(&s);
            ESP_LOGI(TAG,"LIVE saved=%" PRIu32 " shown=%" PRIu32 " state=%d heap=%u largest=%u",
                     s.saved_generation,s.displayed_generation,(int)s.display,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            last_live=now;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
