#pragma once

#include "driver/i2c_master.h"
#include "lis2dw12_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lis2dw12_device *lis2dw12_handle_t;
typedef struct {
    i2c_master_bus_handle_t bus; /* Borrowed; shared with touch. */
    uint8_t address;            /* 7-bit 0x18 / 0x19. Board uses 0x19. */
    uint32_t clock_speed_hz;
    int timeout_ms;
} lis2dw12_idf_config_t;

typedef struct {
    int16_t raw[3];
    float mg[3];
} lis2dw12_sample_t;

/* Adds an I2C device and verifies WHO_AM_I; does not start measurements. */
esp_err_t lis2dw12_idf_new(const lis2dw12_idf_config_t *config, lis2dw12_handle_t *out);
esp_err_t lis2dw12_idf_delete(lis2dw12_handle_t device);
/* Reset + BDU + auto-increment + high-performance mode, given range and ODR.
 * Task context only; serialize all operations on a device. */
esp_err_t lis2dw12_idf_start(lis2dw12_handle_t device, lis2dw12_fs_t range, lis2dw12_odr_t rate);
esp_err_t lis2dw12_idf_power_down(lis2dw12_handle_t device);
/* ESP_ERR_NOT_FINISHED when a new sample is not available. */
esp_err_t lis2dw12_idf_read(lis2dw12_handle_t device, lis2dw12_sample_t *sample);
/* Borrowed ST context: full register API for wake-up, orientation, INT1, FIFO.
 * Do not change power mode/range concurrently with lis2dw12_idf_read(). */
const stmdev_ctx_t *lis2dw12_idf_context(lis2dw12_handle_t device);

#ifdef __cplusplus
}
#endif
