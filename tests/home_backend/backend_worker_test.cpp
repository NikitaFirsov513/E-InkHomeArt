#include "home_backend.h"
#include "home_network.h"
#include "HttpsClient.hpp"
#include "freertos/queue.h"
#include "esp_mac.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
static void (*entry)(void *);
static uint64_t now;
static home_live_data_t shown;
static home_command_t queued;
static bool has_queue,online=true,clock_ready=true,lamp_on;
static unsigned step,posts,gets,polls,saves;
static std::string saved;
struct Finished {};
int64_t esp_timer_get_time(){return now*1000000;}
bool esp_psram_is_initialized(){return true;}
esp_err_t esp_read_mac(uint8_t *mac,int){memset(mac,1,6);return ESP_OK;}
extern "C" void home_network_status(home_network_status_t *out){*out={};out->connected=online;out->time_ready=online&&clock_ready;snprintf(out->ip,sizeof(out->ip),"192.168.1.2");}
bool loadAuthSession(std::string &out){out=saved;return true;}
bool saveAuthSession(const std::string &value){saved=value;++saves;return true;}
QueueHandle_t xQueueCreate(unsigned count,size_t size){assert(count==1&&size==sizeof(home_command_t));return reinterpret_cast<void *>(1);}
void vQueueDelete(QueueHandle_t){}
int xQueueSend(QueueHandle_t,const void *data,unsigned){assert(!has_queue);queued=*static_cast<const home_command_t *>(data);has_queue=true;return 1;}
int xTaskCreate(void (*fn)(void *),const char *,unsigned stack,void *,unsigned,void **){assert(stack==16384);entry=fn;return 1;}
static void request(home_command_kind_t kind,uint32_t epoch=0,double value=0){home_command_t c{kind,epoch,0,value};assert(home_backend_submit(&c));assert(!home_backend_submit(&c));}
int xQueueReceive(QueueHandle_t,void *out,unsigned){
 switch(step++){
 case 0:clock_ready=false;request(HOME_CMD_LOGIN);break;
 case 1:assert(!shown.waiting&&posts==0&&polls==0);clock_ready=true;request(HOME_CMD_LOGIN);break;
 case 2:assert(shown.waiting&&!shown.authorized);now=6;break;
 case 3:now=12;break;
 case 4:assert(shown.authorized&&shown.loaded&&shown.device_count==1&&!shown.devices[0].on&&saves==1);request(HOME_CMD_TOGGLE,shown.epoch,1);break;
 case 5:assert(posts==1&&shown.devices[0].on&&!shown.pending);request(HOME_CMD_TOGGLE,shown.epoch,0);break;
 case 6:assert(posts==2&&!shown.devices[0].on);online=false;request(HOME_CMD_TOGGLE,shown.epoch,1);break;
 case 7:assert(posts==2&&!shown.online);online=true;now=100;break;
 case 8:assert(shown.loaded&&std::string(shown.message)=="Устройства обновлены");request(HOME_CMD_TOGGLE,shown.epoch-1,1);break;
 case 9:assert(posts==2);request(HOME_CMD_LOGOUT);break;
 case 10:assert(!shown.authorized&&!shown.device_count&&saved.empty()&&saves==2&&posts==2);throw Finished{};
 }
 if(has_queue){*static_cast<home_command_t *>(out)=queued;has_queue=false;return 1;}return 0;
}
HttpsClient::HttpsClient(const char *url,size_t){baseUrl=url;}
HttpsClient::~HttpsClient()=default;
HttpsRes_t HttpsClient::send(const HttpsReq_t &req){
 static std::string response;int status=200;esp_err_t err=ESP_OK;
 std::string path=req.path;
 if(path=="/device/code"){
  assert(clock_ready&&online);response=R"({"device_code":"test-code","user_code":"123456","verification_url":"https://ya.ru/device","expires_in":300,"interval":5})";
 }else if(path=="/token"){
  if(++polls==1){status=400;response=R"({"error":"authorization_pending"})";}
  else response=R"({"token_type":"bearer","access_token":"test-access","refresh_token":"test-refresh","expires_in":3600,"scope":"iot:view iot:control"})";
 }else if(path=="/v1.0/user/info"){
  assert(req.method==HTTP_METHOD_GET);++gets;
  response=R"({"status":"ok","rooms":[],"scenarios":[],"devices":[{"id":"lamp","name":"Lamp","type":"devices.types.light","capabilities":[{"type":"devices.capabilities.on_off","state":{"instance":"on","value":)";
  response+=lamp_on?"true":"false";response+="}}]}]}";
 }else if(path=="/v1.0/devices/actions"){
  assert(req.method==HTTP_METHOD_POST&&shown.pending);assert(shown.devices[0].on==lamp_on);++posts;
  lamp_on=!lamp_on;
  response=R"({"status":"ok","devices":[{"id":"lamp","capabilities":[{"type":"devices.capabilities.on_off","state":{"instance":"on","action_result":{"status":"DONE"}}}]}]})";
  if(posts==2){err=ESP_ERR_TIMEOUT;status=0;response.clear();} // Action may have executed; do not repeat it.
 }else if(path=="/revoke_token")response="{}";
 else assert(false);
 return {err,status,response.c_str(),response.size()};
}
int main(){
 assert(home_backend_start([](const home_live_data_t *data){shown=*data;})==ESP_OK&&entry);
 try{entry(nullptr);assert(false);}catch(const Finished &){}
 assert(gets>=4&&posts==2);
 puts("PASS: actual backend worker: clock gate, OAuth polling/save, real catalog, command queue, confirmed/readback states, uncertain POST without retry, offline/recovery, stale epoch and logout.");
}
