#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GDEY075T7-T01, UC8179, model-specific OTP full / partial / gray4 modes. */
#define UC8179_WIDTH       800
#define UC8179_HEIGHT      480
#define UC8179_FRAME_BYTES ((UC8179_WIDTH / 8) * UC8179_HEIGHT)
#define UC8179_GRAY4_FRAME_BYTES ((UC8179_WIDTH / 4) * UC8179_HEIGHT)
#define UC8179_BUSY_LEVEL  0

typedef struct uc8179_device *uc8179_handle_t;
typedef struct {
    uint32_t bytes;
    int64_t power_on_us, transfer_us, refresh_us, power_off_us;
} uc8179_update_stats_t;
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
/* Explicit reset + model-specific OTP four-gray setup (TSFIX=2, TSSET=0x5f).
 * Full refresh only. Switching back to monochrome requires uc8179_init(). */
esp_err_t uc8179_init_gray4(uc8179_handle_t device);
/* Exactly 96000 bytes, row-major, four pixels/byte, MSB pair first:
 * 0=black, 1=dark gray, 2=light gray, 3=white (ArtFrame pixel payload).
 * Converts to two 48000-byte controller planes in the small DMA buffer.
 * Call init_gray4 before display; then sleep/power_off as for monochrome.
 * Does not accept the 64-byte ArtFrame header. No rotation/inversion. */
esp_err_t uc8179_display_gray4(uc8179_handle_t device, const uint8_t *pixels, size_t length);
esp_err_t uc8179_wait_idle(uc8179_handle_t device);
/* Native panel RAM bytes, 1 bpp, MSB first, row-major, exactly 48000 bytes.
 * Current monochrome CDI DDX=01 uses 1=white, 0=black.
 * No rotation or pixel inversion is applied. Buffer may reside in PSRAM.
 * Polling/task context only; serialize all operations on the device.
 * On transfer/refresh failure call init again before attempting another frame. */
esp_err_t uc8179_display_frame(uc8179_handle_t device, const uint8_t *frame, size_t length);
esp_err_t uc8179_fill(uc8179_handle_t device, uint8_t byte_pattern);
/* Short interactive bench mode, GDEY075T7 OTP partial waveform. After a
 * successful FULL frame, pass that same frame here to seed OLD RAM. No reset
 * or deep sleep may occur between prepare and subsequent partial updates. */
esp_err_t uc8179_prepare_partial(uc8179_handle_t device, const uint8_t *base, size_t length);
esp_err_t uc8179_display_partial(uc8179_handle_t device, const uint8_t *frame, size_t length);
/* Source is a FULL 800x480 frame (100-byte stride). The region expands to byte
 * boundaries and at least 16 columns / 2 rows for strict R90 start<end banks.
 * Unchanged pixels in the expanded region must contain the current image. */
esp_err_t uc8179_display_partial_region(uc8179_handle_t device, const uint8_t *frame, size_t length,
                                       uint16_t x, uint16_t y, uint16_t width, uint16_t height);
esp_err_t uc8179_get_update_stats(uc8179_handle_t device, uc8179_update_stats_t *out);
/* POF (0x02): disable panel high voltages, retain controller RAM/registers.
 * Partial display powers on again without reset. Sleep invalidates the base. */
esp_err_t uc8179_power_off(uc8179_handle_t device);
esp_err_t uc8179_sleep(uc8179_handle_t device);

#ifdef __cplusplus
}
#endif
