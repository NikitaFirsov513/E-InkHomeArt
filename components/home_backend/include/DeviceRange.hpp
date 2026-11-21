#pragma once
#include <string>
#include <vector>
#include <stdint.h>

class DeviceRange
{

public:
    std::string instance;
    std::string unit;
    bool random_access = true;

    int min = 0;
    int max = 0;
    int step = 1;
    int current = 0;

public:
    DeviceRange() = default;
    ~DeviceRange() = default;
};