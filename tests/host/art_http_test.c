#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "art_http.h"
#include "art_manager.h"
#include "esp_http_server.h"

#define ASSET(name) const uint8_t _binary_##name##_start[1]={0}; const uint8_t _binary_##name##_end[1]={0}
ASSET(art_index); ASSET(art_style); ASSET(art_codec); ASSET(art_client); ASSET(art_upload_client);
static httpd_uri_t routes[8];
static unsigned route_count;
static uint8_t incoming[ART_FRAME_PACKET_BYTES],scratch[ART_FRAME_PACKET_BYTES],pixels[ART_FRAME_PIXEL_BYTES];
static size_t offset,chunk,disconnect_at;
static int64_t clock_us;
static bool missing_header,timeouts,leased,force_save_error,closed;
static unsigned commits,aborts;
static esp_err_t lease_error;
static char response[800],status[64];
static art_manager_status_t snapshot;

const char *esp_err_to_name(esp_err_t err) { return err==ESP_OK?"ESP_OK":"ESP_FAIL"; }
int64_t esp_timer_get_time(void) { return clock_us; }
esp_err_t httpd_start(httpd_handle_t *handle,const httpd_config_t *cfg)
{ assert(cfg->max_uri_handlers>=7); *handle=(void *)1; return ESP_OK; }
esp_err_t httpd_stop(httpd_handle_t handle) { (void)handle; return ESP_OK; }
esp_err_t httpd_register_uri_handler(httpd_handle_t handle,const httpd_uri_t *uri)
{ (void)handle; assert(route_count<8); routes[route_count++]=*uri; return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *req,const char *type) { (void)req;(void)type;return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *req,const char *key,const char *value)
{ (void)req; if(!strcmp(key,"Connection")&&!strcmp(value,"close"))closed=true; return ESP_OK; }
esp_err_t httpd_resp_set_status(httpd_req_t *req,const char *value)
{ (void)req; snprintf(status,sizeof(status),"%s",value); return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t *req,const char *bytes,int length)
{ (void)req; assert(length>=0&&(size_t)length<sizeof(response));memcpy(response,bytes,(size_t)length);response[length]=0;return ESP_OK; }
esp_err_t httpd_resp_sendstr(httpd_req_t *req,const char *bytes)
{ return httpd_resp_send(req,bytes,(int)strlen(bytes)); }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req,const char *key,char *out,size_t capacity)
{
    (void)req;
    if(missing_header)return ESP_ERR_NOT_FOUND;
    snprintf(out,capacity,"%s",!strcmp(key,"X-Art-Upload")?"1":"application/octet-stream");
    return ESP_OK;
}
int httpd_req_recv(httpd_req_t *req,char *out,size_t count)
{
    (void)req;
    assert(leased);
    if(timeouts){clock_us+=4000000;return HTTPD_SOCK_ERR_TIMEOUT;}
    if(offset>=disconnect_at)return 0;
    if(count>chunk)count=chunk;
    if(count>disconnect_at-offset)count=disconnect_at-offset;
    memcpy(out,incoming+offset,count);offset+=count;return (int)count;
}
void art_manager_status(art_manager_status_t *out) { *out=snapshot; }
esp_err_t art_manager_begin_upload(uint8_t **packet)
{ assert(!leased);if(lease_error)return lease_error;leased=true;*packet=scratch;return ESP_OK; }
void art_manager_abort_upload(void) { assert(leased);leased=false;++aborts; }
art_store_result_t art_manager_commit_upload(size_t length)
{
    assert(leased&&offset==ART_FRAME_PACKET_BYTES);leased=false;++commits;
    art_frame_view_t frame;
    if(art_frame_parse(scratch,length,&frame)!=ART_FRAME_OK)return ART_STORE_INVALID_FRAME;
    if(force_save_error)return ART_STORE_IO;
    snapshot.saved_generation=42;snapshot.pixel_crc=frame.pixel_crc;snapshot.has_frame=true;
    snapshot.display=ART_DISPLAY_QUEUED;
    return ART_STORE_OK;
}
static esp_err_t request(const char *path,size_t length)
{
    for(unsigned i=0;i<route_count;++i)if(!strcmp(routes[i].uri,path)){
        httpd_req_t req={.content_len=length,.user_ctx=routes[i].user_ctx};return routes[i].handler(&req);
    }
    assert(false);return ESP_FAIL;
}
static void reset(void)
{
    offset=0;chunk=17;disconnect_at=ART_FRAME_PACKET_BYTES;clock_us=0;
    missing_header=timeouts=leased=force_save_error=closed=false;
    commits=aborts=0;lease_error=ESP_OK;response[0]=0;snprintf(status,sizeof(status),"200 OK");
    snapshot=(art_manager_status_t){.storage_ready=true};
    assert(art_frame_encode(incoming,sizeof(incoming),pixels,sizeof(pixels))==ART_FRAME_OK);
}
int main(void)
{
    for(unsigned i=0;i<sizeof(pixels);++i)pixels[i]=(uint8_t)i;
    assert(art_http_start()==ESP_OK&&route_count==7);
    reset();assert(request("/api/image",ART_FRAME_PACKET_BYTES)==ESP_OK);
    assert(commits==1&&!aborts&&!leased&&!closed&&!memcmp(incoming,scratch,sizeof(incoming)));
    assert(!strcmp(status,"202 Accepted")&&strstr(response,"\"saved\":true")&&strstr(response,"\"generation\":42"));
    reset();assert(request("/api/image",ART_FRAME_PACKET_BYTES-1)==ESP_FAIL&&!commits&&!aborts&&closed);
    reset();incoming[0]^=1;assert(request("/api/image",ART_FRAME_PACKET_BYTES)==ESP_FAIL);
    assert(aborts==1&&!commits&&!leased&&offset<100&&closed);
    reset();incoming[1000]^=1;assert(request("/api/image",ART_FRAME_PACKET_BYTES)==ESP_FAIL);
    assert(commits==1&&!leased&&!strcmp(status,"400 Bad Request"));
    reset();disconnect_at=40001;assert(request("/api/image",ART_FRAME_PACKET_BYTES)==ESP_FAIL);
    assert(aborts==1&&!commits&&!leased&&offset==disconnect_at);
    reset();timeouts=true;assert(request("/api/image",ART_FRAME_PACKET_BYTES)==ESP_FAIL);
    assert(aborts==1&&!commits&&!leased&&closed&&!strcmp(status,"408 Request Timeout"));
    reset();lease_error=ESP_ERR_TIMEOUT;assert(request("/api/image",ART_FRAME_PACKET_BYTES)==ESP_FAIL);
    assert(!commits&&!aborts&&!offset&&!strcmp(status,"409 Conflict"));
    reset();lease_error=ESP_ERR_INVALID_STATE;assert(request("/api/image",ART_FRAME_PACKET_BYTES)==ESP_FAIL);
    assert(!strcmp(status,"503 Service Unavailable")&&!offset);
    reset();missing_header=true;assert(request("/api/image",ART_FRAME_PACKET_BYTES)==ESP_FAIL);
    assert(!strcmp(status,"415 Unsupported Media Type")&&!offset);
    reset();force_save_error=true;assert(request("/api/image",ART_FRAME_PACKET_BYTES)==ESP_FAIL);
    assert(!leased&&!snapshot.has_frame&&!strcmp(status,"500 Internal Server Error"));
    reset();snapshot.display=ART_DISPLAY_QUEUED;
    assert(request("/api/status",0)==ESP_OK&&strstr(response,"\"refresh_wait_seconds\":0")&&strstr(response,"\"display_state\":\"queued\""));
    puts("PASS: actual HTTP handlers: fragmented upload, exact size, CRC/header, disconnect, deadline, busy/storage errors, save acknowledgement and status.");
    return 0;
}
