#pragma once
static inline void host_log_ignore(const char *tag,const char *format,...)
{ (void)tag;(void)format; }
#define ESP_LOGI(...) host_log_ignore(__VA_ARGS__)
#define ESP_LOGE(...) host_log_ignore(__VA_ARGS__)
