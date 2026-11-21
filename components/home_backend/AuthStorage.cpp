#include "AuthStorage.hpp"
#include "nvs.h"
#include "nvs_flash.h"

namespace { constexpr const char *partition = "auth_nvs"; }
bool loadAuthSession(std::string &json) {
    json.clear();
    if (nvs_flash_init_partition(partition) != ESP_OK) return false;
    nvs_handle_t handle;
    const auto opened = nvs_open_from_partition(partition, "yandex", NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
    if (opened != ESP_OK) return false;
    size_t size = 0; auto err = nvs_get_blob(handle, "session_v1", nullptr, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return true; }
    if (err != ESP_OK || size == 0 || size > 4096) { nvs_close(handle); return false; }
    json.resize(size); err = nvs_get_blob(handle, "session_v1", json.data(), &size); nvs_close(handle);
    return err == ESP_OK;
}
bool saveAuthSession(const std::string &json) {
    if (json.size() > 4096 || nvs_flash_init_partition(partition) != ESP_OK) return false;
    nvs_handle_t handle;
    if (nvs_open_from_partition(partition, "yandex", NVS_READWRITE, &handle) != ESP_OK) return false;
    auto err = json.empty() ? nvs_erase_key(handle, "session_v1") : nvs_set_blob(handle, "session_v1", json.data(), json.size());
    if (err == ESP_ERR_NVS_NOT_FOUND && json.empty()) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle); return err == ESP_OK;
}
