#pragma once

#include "esp_err.h"

/* Call once from app_main. Shared timer task, one GPIO write per 500 ms. */
esp_err_t heartbeat_start(void);
