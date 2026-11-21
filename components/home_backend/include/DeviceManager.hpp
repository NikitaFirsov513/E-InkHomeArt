#pragma once
#include "Device.hpp"
#include "cJSON.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <optional>

class DeviceManager
{

private:
    std::vector<Device> home_devices;
    SemaphoreHandle_t mutex_home_devices = nullptr;

public:
    // Добавить методы для управления устройствами, например, добавление, удаление, поиск и т.д.
    DeviceManager()
    {
        mutex_home_devices = xSemaphoreCreateMutex();
    }
    std::vector<std::string> getDeviceIds();
    ~DeviceManager();
    DeviceManager(const DeviceManager &) = delete;
    DeviceManager &operator=(const DeviceManager &) = delete;
    esp_err_t getDevices(std::vector<Device> &devices);
    esp_err_t createDevicesFromJson(cJSON *root);
    esp_err_t clearDevices();
    esp_err_t replaceSource(DeviceSource source, std::vector<Device> devices);
    esp_err_t clearSource(DeviceSource source) { return replaceSource(source, {}); }
    std::optional<Device> getDeviceById(const std::string &id);
};
