#include "DeviceAction.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace {
bool equals(const cJSON *object, const char *key, const char *value)
{
    const auto *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring && !strcmp(item->valuestring, value);
}
std::string stringField(const cJSON *object, const char *key)
{
    const auto *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}
}

esp_err_t buildDeviceAction(const Device &device, const DeviceAction &action, std::string &json)
{
    json.clear();
    if (device.source != DeviceSource::Yandex || !device.available) return ESP_ERR_NOT_SUPPORTED;
    if (!memchr(action.device_id, 0, sizeof(action.device_id)) ||
        !memchr(action.type, 0, sizeof(action.type)) ||
        !memchr(action.instance, 0, sizeof(action.instance)) ||
        !memchr(action.text, 0, sizeof(action.text)) || device.id != action.device_id)
        return ESP_ERR_INVALID_ARG;
    const auto it = std::find_if(device.controls.begin(), device.controls.end(),
        [&](const DeviceControl &c) { return c.type == action.type && c.instance == action.instance; });
    if (it == device.controls.end()) return ESP_ERR_NOT_SUPPORTED;
    const auto &c = *it;
    cJSON *value = nullptr;
    if (c.kind == ControlKind::Toggle && action.kind == ActionValue::Boolean && !action.relative) {
        value = cJSON_CreateBool(action.boolean);
    } else if (c.kind == ControlKind::Mode && action.kind == ActionValue::String && !action.relative) {
        if (std::none_of(c.options.begin(), c.options.end(), [&](const ControlOption &o) {
            return o.value == action.text;
        })) return ESP_ERR_INVALID_ARG;
        value = cJSON_CreateString(action.text);
    } else if ((c.kind == ControlKind::Range || c.kind == ControlKind::Rgb) &&
               action.kind == ActionValue::Number && std::isfinite(action.number)) {
        if (action.relative != c.relative_only) return ESP_ERR_INVALID_ARG;
        if (!action.relative && (action.number < c.minimum || action.number > c.maximum))
            return ESP_ERR_INVALID_ARG;
        if (action.relative && (action.number == 0 || std::abs(action.number) > c.maximum - c.minimum))
            return ESP_ERR_INVALID_ARG;
        value = cJSON_CreateNumber(action.number);
    } else if (c.kind == ControlKind::Hsv && action.kind == ActionValue::Hsv &&
               !action.relative && std::isfinite(action.number) && action.number >= 0 && action.number <= 360 &&
               std::isfinite(action.saturation) && action.saturation>=0 && action.saturation<=100 &&
               std::isfinite(action.value) && action.value>=0 && action.value<=100) {
        value = cJSON_CreateObject();
        if (value && (!cJSON_AddNumberToObject(value, "h", action.number) ||
                      !cJSON_AddNumberToObject(value, "s", action.saturation) ||
                      !cJSON_AddNumberToObject(value, "v", action.value))) {
            cJSON_Delete(value);
            return ESP_ERR_NO_MEM;
        }
    } else return ESP_ERR_INVALID_ARG;
    if (!value) return ESP_ERR_NO_MEM;
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_CreateObject(), &cJSON_Delete);
    auto *devices = root ? cJSON_AddArrayToObject(root.get(), "devices") : nullptr;
    auto *dev = devices ? cJSON_CreateObject() : nullptr;
    if (dev) cJSON_AddItemToArray(devices, dev);
    auto *actions = dev ? cJSON_AddArrayToObject(dev, "actions") : nullptr;
    auto *command = actions ? cJSON_CreateObject() : nullptr;
    if (command) cJSON_AddItemToArray(actions, command);
    auto *state = command ? cJSON_AddObjectToObject(command, "state") : nullptr;
    if (!state) { cJSON_Delete(value); return ESP_ERR_NO_MEM; }
    if (!cJSON_AddItemToObject(state, "value", value)) {
        cJSON_Delete(value);
        return ESP_ERR_NO_MEM;
    }
    if (!cJSON_AddStringToObject(dev, "id", action.device_id) ||
        !cJSON_AddStringToObject(command, "type", action.type) ||
        !cJSON_AddStringToObject(state, "instance", action.instance) ||
        (action.relative && !cJSON_AddBoolToObject(state, "relative", true))) return ESP_ERR_NO_MEM;
    char *encoded = cJSON_PrintUnformatted(root.get());
    if (!encoded) return ESP_ERR_NO_MEM;
    json = encoded;
    cJSON_free(encoded);
    return ESP_OK;
}

bool deviceActionSucceeded(const cJSON *root, const DeviceAction &action, std::string &message)
{
    message = "Некорректный ответ Яндекса";
    if (!equals(root, "status", "ok")) {
        const auto detail = stringField(root, "message");
        if (!detail.empty()) message = detail;
        return false;
    }
    const auto *devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
    const cJSON *dev = nullptr;
    cJSON_ArrayForEach(dev, devices) {
        if (!equals(dev, "id", action.device_id)) continue;
        const cJSON *cap = nullptr;
        cJSON_ArrayForEach(cap, cJSON_GetObjectItemCaseSensitive(dev, "capabilities")) {
            const auto *state = cJSON_GetObjectItemCaseSensitive(cap, "state");
            if (!equals(cap, "type", action.type) || !equals(state, "instance", action.instance)) continue;
            const auto *result = cJSON_GetObjectItemCaseSensitive(state, "action_result");
            if (equals(result, "status", "DONE")) { message = "Команда выполнена"; return true; }
            const auto code = stringField(result, "error_code");
            const auto detail = stringField(result, "error_message");
            message = detail.empty() ? (code.empty() ? "Устройство отклонило команду" : code) : detail;
            return false;
        }
    }
    return false;
}
