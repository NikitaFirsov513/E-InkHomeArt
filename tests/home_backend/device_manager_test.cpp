#include "DeviceManager.hpp"
#include "DeviceAction.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <type_traits>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAILED at line %d: %s\n", __LINE__, #condition); \
    std::exit(1); } } while (false)

static esp_err_t load(DeviceManager &manager, const char *json)
{
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(json), &cJSON_Delete);
    return manager.createDevicesFromJson(root.get());
}

int main()
{
    {
        DeviceManager manager;
        CHECK(load(manager, R"({"rooms":[{"id":"r","name":"Kitchen"}],"devices":[{"id":"lamp","name":"Light","type":"devices.types.light","room":"r","capabilities":[
          {"type":"devices.capabilities.on_off","state":{"instance":"on","value":false}},
          {"type":"devices.capabilities.range","parameters":{"instance":"brightness","range":{"min":1,"max":100,"precision":1}},"state":{"instance":"brightness","value":45}},
          {"type":"devices.capabilities.color_setting","parameters":{"color_model":"rgb","temperature_k":{"min":2700,"max":6500},"color_scene":{"scenes":[{"id":"reading"}]}},"state":{"instance":"temperature_k","value":3200}},
          {"type":"devices.capabilities.mode","parameters":{"instance":"work_speed","modes":[{"value":"quiet"},{"value":"turbo"}]},"state":{"instance":"work_speed","value":"quiet"}},
          {"type":"devices.capabilities.toggle","parameters":{"instance":"pause"},"state":null},
          {"type":"devices.capabilities.range","parameters":{"instance":"volume","random_access":false,"range":{"min":0,"max":100,"precision":0.5}}},
          {"type":"devices.capabilities.range","parameters":{"instance":"invalid","range":{"min":0,"max":100,"precision":0}}}
        ],"properties":[{"state":{"instance":"battery_level","value":80}}]}]})") == ESP_OK);
        const auto device = manager.getDeviceById("lamp");
        CHECK(device && device->room == "Kitchen" && device->controls.size() == 8);
        CHECK(device->controls[0].known && !device->controls[0].boolean);
        CHECK(device->controls[1].number == 45 && device->controls[1].maximum == 100);
        CHECK(device->controls[3].instance == "temperature_k" && device->controls[3].number == 3200);
        CHECK(!device->controls[6].known && device->controls[7].relative_only);
        CHECK(device->readings.size() == 1);
        DeviceAction action{};
        strcpy(action.device_id, "lamp");
        strcpy(action.type, "devices.capabilities.on_off");
        strcpy(action.instance, "on");
        action.boolean = true;
        std::string body;
        CHECK(buildDeviceAction(*device, action, body) == ESP_OK);
        std::unique_ptr<cJSON, decltype(&cJSON_Delete)> json(cJSON_Parse(body.c_str()), &cJSON_Delete);
        auto *dev = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json.get(), "devices"), 0);
        auto *cap = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(dev, "actions"), 0);
        auto *state = cJSON_GetObjectItemCaseSensitive(cap, "state");
        CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(state, "value")));
        CHECK(!cJSON_GetObjectItemCaseSensitive(state, "relative"));
        auto result = [&](const char *response) {
            std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(response), &cJSON_Delete);
            std::string message;
            return deviceActionSucceeded(root.get(), action, message);
        };
        CHECK(result(R"({"status":"ok","devices":[{"id":"lamp","capabilities":[{"type":"devices.capabilities.on_off","state":{"instance":"on","action_result":{"status":"DONE"}}}]}]})"));
        CHECK(!result(R"({"status":"ok","devices":[{"id":"lamp","capabilities":[{"type":"devices.capabilities.on_off","state":{"instance":"on","action_result":{"status":"ERROR","error_code":"DEVICE_UNREACHABLE"}}}]}]})"));
        CHECK(!result(R"({"status":"ok","devices":[{"id":"other","capabilities":[{"type":"devices.capabilities.on_off","state":{"instance":"on","action_result":{"status":"DONE"}}}]}]})"));
        CHECK(!result(R"({"status":"ok","devices":[]})"));
        CHECK(!result("null"));
        strcpy(action.type, "devices.capabilities.range");
        strcpy(action.instance, "brightness");
        action.kind = ActionValue::Number;
        action.number = 101;
        CHECK(buildDeviceAction(*device, action, body) == ESP_ERR_INVALID_ARG);
        action.number = 75;
        CHECK(buildDeviceAction(*device, action, body) == ESP_OK);
        action.relative = true;
        CHECK(buildDeviceAction(*device, action, body) == ESP_ERR_INVALID_ARG);
        strcpy(action.instance, "volume"); action.number = -0.5;
        CHECK(buildDeviceAction(*device, action, body) == ESP_OK);
        CHECK(body.find("\"relative\":true") != std::string::npos);
        action.relative = false;
        CHECK(buildDeviceAction(*device, action, body) == ESP_ERR_INVALID_ARG);
        strcpy(action.type, "devices.capabilities.mode");
        strcpy(action.instance, "work_speed"); action.kind = ActionValue::String;
        strcpy(action.text, "turbo");
        CHECK(buildDeviceAction(*device, action, body) == ESP_OK);
        strcpy(action.text, "nonexistent");
        CHECK(buildDeviceAction(*device, action, body) == ESP_ERR_INVALID_ARG);
        strcpy(action.instance, "unsupported");
        CHECK(buildDeviceAction(*device, action, body) == ESP_ERR_NOT_SUPPORTED);
    }
    {
        DeviceManager manager;
        CHECK(load(manager,R"({"devices":[{"id":"hsv","name":"Color","type":"light","capabilities":[{"type":"devices.capabilities.color_setting","parameters":{"color_model":"hsv"},"state":{"instance":"hsv","value":{"h":210,"s":35,"v":70}}}]}]})")==ESP_OK);
        const auto device=manager.getDeviceById("hsv");CHECK(device && device->controls.size()==1);
        const auto &c=device->controls[0];CHECK(c.known && c.number==210 && c.saturation==35 && c.value==70);
        DeviceAction action{};strcpy(action.device_id,"hsv");strcpy(action.type,c.type.c_str());strcpy(action.instance,"hsv");
        action.kind=ActionValue::Hsv;action.number=180;action.saturation=40;action.value=65;
        std::string body;CHECK(buildDeviceAction(*device,action,body)==ESP_OK);
        auto *root=cJSON_Parse(body.c_str());CHECK(root);
        auto *entry=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root,"devices"),0);
        auto *cap=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(entry,"actions"),0);
        auto *value=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cap,"state"),"value");
        CHECK(cJSON_GetObjectItemCaseSensitive(value,"h")->valuedouble==180);
        CHECK(cJSON_GetObjectItemCaseSensitive(value,"s")->valuedouble==40);
        CHECK(cJSON_GetObjectItemCaseSensitive(value,"v")->valuedouble==65);cJSON_Delete(root);
        action.saturation=101;CHECK(buildDeviceAction(*device,action,body)==ESP_ERR_INVALID_ARG);
        action.saturation=40;action.value=-1;CHECK(buildDeviceAction(*device,action,body)==ESP_ERR_INVALID_ARG);
    }
    static_assert(!std::is_copy_constructible<DeviceManager>::value);
    {
        DeviceManager manager;
        CHECK(load(manager, R"({"devices":[{"id":"a","name":"Lamp","type":"light"}]})") == ESP_OK);
        const char *invalid[] = {
            "{", "null", "[]", "{}", R"({"devices":{}})", R"({"devices":[null]})",
            R"({"devices":[{"id":"","name":"Lamp","type":"light"}]})",
            R"({"devices":[{"id":"b","name":1,"type":"light"}]})",
            R"({"devices":[{"id":"b","name":"Lamp"}]})",
            R"({"devices":[{"id":"b","name":"New","type":"light"},false]})",
            R"({"devices":[{"id":"b","name":"One","type":"light"},{"id":"b","name":"Two","type":"light"}]})"
        };
        for (const char *json : invalid) {
            CHECK(load(manager, json) == ESP_ERR_INVALID_ARG);
            auto device = manager.getDeviceById("a");
            CHECK(device && device->name == "Lamp");
            CHECK(manager.getDeviceIds().size() == 1);
        }
        std::vector<Device> snapshot;
        CHECK(manager.getDevices(snapshot) == ESP_OK && snapshot.size() == 1);
        force_mutex_timeout = true;
        CHECK(load(manager, R"({"devices":[]})") == ESP_ERR_TIMEOUT);
        CHECK(manager.getDevices(snapshot) == ESP_ERR_TIMEOUT);
        CHECK(snapshot.size() == 1);
        force_mutex_timeout = false;
        CHECK(manager.getDeviceById("a").has_value());
        CHECK(load(manager, R"({"devices":[{"id":"b","name":"Updated","type":"socket"}]})") == ESP_OK);
        CHECK(!manager.getDeviceById("a").has_value());
        CHECK(manager.getDeviceById("b")->name == "Updated");
        CHECK(snapshot[0].id == "a");
        CHECK(load(manager, R"({"devices":[]})") == ESP_OK);
        CHECK(manager.getDeviceIds().empty());
    }
    CHECK(live_mutexes == 0);
    fail_mutex_allocation = true;
    {
        DeviceManager manager;
        CHECK(load(manager, R"({"devices":[]})") == ESP_ERR_NO_MEM);
        std::vector<Device> snapshot;
        CHECK(manager.getDevices(snapshot) == ESP_ERR_NO_MEM);
        CHECK(!manager.getDeviceById("a"));
    }
    CHECK(live_mutexes == 0);
    std::puts("DeviceManager regression tests passed");
}
