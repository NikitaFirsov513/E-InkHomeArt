#pragma once

#include "driver/i2c_master.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GT911_ADDRESS_LOW  0x5D
#define GT911_ADDRESS_HIGH 0x14
#define GT911_MAX_POINTS   5

typedef struct gt911_device *gt911_handle_t;
typedef struct {
    i2c_master_bus_handle_t bus; /* Borrowed; caller creates and deletes the bus. */
    gpio_num_t reset_pin;
    gpio_num_t interrupt_pin;
    uint16_t width;
    uint16_t height;
    uint8_t address; /* GT911_ADDRESS_LOW or GT911_ADDRESS_HIGH. */
    uint32_t clock_speed_hz;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
} gt911_config_t;

/* Explicitly resets the chip, selects its address and verifies product ID. */
esp_err_t gt911_new(const gt911_config_t *config, gt911_handle_t *out);
esp_err_t gt911_delete(gt911_handle_t device);
/* Task context only. Serialize operations on each device, including deletion. */
esp_err_t gt911_read(gt911_handle_t device,
                    esp_lcd_touch_point_data_t points[GT911_MAX_POINTS], uint8_t *count);
esp_err_t gt911_sleep(gt911_handle_t device);
esp_err_t gt911_wake(gt911_handle_t device);
/* Borrowed handle for registering an interrupt callback / GUI integration.
 * Use this wrapper's sleep/wake functions to preserve INT input mode/timings.
 * Stop users/callbacks before deleting the device. */
esp_lcd_touch_handle_t gt911_touch_handle(gt911_handle_t device);

#ifdef __cplusplus
}
#endif
