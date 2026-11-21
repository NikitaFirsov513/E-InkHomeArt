#pragma once
#include "esp_err.h"

struct MockHttpClient;
struct ifreq { char ifr_name[16]{}; };
using esp_http_client_handle_t = MockHttpClient *;
enum esp_http_client_method_t { HTTP_METHOD_GET, HTTP_METHOD_POST, HTTP_METHOD_PUT };
enum esp_http_client_event_id_t {
    HTTP_EVENT_ERROR, HTTP_EVENT_ON_CONNECTED, HTTP_EVENT_HEADER_SENT,
    HTTP_EVENT_ON_HEADER, HTTP_EVENT_ON_DATA, HTTP_EVENT_ON_FINISH,
    HTTP_EVENT_DISCONNECTED, HTTP_EVENT_REDIRECT
};
struct esp_http_client_event_t {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t client = nullptr;
    void *user_data = nullptr;
    void *data = nullptr;
    int data_len = 0;
    const char *header_key = nullptr;
    const char *header_value = nullptr;
};
struct esp_http_client_config_t {
    const char *url = nullptr;
    esp_err_t (*crt_bundle_attach)(void *) = nullptr;
    esp_err_t (*event_handler)(esp_http_client_event_t *) = nullptr;
    void *user_data = nullptr;
    int timeout_ms = 0;
    bool disable_auto_redirect = false;
    struct ifreq *if_name = nullptr;
};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t, int);
esp_err_t esp_http_client_delete_header(esp_http_client_handle_t, const char *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t, const char *);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t, esp_http_client_method_t);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t, const char *, int);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t);
