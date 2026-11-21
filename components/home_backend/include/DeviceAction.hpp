#pragma once
#include "Device.hpp"
#include "cJSON.h"
#include "esp_err.h"
#include <type_traits>

enum class ActionValue { Boolean, Number, String, Hsv, Refresh, Command };

// FreeRTOS queues copy bytes: never put std::string or owning pointers here.
struct DeviceAction {
    char device_id[128]{};
    char type[64]{};
    char instance[64]{};
    char text[128]{};
    ActionValue kind = ActionValue::Boolean;
    double number = 0;
    double saturation = 100, value = 100;
    bool boolean = false;
    bool relative = false;
};
static_assert(std::is_trivially_copyable<DeviceAction>::value);

esp_err_t buildDeviceAction(const Device &device, const DeviceAction &action,
                           std::string &json);
bool deviceActionSucceeded(const cJSON *root, const DeviceAction &action,
                           std::string &message);
