#include "DeviceManager.hpp"
#include <algorithm>
#include <utility>
#include <cmath>
#include <cstdio>

namespace {
std::string str(const cJSON *object, const char *key)
{
    const auto *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}

void parseControls(Device &device, const cJSON *item)
{
    const cJSON *cap = nullptr;
    cJSON_ArrayForEach(cap, cJSON_GetObjectItemCaseSensitive(item, "capabilities")) {
        DeviceControl c;
        c.type = str(cap, "type");
        const auto *p = cJSON_GetObjectItemCaseSensitive(cap, "parameters");
        const auto *state = cJSON_GetObjectItemCaseSensitive(cap, "state");
        const auto *value = cJSON_GetObjectItemCaseSensitive(state, "value");
        c.instance = str(p, "instance");
        const auto state_instance = str(state, "instance");
        if (c.type == "devices.capabilities.on_off" || c.type == "devices.capabilities.toggle") {
            if (c.type == "devices.capabilities.on_off") c.instance = "on";
            if (c.instance.empty()) continue;
            c.kind = ControlKind::Toggle;
            c.known = state_instance == c.instance && cJSON_IsBool(value);
            c.boolean = cJSON_IsTrue(value);
            device.controls.push_back(c);
        } else if (c.type == "devices.capabilities.range") {
            c.kind = ControlKind::Range;
            if (c.instance.empty()) continue;
            c.relative_only = cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(p, "random_access"));
            const auto *range = cJSON_GetObjectItemCaseSensitive(p, "range");
            const auto *min = cJSON_GetObjectItemCaseSensitive(range, "min");
            const auto *max = cJSON_GetObjectItemCaseSensitive(range, "max");
            const auto *step = cJSON_GetObjectItemCaseSensitive(range, "precision");
            if (!cJSON_IsNumber(min) || !cJSON_IsNumber(max)) continue;
            c.minimum = min->valuedouble;
            c.maximum = max->valuedouble;
            c.step = cJSON_IsNumber(step) ? step->valuedouble : 1;
            if (!std::isfinite(c.minimum) || !std::isfinite(c.maximum) ||
                !std::isfinite(c.step) || c.step <= 0 || c.maximum <= c.minimum ||
                (c.maximum - c.minimum) / c.step > 10000) continue;
            c.known = state_instance == c.instance && cJSON_IsNumber(value) &&
                      std::isfinite(value->valuedouble) && value->valuedouble >= c.minimum && value->valuedouble <= c.maximum;
            c.number = c.known ? value->valuedouble : c.minimum;
            device.controls.push_back(c);
        } else if (c.type == "devices.capabilities.mode") {
            c.kind = ControlKind::Mode;
            if (c.instance.empty()) continue;
            const cJSON *mode = nullptr;
            cJSON_ArrayForEach(mode, cJSON_GetObjectItemCaseSensitive(p, "modes")) {
                const auto name = str(mode, "value");
                if (!name.empty()) c.options.push_back({name});
            }
            c.text = state_instance == c.instance ? str(state, "value") : "";
            c.known = !c.text.empty();
            if (!c.options.empty()) device.controls.push_back(c);
        } else if (c.type == "devices.capabilities.color_setting") {
            const auto model = str(p, "color_model");
            if (model == "rgb" || model == "hsv") {
                c.kind = model == "rgb" ? ControlKind::Rgb : ControlKind::Hsv;
                c.instance = model;
                c.maximum = 16777215;
                c.known = state_instance == model && (cJSON_IsNumber(value) || cJSON_IsObject(value));
                c.number = cJSON_IsNumber(value) ? value->valuedouble : 0;
                if(model=="hsv" && c.known) {
                    const auto *h=cJSON_GetObjectItemCaseSensitive(value,"h"),*s=cJSON_GetObjectItemCaseSensitive(value,"s"),*v=cJSON_GetObjectItemCaseSensitive(value,"v");
                    c.known=cJSON_IsNumber(h)&&cJSON_IsNumber(s)&&cJSON_IsNumber(v) &&
                        std::isfinite(h->valuedouble)&&h->valuedouble>=0&&h->valuedouble<=360 &&
                        std::isfinite(s->valuedouble)&&s->valuedouble>=0&&s->valuedouble<=100 &&
                        std::isfinite(v->valuedouble)&&v->valuedouble>=0&&v->valuedouble<=100;
                    if(c.known) {c.number=h->valuedouble;c.saturation=s->valuedouble;c.value=v->valuedouble;}
                }
                device.controls.push_back(c);
            }
            const auto *temp = cJSON_GetObjectItemCaseSensitive(p, "temperature_k");
            const auto *min = cJSON_GetObjectItemCaseSensitive(temp, "min");
            const auto *max = cJSON_GetObjectItemCaseSensitive(temp, "max");
            if (cJSON_IsNumber(min) && cJSON_IsNumber(max) && min->valuedouble >= 100 &&
                max->valuedouble <= 20000 && max->valuedouble > min->valuedouble) {
                DeviceControl t;
                t.type = c.type; t.instance = "temperature_k"; t.kind = ControlKind::Range;
                t.minimum = min->valuedouble; t.maximum = max->valuedouble; t.step = 1;
                t.known = state_instance == t.instance && cJSON_IsNumber(value) &&
                          value->valuedouble >= t.minimum && value->valuedouble <= t.maximum;
                t.number = t.known ? value->valuedouble : t.minimum;
                device.controls.push_back(t);
            }
            const auto *scene = cJSON_GetObjectItemCaseSensitive(p, "color_scene");
            DeviceControl s;
            s.type = c.type; s.instance = "scene"; s.kind = ControlKind::Mode;
            const cJSON *entry = nullptr;
            cJSON_ArrayForEach(entry, cJSON_GetObjectItemCaseSensitive(scene, "scenes")) {
                const auto id = str(entry, "id");
                if (!id.empty()) s.options.push_back({id});
            }
            s.text = state_instance == "scene" ? str(state, "value") : "";
            s.known = !s.text.empty();
            if (!s.options.empty()) device.controls.push_back(s);
        }
    }
    const cJSON *prop = nullptr;
    cJSON_ArrayForEach(prop, cJSON_GetObjectItemCaseSensitive(item, "properties")) {
        const auto *state = cJSON_GetObjectItemCaseSensitive(prop, "state");
        const auto *value = cJSON_GetObjectItemCaseSensitive(state, "value");
        const auto instance = str(state, "instance");
        if (instance.empty()) continue;
        if (cJSON_IsNumber(value)) {
            char number[48]; snprintf(number, sizeof(number), "%g", value->valuedouble);
            device.readings.push_back(instance + ": " + number);
        } else if (cJSON_IsString(value)) device.readings.push_back(instance + ": " + value->valuestring);
    }
}

