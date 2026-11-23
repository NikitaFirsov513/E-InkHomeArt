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
#include "home_ui.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "home_network.h"
#include "home_backend.h"
#include "esp_psram.h"

#if !CONFIG_ESP_CONSOLE_UART_DEFAULT
#error "Art MVP uses UART0 TX43/RX44 for setup details."
#endif

static const char *TAG = "ART_APP";
static gt911_handle_t touch;
static uint16_t touch_width,touch_height;
static unsigned touch_map;

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
    if(touch)gt911_get_native_resolution(touch,&touch_width,&touch_height);
}

static bool saved_dark_theme;
static int64_t next_theme_save;
static void restore_theme(void)
{
    nvs_handle_t handle;uint8_t dark=0;
    esp_err_t err=nvs_open("panel_ui",NVS_READONLY,&handle);
    if(err==ESP_OK){err=nvs_get_u8(handle,"dark",&dark);nvs_close(handle);}
    if(err!=ESP_OK&&err!=ESP_ERR_NVS_NOT_FOUND)ESP_LOGW(TAG,"Theme load: %s",esp_err_to_name(err));
    saved_dark_theme=err==ESP_OK&&dark==1;
    art_manager_set_dark_theme(saved_dark_theme);
}
static void persist_theme(int64_t now)
{
    art_manager_status_t s;art_manager_status(&s);
    if(s.dark_theme==saved_dark_theme||now<next_theme_save||s.uploading||
       s.display!=ART_DISPLAY_READY||s.ui_revision!=s.ui_shown_revision)return;
    next_theme_save=now+INT64_C(10000000);
    nvs_handle_t handle;esp_err_t err=nvs_open("panel_ui",NVS_READWRITE,&handle);
    if(err==ESP_OK){
        err=nvs_set_u8(handle,"dark",s.dark_theme?1:0);
        if(err==ESP_OK)err=nvs_commit(handle);
        nvs_close(handle);
    }
    if(err==ESP_OK){saved_dark_theme=s.dark_theme;next_theme_save=0;ESP_LOGI(TAG,"UI theme saved: %s",saved_dark_theme?"dark":"light");}
    else ESP_LOGW(TAG,"Theme save: %s",esp_err_to_name(err));
}

static void print_setup(void)
{
    home_network_status_t net;home_network_status(&net);
    ESP_LOGI(TAG,"Photo AP: %s",net.ap_ssid);
    ESP_LOGI(TAG,"Home connected=%d clock=%d IP=%s",net.connected,net.time_ready,net.ip);
    ESP_LOGI(TAG,"Wi-Fi: open network, no password");
    ESP_LOGI(TAG,"Photo page: http://192.168.4.1 (stay connected without Internet)");
    ESP_LOGI(TAG,"UI: first touch opens home / 45 s idle returns photo / h menu | a art | l login | p poll | d logout | i theme | t map");
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
    esp_err_t live_model=art_manager_enable_home(home_backend_submit);
    report("Live home model",live_model);
    ESP_LOGI(TAG,"PSRAM initialized=%d size=%u",esp_psram_is_initialized(),(unsigned)esp_psram_get_size());
    esp_err_t network = home_network_start();
    report("Wi-Fi AP + home STA",network);
    if (network == ESP_OK) {
        restore_theme();
        print_setup();
        report("Photo HTTP server",art_http_start());
        esp_err_t backend=live_model==ESP_OK?home_backend_start(art_manager_home_update):live_model;
        report("Yandex backend",backend);
        if(backend!=ESP_OK){
            home_live_data_t *failed=heap_caps_calloc(1,sizeof(*failed),MALLOC_CAP_8BIT);
            if(failed){failed->epoch=1;snprintf(failed->message,sizeof(failed->message),"Yandex backend unavailable. Check PSRAM/UART");art_manager_home_update(failed);heap_caps_free(failed);}
        }
    }
    ESP_LOGI(TAG,"ART UI READY / UI_CAPS_V4 QR1 THEMES1 / Yandex live home / 45 s return / UART ? h a i t / photo upload has no cooldown");
    int64_t last_live=0,last_touch=0;
    for (;;) {
        char command;
        if (uart_error == ESP_OK) while (uart_read_bytes(BOARD_DEBUG_UART_PORT,&command,1,0) == 1){
            if(command=='?'&&network==ESP_OK)print_setup();
            else if(command=='h'&&live_model==ESP_OK)art_manager_open_menu();
            else if(command=='a')art_manager_show_art();
            else if(command=='l'||command=='p'||command=='d'){
                home_command_t c={.kind=command=='l'?HOME_CMD_LOGIN:command=='d'?HOME_CMD_LOGOUT:HOME_CMD_REFRESH};
                ESP_LOGI(TAG,"Home request queued=%d",home_backend_submit(&c));
                if(command=='l')art_manager_open_menu();
            }
            else if(command=='i'){
                art_manager_status_t status;art_manager_status(&status);
                art_manager_set_dark_theme(!status.dark_theme);
            }
            else if(command=='t'){
                touch_map=(touch_map+1)%8;art_manager_cancel_touch();
                ESP_LOGI(TAG,"Touch map=%u native=%ux%u",touch_map,(unsigned)touch_width,(unsigned)touch_height);
            }
        }
        int64_t now=esp_timer_get_time();
        if (touch) {
            esp_lcd_touch_point_data_t points[GT911_MAX_POINTS];
            uint8_t count;
            bool fresh;
            esp_err_t read=gt911_read_frame(touch,points,&count,&fresh);
            if(read!=ESP_OK)art_manager_cancel_touch();
            else if(fresh){
                int x=-1,y=-1;
                if(count==1)home_ui_map_touch(points[0].x,points[0].y,touch_width,touch_height,touch_map,&x,&y);
                if(live_model==ESP_OK)art_manager_touch(count,x,y,count?points[0].track_id:0,now);
                if(count&&now-last_touch>=200000){
                    ESP_LOGI(TAG,"TOUCH count=%u x=%d y=%d map=%u",(unsigned)count,x,y,touch_map);last_touch=now;
                }
            }
        }
        if(network==ESP_OK)persist_theme(now);
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
