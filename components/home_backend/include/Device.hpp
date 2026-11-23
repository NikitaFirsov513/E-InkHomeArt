#pragma once
#include <string>
#include <vector>
#include "DeviceOnOff.hpp"
#include "DeviceRange.hpp"
#include "DeviceMode.hpp"
#include "DeviceColorSetting.hpp"

enum class ControlKind { Toggle, Range, Mode, Rgb, Hsv, Command, Unsupported };
enum class DeviceSource { Yandex, HomeAssistant };
inline const char *sourceName(DeviceSource source) { return source == DeviceSource::Yandex ? "Яндекс" : "Home Assistant"; }

struct ControlOption {
    std::string value;
};

struct DeviceControl {
    std::string type;
    std::string instance;
    std::string unit;
    ControlKind kind = ControlKind::Toggle;
    bool known = false;
    bool boolean = false;
    bool relative_only = false;
    bool retrievable = true, split = false;
    bool has_minimum = true, has_maximum = true;
    double number = 0;
    double saturation = 100, value = 100;
    double minimum = 0;
    double maximum = 100;
    double step = 1;
    std::string text;
    std::vector<ControlOption> options;
};

struct DeviceReading {
    std::string type,instance,unit,text;
    bool known=false;
};

class Device
{
public:
    std::string id;
    std::string name;
    std::string type;
    std::string room;
    DeviceSource source = DeviceSource::Yandex;
    std::string native_id;
    bool available = true;
    std::vector<DeviceControl> controls;
    std::vector<std::string> readings;
    std::vector<DeviceReading> properties;

    DeviceOnOff Power;
    DeviceColorSetting ColorSetting;

    std::vector<DeviceMode> Modes;
    std::vector<DeviceRange> Ranges;

public:
    Device() = default;
    ~Device() = default;
};

// Keep existing Yandex placements compatible with plans already saved on the panel.
inline std::string devicePlanKey(const Device &device) {
    return device.source == DeviceSource::Yandex ? "yandex:" + device.id : device.id;
}
