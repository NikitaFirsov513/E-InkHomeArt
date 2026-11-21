#pragma once
#include "esp_err.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { bool configured,connected,time_ready; char ip[16],ap_ssid[33]; unsigned disconnect_reason; } home_network_status_t;
esp_err_t home_network_start(void);
void home_network_status(home_network_status_t *out);
#ifdef __cplusplus
}
#endif
