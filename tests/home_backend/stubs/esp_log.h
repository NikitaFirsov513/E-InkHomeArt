#pragma once
inline void mockLog(const char *,const char *,...) {}
#define ESP_LOGE(...) mockLog(__VA_ARGS__)
#define ESP_LOGW(...) mockLog(__VA_ARGS__)
#define ESP_LOGI(...) mockLog(__VA_ARGS__)
#define ESP_LOGD(...) mockLog(__VA_ARGS__)
