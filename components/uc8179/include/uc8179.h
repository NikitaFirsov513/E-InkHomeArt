#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GDEY075T7-T01, UC8179, monochrome full refresh using the panel's OTP LUT. */
#define UC8179_WIDTH       800
#define UC8179_HEIGHT      480
#define UC8179_FRAME_BYTES ((UC8179_WIDTH / 8) * UC8179_HEIGHT)
#define UC8179_BUSY_LEVEL  0

typedef struct uc8179_device *uc8179_handle_t;
typedef struct {
    spi_host_device_t host; /* Borrowed bus, already initialized by the caller. */
    gpio_num_t cs_pin;
    gpio_num_t dc_pin;
    gpio_num_t reset_pin;
    gpio_num_t busy_pin;
    int clock_speed_hz;
    int busy_timeout_ms; /* Suggested bring-up value: 20000. Must be finite. */
} uc8179_config_t;

/* Attaches SPI device and allocates a small internal DMA buffer, no panel reset. */
esp_err_t uc8179_new(const uc8179_config_t *config, uc8179_handle_t *out);
/* Releases software resources; call sleep first to power down a running panel. */
esp_err_t uc8179_delete(uc8179_handle_t device);
/* Explicit reset + full-refresh setup + power-on. Also wakes from deep sleep. */
esp_err_t uc8179_init(uc8179_handle_t device);
esp_err_t uc8179_wait_idle(uc8179_handle_t device);
/* Native panel RAM bytes, 1 bpp, MSB first, row-major, exactly 48000 bytes.
 * No rotation or pixel inversion is applied. Buffer may reside in PSRAM.
 * Polling/task context only; serialize all operations on the device.
 * On transfer/refresh failure call init again before attempting another frame. */
esp_err_t uc8179_display_frame(uc8179_handle_t device, const uint8_t *frame, size_t length);
esp_err_t uc8179_fill(uc8179_handle_t device, uint8_t byte_pattern);
esp_err_t uc8179_sleep(uc8179_handle_t device);

#ifdef __cplusplus
}
#endif