class DeviceLock {
public:
    explicit DeviceLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        locked_ = mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE;
    }
    ~DeviceLock() { if (locked_) xSemaphoreGive(mutex_); }
    explicit operator bool() const { return locked_; }
    DeviceLock(const DeviceLock &) = delete;
    DeviceLock &operator=(const DeviceLock &) = delete;
private:
    SemaphoreHandle_t mutex_;
    bool locked_ = false;
};
}

DeviceManager::~DeviceManager()
{
    if (mutex_home_devices) vSemaphoreDelete(mutex_home_devices);
}

esp_err_t DeviceManager::clearDevices()
{
    if (!mutex_home_devices) return ESP_ERR_NO_MEM;
    DeviceLock lock(mutex_home_devices);
    if (!lock) return ESP_ERR_TIMEOUT;
    home_devices.clear();
    return ESP_OK;
}

esp_err_t DeviceManager::createDevicesFromJson(cJSON *root)
{
    if (!cJSON_IsObject(root)) return ESP_ERR_INVALID_ARG;
    cJSON *devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
    if (!cJSON_IsArray(devices)) return ESP_ERR_INVALID_ARG;
    // Parse outside the lock and commit only a fully validated response.
    std::vector<Device> parsed;
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, devices) {
        if (!cJSON_IsObject(item)) return ESP_ERR_INVALID_ARG;
        auto *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        auto *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        auto *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        if (!cJSON_IsString(id) || !id->valuestring || !id->valuestring[0] ||
            !cJSON_IsString(name) || !name->valuestring ||
            !cJSON_IsString(type) || !type->valuestring)
            return ESP_ERR_INVALID_ARG;
        if (std::any_of(parsed.begin(), parsed.end(),
                        [&](const Device &device) { return device.id == id->valuestring; }))
            return ESP_ERR_INVALID_ARG;
        Device device;
        device.id = id->valuestring;
        device.native_id = device.id;
        device.name = name->valuestring;
        device.type = type->valuestring;
        const auto room_id = str(item, "room");
        const cJSON *room = nullptr;
        cJSON_ArrayForEach(room, cJSON_GetObjectItemCaseSensitive(root, "rooms"))
            if (str(room, "id") == room_id && !room_id.empty()) device.room = str(room, "name");
        parseControls(device, item);
        parsed.push_back(std::move(device));
    }
    return replaceSource(DeviceSource::Yandex, std::move(parsed));
}

esp_err_t DeviceManager::replaceSource(DeviceSource source, std::vector<Device> devices)
{
    for (size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].id.empty() || devices[i].source != source) return ESP_ERR_INVALID_ARG;
        for (size_t j = 0; j < i; ++j) if (devices[j].id == devices[i].id) return ESP_ERR_INVALID_ARG;
    }
    if (!mutex_home_devices) return ESP_ERR_NO_MEM;
    DeviceLock lock(mutex_home_devices);
    if (!lock) return ESP_ERR_TIMEOUT;
    for (const auto &old : home_devices) if (old.source != source)
        for (const auto &device : devices) if (device.id == old.id) return ESP_ERR_INVALID_ARG;
    std::vector<Device> merged;
    for (const auto &old : home_devices) if (old.source != source) merged.push_back(old);
    merged.insert(merged.end(), std::make_move_iterator(devices.begin()), std::make_move_iterator(devices.end()));
    home_devices.swap(merged);
    return ESP_OK;
}

esp_err_t DeviceManager::getDevices(std::vector<Device> &devices)
{
    if (!mutex_home_devices) return ESP_ERR_NO_MEM;
    DeviceLock lock(mutex_home_devices);
    if (!lock) return ESP_ERR_TIMEOUT;
    devices = home_devices;
    return ESP_OK;
}

std::optional<Device> DeviceManager::getDeviceById(const std::string &id)
{
    DeviceLock lock(mutex_home_devices);
    if (!lock) return std::nullopt;
    for (const auto &device : home_devices)
        if (device.id == id) return device;
    return std::nullopt;
}

std::vector<std::string> DeviceManager::getDeviceIds()
{
    std::vector<std::string> ids;
    DeviceLock lock(mutex_home_devices);
    if (!lock) return ids;
    for (const auto &device : home_devices) ids.push_back(device.id);
    return ids;
}
