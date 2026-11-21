#pragma once

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <string>

#define HTTPS_RESPONSE_BUFFER_SIZE (32 * 1024)

struct HttpsRes_t
{
    esp_err_t transport_error =
        ESP_OK;

    int http_status = 0;

    // Borrowed from HttpsClient; valid until its next send() or destruction.
    const char *body = nullptr;

    size_t body_len = 0;

    bool ok() const
    {
        return transport_error == ESP_OK &&
               http_status >= 200 &&
               http_status < 300;
    }
};

struct HttpsReq_t
{
    esp_http_client_method_t method = HTTP_METHOD_GET;
    const char *path = nullptr;
    const char *token = nullptr;
    const char *body = nullptr;
    int timeout_ms = 10000;
    const char *content_type = "application/json";
};
class HttpsClient
{
private:
    esp_http_client_handle_t client_ = nullptr;

    std::string baseUrl;

    char *response_buffer_ = nullptr;
    
    size_t response_length_ = 0;
    size_t response_capacity_ = HTTPS_RESPONSE_BUFFER_SIZE;

    char auth_buffer_[1024]{};

    bool response_too_large_ = false;

    bool isFail = false;

    esp_err_t set_token(const char *token);

    HttpsRes_t get(const HttpsReq_t &req);
    HttpsRes_t post(const HttpsReq_t &req);
    HttpsRes_t perform();

    static esp_err_t event_handler(
        esp_http_client_event_t *event);

public:
    using InterfaceResolver = bool (*)(char *name, size_t capacity);
    static void setInterfaceResolver(InterfaceResolver resolver) { interface_resolver_ = resolver; }
    HttpsClient(const char *baseUrl, size_t response_capacity = HTTPS_RESPONSE_BUFFER_SIZE);
    ~HttpsClient();
    HttpsClient(const HttpsClient &) = delete;
    HttpsClient &operator=(const HttpsClient &) = delete;

    HttpsRes_t send(const HttpsReq_t &req);
private:
    static InterfaceResolver interface_resolver_;
};
