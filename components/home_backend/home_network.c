#include "home_network.h"
#include "home_private.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG="HOME_WIFI";
static home_network_status_t state;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t reconnect_task;
static void event(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    (void)arg;
    if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){
        const ip_event_got_ip_t *e=data;
        portENTER_CRITICAL(&lock);
        state.connected=true;state.disconnect_reason=0;
        snprintf(state.ip,sizeof(state.ip),IPSTR,IP2STR(&e->ip_info.ip));
        portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG,"Home LAN: http://" IPSTR,IP2STR(&e->ip_info.ip));
        xTaskNotifyGive(reconnect_task);
    }else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){
        const wifi_event_sta_disconnected_t *e=data;
        portENTER_CRITICAL(&lock);state.connected=false;state.ip[0]=0;state.disconnect_reason=e->reason;portEXIT_CRITICAL(&lock);
        ESP_LOGW(TAG,"Home Wi-Fi disconnected, reason=%u; retry scheduled",(unsigned)e->reason);
        xTaskNotifyGive(reconnect_task);
    }else if(base==IP_EVENT&&id==IP_EVENT_STA_LOST_IP){
        portENTER_CRITICAL(&lock);state.connected=false;state.ip[0]=0;portEXIT_CRITICAL(&lock);
        xTaskNotifyGive(reconnect_task);
    }
}
void home_network_status(home_network_status_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&lock);*out=state;portEXIT_CRITICAL(&lock);
    out->time_ready=out->connected&&time(NULL)>1735689600;
}
static void reconnect(void *arg)
{
    (void)arg;ulTaskNotifyTake(pdTRUE,portMAX_DELAY);bool sntp_started=false;unsigned backoff=2;
    for(;;){
        home_network_status_t s;home_network_status(&s);
        if(s.connected){
            backoff=2;
            if(!sntp_started){
                esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
                esp_sntp_setservername(0,"pool.ntp.org");esp_sntp_setservername(1,"time.google.com");
                esp_sntp_init();sntp_started=true;
            }
            ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1000));
        }else{
            esp_err_t err=esp_wifi_connect();
            if(err!=ESP_OK)ESP_LOGW(TAG,"Connect: %s",esp_err_to_name(err));
            /* A disconnect event must not cause an immediate retry loop. */
            vTaskDelay(pdMS_TO_TICKS(backoff*1000));
            backoff=backoff<30?backoff*2:60;
        }
    }
}
esp_err_t home_network_start(void)
{
    esp_err_t err=nvs_flash_init();if(err!=ESP_OK)return err;
    err=esp_netif_init();if(err!=ESP_OK)return err;
    err=esp_event_loop_create_default();if(err!=ESP_OK)return err;
    if(!esp_netif_create_default_wifi_ap()||!esp_netif_create_default_wifi_sta())return ESP_ERR_NO_MEM;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&init);if(err!=ESP_OK)return err;
    uint8_t mac[6];err=esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP);if(err!=ESP_OK)return err;
    snprintf(state.ap_ssid,sizeof(state.ap_ssid),"EInkHomeArt-%02X%02X%02X",mac[3],mac[4],mac[5]);
    wifi_config_t ap={.ap={.channel=1,.authmode=WIFI_AUTH_OPEN,.max_connection=2}};
    memcpy(ap.ap.ssid,state.ap_ssid,strlen(state.ap_ssid));ap.ap.ssid_len=strlen(state.ap_ssid);
    state.configured=HOME_WIFI_SSID[0]!=0;
    wifi_config_t sta={0};
    memcpy(sta.sta.ssid,HOME_WIFI_SSID,strlen(HOME_WIFI_SSID));
    memcpy(sta.sta.password,HOME_WIFI_PASSWORD,strlen(HOME_WIFI_PASSWORD));
    sta.sta.threshold.authmode=HOME_WIFI_PASSWORD[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    sta.sta.pmf_cfg.capable=true;
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if(err==ESP_OK)err=esp_wifi_set_mode(state.configured?WIFI_MODE_APSTA:WIFI_MODE_AP);
    if(err==ESP_OK)err=esp_wifi_set_config(WIFI_IF_AP,&ap);
    if(err==ESP_OK&&state.configured)err=esp_wifi_set_config(WIFI_IF_STA,&sta);
    if(err==ESP_OK&&state.configured){
        if(xTaskCreate(reconnect,"home_wifi",3072,NULL,3,&reconnect_task)!=pdPASS)return ESP_ERR_NO_MEM;
        err=esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,event,NULL);
        if(err==ESP_OK)err=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL);
        if(err==ESP_OK)err=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_LOST_IP,event,NULL);
    }
    if(err==ESP_OK)err=esp_wifi_start();
    if(err==ESP_OK&&reconnect_task)xTaskNotifyGive(reconnect_task);
    if(err==ESP_OK)ESP_LOGI(TAG,"Open photo AP: %s / http://192.168.4.1; home Wi-Fi configured=%d",state.ap_ssid,state.configured);
    return err;
}
