#pragma once
#include <string>
#include <vector>

struct ModeElement_t
{
    std::string value;
    std::string name;
};

class DeviceMode
{
public:

    std::vector<ModeElement_t> modes;
    std::string instance;
    uint8_t current_mode_index = 0;
    
    DeviceMode() = default;
    ~DeviceMode() = default;
};