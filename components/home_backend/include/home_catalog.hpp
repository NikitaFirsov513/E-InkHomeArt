#pragma once
#include "home_live.h"
#include "DeviceManager.hpp"
#include "DeviceAction.hpp"
class HomeCatalog {
    std::vector<Device> devices_;
    std::vector<std::string> rooms_,scenes_,signature_;
public:
    home_live_data_t data{};
    bool load(cJSON *root);
    void clear();
    bool command(const home_command_t &cmd,std::string &path,std::string &body,DeviceAction &action) const;
};
void homeText(char *out,size_t capacity,const std::string &value);
