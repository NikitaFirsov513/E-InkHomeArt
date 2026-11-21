#pragma once
#include <stdint.h>
#include <string>
#include <vector>

enum ColorModel
{
    NONE_CM = 0,
    RGB_CM,
    HSV_CM,
};

enum ActiveState
{
    NONE_STATE = 0,
    RGB_STATE,
    HSV_STATE,
    TEMP_K_STATE,
    SCENE_STATE,
};

struct TempK
{
    uint16_t min = 0;
    uint16_t max = 0;
    uint16_t current = 0;
};

struct HSVColor_t
{
    uint8_t h = 0;
    uint8_t s = 0;
    uint8_t v = 0;
};

struct ColorScenes_t
{
    std::vector<std::string> colors;
    uint8_t active = 0;
};

class DeviceColorSetting
{
public:
    ActiveState active_state = ActiveState::NONE_STATE;
    ColorModel color_model = ColorModel::NONE_CM;
    uint32_t rgb_value = 0;
    HSVColor_t hsv_value;
    ColorScenes_t color_scenes;
    TempK temp_k;

public:
    DeviceColorSetting() = default;
    ~DeviceColorSetting() = default;
};