#include "art_http.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "art_manager.h"
#include "esp_http_server.h"
#include "esp_timer.h"

static httpd_handle_t server;
#define ASSET(name) \
    extern const uint8_t name##_start[] asm("_binary_" #name "_start"); \
    extern const uint8_t name##_end[] asm("_binary_" #name "_end")
ASSET(art_index);
ASSET(art_style);
ASSET(art_codec);
ASSET(art_client);
ASSET(art_upload_client);
typedef struct { const uint8_t *start, *end; const char *type; } asset_t;
static const asset_t assets[] = {
    {art_index_start,art_index_end,"text/html; charset=utf-8"},
    {art_style_start,art_style_end,"text/css; charset=utf-8"},
    {art_codec_start,art_codec_end,"application/javascript; charset=utf-8"},
    {art_client_start,art_client_end,"application/javascript; charset=utf-8"},
    {art_upload_client_start,art_upload_client_end,"application/javascript; charset=utf-8"},
};

static void response_headers(httpd_req_t *req, const char *type)
{
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
}

static esp_err_t asset_handler(httpd_req_t *req)
{
    const asset_t *asset = req->user_ctx;
    response_headers(req, asset->type);
    httpd_resp_set_hdr(req,"Content-Security-Policy", "default-src 'self'; img-src 'self' blob: data:; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'");
    return httpd_resp_send(req, (const char *)asset->start, (size_t)((uintptr_t)asset->end-(uintptr_t)asset->start));
}

static esp_err_t status_handler(httpd_req_t *req)
{
    art_manager_status_t state;
    art_manager_status(&state);
    static const char *names[] = {"empty","queued","displaying","displayed","error"};
    char json[640];
    snprintf(json, sizeof(json),
        "{\"storage_ready\":%s,\"has_frame\":%s,\"uploading\":%s,"
        "\"saved_generation\":%" PRIu32 ",\"displayed_generation\":%" PRIu32 ","
        "\"pixel_crc\":%" PRIu32 ",\"display_state\":\"%s\",\"display_error\":\"%s\","
        "\"store_error\":%d,\"refresh_wait_seconds\":0}",
        state.storage_ready?"true":"false",state.has_frame?"true":"false",state.uploading?"true":"false",
        state.saved_generation,state.displayed_generation,state.pixel_crc,names[state.display],
        esp_err_to_name(state.display_error),(int)state.store_error);
    response_headers(req, "application/json");
    return httpd_resp_sendstr(req,json);
}

/* Rejected POST bodies must not be parsed as another keep-alive request. */
static esp_err_t reject(httpd_req_t *req, const char *status, const char *message)
{
    response_headers(req, "application/json");
    httpd_resp_set_status(req,status);
    httpd_resp_set_hdr(req,"Connection","close");
    httpd_resp_sendstr(req,message);
    return ESP_FAIL;
}

static esp_err_t upload_handler(httpd_req_t *req)
{
    char type[48], upload_header[8];
    /* A custom header and no CORS allow-list prevent cross-site form uploads. */
    if (httpd_req_get_hdr_value_str(req,"X-Art-Upload",upload_header,sizeof(upload_header)) != ESP_OK ||
        strcmp(upload_header,"1") ||
        httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type)) != ESP_OK ||
        strcmp(type,"application/octet-stream"))
        return reject(req,"415 Unsupported Media Type","{\"error\":\"Use the device photo page\"}");
    if (req->content_len != ART_FRAME_PACKET_BYTES)
        return reject(req,"400 Bad Request","{\"error\":\"Expected a 96064-byte prepared frame\"}");
    uint8_t *packet;
    esp_err_t err = art_manager_begin_upload(&packet);
    if (err != ESP_OK)
        return reject(req, err == ESP_ERR_TIMEOUT ? "409 Conflict" : "503 Service Unavailable",
                      err == ESP_ERR_TIMEOUT ? "{\"error\":\"Panel is busy; retry after refresh\"}" :
                                              "{\"error\":\"Art storage is unavailable; check UART\"}");
    size_t received = 0;
    bool header_checked = false;
    int64_t deadline = esp_timer_get_time()+INT64_C(30000000);
    while (received < ART_FRAME_PACKET_BYTES) {
        if (esp_timer_get_time() >= deadline) {
            art_manager_abort_upload();
            return reject(req,"408 Request Timeout","{\"error\":\"Upload timed out; previous picture retained\"}");
        }
        size_t count = ART_FRAME_PACKET_BYTES-received;
        if (count > 4096) count = 4096;
        int n = httpd_req_recv(req,(char *)packet+received,count);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) { art_manager_abort_upload(); return ESP_FAIL; }
        received += (size_t)n;
        if (!header_checked && received >= ART_FRAME_HEADER_BYTES) {
            uint32_t crc;
            if (art_frame_check_header(packet,ART_FRAME_HEADER_BYTES,&crc) != ART_FRAME_OK) {
                art_manager_abort_upload();
                return reject(req,"400 Bad Request","{\"error\":\"Invalid frame header\"}");
            }
            header_checked = true;
        }
    }
    art_store_result_t result = art_manager_commit_upload(received);
    if (result != ART_STORE_OK)
        return reject(req,result == ART_STORE_INVALID_FRAME ? "400 Bad Request" : "500 Internal Server Error",
                      result == ART_STORE_INVALID_FRAME ? "{\"error\":\"Image CRC failed; previous picture retained\"}" :
                                                         "{\"error\":\"Flash save failed; check UART\"}");
    art_manager_status_t state;
    art_manager_status(&state);
    char json[128];
    snprintf(json,sizeof(json),"{\"saved\":true,\"generation\":%" PRIu32 ",\"pixel_crc\":%" PRIu32 "}",
             state.saved_generation,state.pixel_crc);
    response_headers(req,"application/json");
    httpd_resp_set_status(req,"202 Accepted");
    return httpd_resp_sendstr(req,json);
}

esp_err_t art_http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_uri_handlers = 8;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 5;
    esp_err_t err = httpd_start(&server,&config);
    if (err != ESP_OK) return err;
    static const char *paths[] = {"/","/style.css","/art-image.js","/app.js","/upload.js"};
    for (unsigned i = 0; i < sizeof(assets)/sizeof(assets[0]) && err == ESP_OK; ++i) {
        const httpd_uri_t uri = {.uri=paths[i],.method=HTTP_GET,.handler=asset_handler,.user_ctx=(void *)&assets[i]};
        err = httpd_register_uri_handler(server,&uri);
    }
    const httpd_uri_t status = {.uri="/api/status",.method=HTTP_GET,.handler=status_handler};
    const httpd_uri_t upload = {.uri="/api/image",.method=HTTP_POST,.handler=upload_handler};
    if (err == ESP_OK) err = httpd_register_uri_handler(server,&status);
    if (err == ESP_OK) err = httpd_register_uri_handler(server,&upload);
    if (err != ESP_OK) { httpd_stop(server); server=NULL; }
    return err;
}
