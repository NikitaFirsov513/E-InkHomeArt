#include "home_backend.h"
#include "home_network.h"
#include "home_private.h"
#include "home_catalog.hpp"
#include "YandexAuth.hpp"
#include "AuthStorage.hpp"
#include "HttpsClient.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

static QueueHandle_t commands;
static home_publish_fn publish;
static portMUX_TYPE gate=portMUX_INITIALIZER_UNLOCKED;
static bool busy;
static uint64_t seconds(){return esp_timer_get_time()/1000000;}
extern "C" bool home_backend_submit(const home_command_t *command)
{
    if(!command||command->kind==HOME_CMD_NONE||!commands)return false;
    portENTER_CRITICAL(&gate);bool accepted=!busy;if(accepted)busy=true;portEXIT_CRITICAL(&gate);
    if(!accepted)return false;
    if(xQueueSend(commands,command,0)==pdTRUE)return true;
    portENTER_CRITICAL(&gate);busy=false;portEXIT_CRITICAL(&gate);return false;
}
static void worker(void *)
{
    auto catalog=std::make_unique<HomeCatalog>();
    auto view=std::make_unique<home_live_data_t>();
    HttpsClient oauth("https://oauth.yandex.ru",8192),api("https://api.iot.yandex.net",256*1024);
    uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);char id[32];
    snprintf(id,sizeof(id),"eink-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    YandexAuth auth(HOME_OAUTH_ID,HOME_OAUTH_SECRET,id,[&](const char *path,const std::string &body){
        home_network_status_t net;home_network_status(&net);
        if(!net.time_ready)return OAuthReply{false,0,"","NETWORK_OR_CLOCK"};
        HttpsReq_t req;req.path=path;req.method=HTTP_METHOD_POST;req.body=body.c_str();req.content_type="application/x-www-form-urlencoded";
        auto r=oauth.send(req);return OAuthReply{r.transport_error==ESP_OK,r.http_status,r.body?std::string(r.body,r.body_len):"",esp_err_to_name(r.transport_error)};
    },saveAuthSession);
    std::string session,message;
    if(loadAuthSession(session))auth.restore(session);else message="Не удалось прочитать хранилище входа";
    session.clear();
    auto update=[&](){
        auto a=auth.snapshot(seconds());home_network_status_t n;home_network_status(&n);
        *view=catalog->data;view->online=n.connected;view->time_ready=n.time_ready;view->authorized=a.authorized;view->waiting=a.state==AuthState::Waiting;
        portENTER_CRITICAL(&gate);view->pending=busy;portEXIT_CRITICAL(&gate);
        snprintf(view->ip,sizeof(view->ip),"%s",n.ip);snprintf(view->code,sizeof(view->code),"%s",a.user_code);snprintf(view->url,sizeof(view->url),"%s",a.verification_url);
        homeText(view->message,sizeof(view->message),message.empty()?a.message:message);
        if(publish)publish(view.get());
    };
    uint64_t next_poll=0;bool had_auth=auth.authorized();
    for(;;){
        home_command_t cmd{};bool got=xQueueReceive(commands,&cmd,pdMS_TO_TICKS(200))==pdTRUE;
        home_network_status_t net;home_network_status(&net);
        if(got){
            message.clear();update();
            if(cmd.kind==HOME_CMD_CANCEL)auth.cancel();
            else if(cmd.kind==HOME_CMD_LOGOUT){if(auth.logout()){catalog->clear();next_poll=0;}else message="Не удалось сохранить выход";}
            else if(!net.connected)message="Подключитесь к домашней сети и повторите";
            else if(!net.time_ready)message="Ожидаем время SNTP. Повторите вход позже";
            else if(cmd.kind==HOME_CMD_LOGIN)auth.begin(seconds());
            else if(cmd.kind==HOME_CMD_REFRESH)next_poll=0;
            else if(cmd.kind==HOME_CMD_TOGGLE||cmd.kind==HOME_CMD_RANGE||cmd.kind==HOME_CMD_SCENE){
                std::string path,body;DeviceAction action;
                if(!auth.ensureToken(seconds()))message="Сначала войдите в Яндекс";
                else if(!catalog->command(cmd,path,body,action))message="Список изменился. Откройте устройство заново";
                else{
                    HttpsReq_t request;request.path=path.c_str();request.method=HTTP_METHOD_POST;request.body=body.c_str();request.token=auth.accessToken().c_str();
                    auto reply=api.send(request);
                    if(reply.http_status==401){bool renewed=auth.ensureToken(seconds(),true);message=renewed?"Вход обновлён. Повторите команду":"Вход недоступен. Войдите в настройках";}
                    else if(!reply.ok())message="Нет подтверждения команды. Проверьте устройство";
                    else{
                        std::unique_ptr<cJSON,decltype(&cJSON_Delete)> root(cJSON_ParseWithLength(reply.body,reply.body_len),cJSON_Delete);
                        bool ok=false;
                        if(cmd.kind==HOME_CMD_SCENE){const auto *status=cJSON_GetObjectItemCaseSensitive(root.get(),"status");ok=cJSON_IsString(status)&&!strcmp(status->valuestring,"ok");message=ok?"Сценарий запущен":"Сценарий не подтверждён";}
                        else ok=deviceActionSucceeded(root.get(),action,message);
                        ESP_LOGI("HOME_API","Action result=%s",ok?"confirmed":"rejected");
                    }
                    next_poll=0; // Read state back; POST is never retried automatically.
                }
            }
        }
        auth.tick(seconds(),net.time_ready);
        bool authorized=auth.authorized();
        if(had_auth&&!authorized){catalog->clear();next_poll=0;}
        if(!had_auth&&authorized){next_poll=0;message.clear();}
        had_auth=authorized;
        if(authorized&&net.time_ready&&seconds()>=next_poll){
            next_poll=seconds()+10;
            if(auth.ensureToken(seconds())){
                HttpsReq_t request;request.path="/v1.0/user/info";request.token=auth.accessToken().c_str();
                auto reply=api.send(request);
                if(reply.http_status==401&&auth.ensureToken(seconds(),true)){request.token=auth.accessToken().c_str();reply=api.send(request);}
                if(reply.ok()){
                    std::unique_ptr<cJSON,decltype(&cJSON_Delete)> root(cJSON_ParseWithLength(reply.body,reply.body_len),cJSON_Delete);
                    if(catalog->load(root.get())){next_poll=seconds()+60;if(!got||cmd.kind==HOME_CMD_REFRESH)message="Устройства обновлены";}
                    else {catalog->data.loaded=false;message="Некорректный список устройств Яндекса";}
                }else {catalog->data.loaded=false;message=reply.transport_error==ESP_ERR_INVALID_SIZE?"Список больше 256 КБ. Нужна настройка лимита":"Не удалось обновить устройства";}
            }else {catalog->data.loaded=false;message=auth.snapshot(seconds()).message;}
        }
        if(got){portENTER_CRITICAL(&gate);busy=false;portEXIT_CRITICAL(&gate);}
        update();
    }
}
extern "C" esp_err_t home_backend_start(home_publish_fn callback)
{
    if(commands)return ESP_ERR_INVALID_STATE;
    publish=callback;
    if(!esp_psram_is_initialized())return ESP_ERR_NO_MEM;
    commands=xQueueCreate(1,sizeof(home_command_t));if(!commands)return ESP_ERR_NO_MEM;
    if(xTaskCreate(worker,"yandex_home",16384,nullptr,3,nullptr)!=pdPASS){vQueueDelete(commands);commands=nullptr;return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
