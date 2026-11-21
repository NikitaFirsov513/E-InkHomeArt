#include "HttpsClient.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAILED at line %d: %s\n", __LINE__, #condition); \
    std::exit(1); } } while (false)

struct MockHttpClient {
    esp_http_client_config_t config;
    std::map<std::string, std::string> headers;
    std::string url;
    std::string body;
    esp_http_client_method_t method = HTTP_METHOD_GET;
};
static MockHttpClient *client;
static int requests = 0;
static int missing_content_type = 0;
static esp_err_t clear_body_error = ESP_OK;
static std::string payload = "{\"devices\":[]}";
static int http_status = 200;
static esp_err_t perform_error = ESP_OK;
static std::string bound_interface;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    client = new MockHttpClient{};
    client->config = *config;
    bound_interface = config->if_name ? config->if_name->ifr_name : "";
    return client;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle) { delete handle; client = nullptr; return ESP_OK; }
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t, int) { return ESP_OK; }
esp_err_t esp_http_client_delete_header(esp_http_client_handle_t handle, const char *key)
{
    return handle->headers.erase(key) ? ESP_OK : ESP_ERR_NOT_FOUND;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t handle, const char *key, const char *value)
{
    if (!value) return esp_http_client_delete_header(handle, key);
    handle->headers[key] = value;
    return ESP_OK;
}
esp_err_t esp_http_client_set_url(esp_http_client_handle_t handle, const char *url) { handle->url = url; return ESP_OK; }
esp_err_t esp_http_client_set_method(esp_http_client_handle_t handle, esp_http_client_method_t method) { handle->method = method; return ESP_OK; }
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t handle, const char *body, int size)
{
    handle->body.assign(body ? body : "", body ? size : 0);
    if (body) return ESP_OK;
    if (clear_body_error != ESP_OK) return clear_body_error;
    // Match ESP-IDF 6.1: the body is cleared before Content-Type is removed.
    esp_err_t error = esp_http_client_set_header(handle, "Content-Type", nullptr);
    if (error == ESP_ERR_NOT_FOUND) ++missing_content_type;
    return error;
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t handle)
{
    ++requests;
    if (perform_error != ESP_OK) return perform_error;
    esp_http_client_event_t event{};
    event.event_id = HTTP_EVENT_ON_DATA;
    event.client = handle;
    event.user_data = handle->config.user_data;
    event.data = payload.data();
    event.data_len = static_cast<int>(payload.size());
    return handle->config.event_handler(&event);
}
int esp_http_client_get_status_code(esp_http_client_handle_t) { return http_status; }
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t) { return ESP_OK; }

int main()
{
    {
        HttpsClient https("https://example.invalid");
        HttpsReq_t request{};
        request.path = "/devices";
        request.token = "unit-test";
        auto response = https.send(request);
        CHECK(client->config.disable_auto_redirect);
        CHECK(missing_content_type == 1);
        CHECK(requests == 1 && response.ok());
        CHECK(response.body_len == payload.size());
        CHECK(std::string(response.body, response.body_len) == payload);
        CHECK(client->headers.at("Authorization") == "Bearer unit-test");

        // Repeated GETs must also reach the transport without Content-Type.
        CHECK(https.send(request).ok());
        CHECK(missing_content_type == 2 && requests == 2);
        request.method = HTTP_METHOD_POST;
        request.body = "{}";
        CHECK(https.send(request).ok());
        CHECK(client->body == "{}" && client->headers.at("Content-Type") == "application/json");
        request.content_type = "application/x-www-form-urlencoded";
        request.body = "grant_type=device_code&code=test";
        request.token = nullptr;
        CHECK(https.send(request).ok());
        CHECK(client->headers.at("Content-Type") == "application/x-www-form-urlencoded" && client->headers.count("Authorization") == 0);
        request.method = HTTP_METHOD_GET;
        request.token = nullptr;
        CHECK(https.send(request).ok());
        CHECK(client->body.empty() && client->headers.count("Content-Type") == 0);
        CHECK(client->headers.count("Authorization") == 0);

        // ESP-IDF may fail automatic challenge handling after receiving HTTP 401.
        // Keep the actual HTTP status so the UI can report the token error.
        http_status = 401;
        perform_error = ESP_ERR_NOT_SUPPORTED;
        auto unauthorized = https.send(request);
        CHECK(unauthorized.http_status == 401);
        CHECK(unauthorized.transport_error == ESP_ERR_NOT_SUPPORTED && !unauthorized.ok());
        CHECK(unauthorized.body == nullptr && unauthorized.body_len == 0);
        http_status = 0;
        perform_error = ESP_ERR_TIMEOUT;
        auto timeout = https.send(request);
        CHECK(timeout.http_status == 0 && timeout.transport_error == ESP_ERR_TIMEOUT);
        http_status = 403;
        perform_error = ESP_OK;
        auto forbidden = https.send(request);
        CHECK(forbidden.http_status == 403 && !forbidden.ok());
        http_status = 200;
        CHECK(https.send(request).ok());

        const int count = requests;
        clear_body_error = ESP_ERR_INVALID_STATE;
        CHECK(https.send(request).transport_error == ESP_ERR_INVALID_STATE);
        CHECK(requests == count);
        clear_body_error = ESP_OK;
        std::string oversized_token(1100, 'x');
        request.token = oversized_token.c_str();
        CHECK(https.send(request).transport_error == ESP_ERR_INVALID_SIZE);
        CHECK(requests == count);
        request.token = nullptr;
        request.path = nullptr;
        CHECK(https.send(request).transport_error == ESP_ERR_INVALID_ARG);
        CHECK(requests == count);
        request.path = "/devices";
        payload.assign(HTTPS_RESPONSE_BUFFER_SIZE, 'x');
        CHECK(https.send(request).transport_error == ESP_ERR_INVALID_SIZE);
    }
    CHECK(client == nullptr);
    payload = "[]";
    HttpsClient::setInterfaceResolver([](char *name, size_t capacity) { std::snprintf(name, capacity, "en1"); return true; });
    {
        HttpsClient http("http://192.168.1.20:8123");
        HttpsReq_t req{}; req.path = "/api/states";
        CHECK(http.send(req).ok() && bound_interface == "en1");
    }
    HttpsClient::setInterfaceResolver([](char *name, size_t capacity) { std::snprintf(name, capacity, "st2"); return true; });
    {
        HttpsClient http("https://example.invalid");
        HttpsReq_t req{}; req.path = "/devices";
        CHECK(http.send(req).ok() && bound_interface == "st2");
    }
    HttpsClient::setInterfaceResolver([](char *, size_t) { return false; });
    {
        const auto before = requests; HttpsClient http("https://example.invalid");
        HttpsReq_t req{}; req.path = "/devices";
        CHECK(!http.send(req).ok() && requests == before);
    }
    HttpsClient::setInterfaceResolver(nullptr);
    {
        HttpsClient http("http://homeassistant.local:8123", 512 * 1024);
        HttpsReq_t req{}; req.path = "/api/states"; req.token = "local-test";
        payload.assign(128 * 1024, 'x');
        CHECK(http.send(req).ok());
        CHECK(client->url == "http://homeassistant.local:8123/api/states");
        payload.assign(512 * 1024, 'x');
        CHECK(http.send(req).transport_error == ESP_ERR_INVALID_SIZE);
    }
    std::puts("HTTPS client regression tests passed");
}
