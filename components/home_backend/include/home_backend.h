#pragma once
#include "esp_err.h"
#include "home_live.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*home_publish_fn)(const home_live_data_t *data);
esp_err_t home_backend_start(home_publish_fn publish);
/* Queue only: no HTTPS or NVS work in caller/touch context. */
bool home_backend_submit(const home_command_t *command);
#ifdef __cplusplus
}
#endif
