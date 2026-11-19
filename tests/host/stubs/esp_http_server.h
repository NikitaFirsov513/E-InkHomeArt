#pragma once
#include "esp_err.h"
typedef void *httpd_handle_t;
typedef struct { size_t content_len; void *user_ctx; } httpd_req_t;
typedef struct {
    const char *uri;
    int method;
    esp_err_t (*handler)(httpd_req_t *);
    void *user_ctx;
} httpd_uri_t;
typedef struct { unsigned stack_size,max_uri_handlers,max_open_sockets,recv_wait_timeout,send_wait_timeout; bool lru_purge_enable; } httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_SOCK_ERR_TIMEOUT -3
esp_err_t httpd_start(httpd_handle_t *,const httpd_config_t *);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *);
esp_err_t httpd_resp_set_type(httpd_req_t *,const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *,const char *,const char *);
esp_err_t httpd_resp_set_status(httpd_req_t *,const char *);
esp_err_t httpd_resp_send(httpd_req_t *,const char *,int);
esp_err_t httpd_resp_sendstr(httpd_req_t *,const char *);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *,const char *,char *,size_t);
int httpd_req_recv(httpd_req_t *,char *,size_t);
