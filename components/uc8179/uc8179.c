#include "uc8179.h"

#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Register sequence: Good Display T075T7_Arduino_V2.3.5, normal full-refresh
 * mode, from https://www.good-display.com/companyfile/1838.html.
 * Transport, bounds, resource handling and timeouts are implemented for IDF. */
struct uc8179_device {
    uc8179_config_t config;
    spi_device_handle_t spi;
    uint8_t *dma_buffer;
    size_t chunk_size;
    bool initialized;
    bool powered;
    bool image_valid;
    bool partial_ready;
    bool gray4;
    uc8179_update_stats_t stats;
};

static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) + 2);
}

static esp_err_t command(uc8179_handle_t dev, uint8_t value)
{
    esp_err_t err = gpio_set_level(dev->config.dc_pin, 0);
    if (err != ESP_OK) return err;
    spi_transaction_t tx = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {value},
    };
    return spi_device_polling_transmit(dev->spi, &tx);
}

static esp_err_t data(uc8179_handle_t dev, const uint8_t *bytes, size_t length,
                      uint8_t fill)
{
    esp_err_t err = gpio_set_level(dev->config.dc_pin, 1);
    if (err != ESP_OK) return err;
    while (length) {
        size_t count = length < dev->chunk_size ? length : dev->chunk_size;
        if (bytes) memcpy(dev->dma_buffer, bytes, count);
        else memset(dev->dma_buffer, fill, count);
        spi_transaction_t tx = { .length = count * 8, .tx_buffer = dev->dma_buffer };
        err = spi_device_polling_transmit(dev->spi, &tx);
        if (err != ESP_OK) return err;
        if (bytes) bytes += count;
        length -= count;
    }
    return ESP_OK;
}

static esp_err_t setting(uc8179_handle_t dev, uint8_t cmd,
                         const uint8_t *bytes, size_t length)
{
    esp_err_t err = command(dev, cmd);
    return err == ESP_OK ? data(dev, bytes, length, 0) : err;
}

esp_err_t uc8179_new(const uc8179_config_t *config, uc8179_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!config || (config->host != SPI2_HOST && config->host != SPI3_HOST) ||
        config->clock_speed_hz <= 0 || config->clock_speed_hz > 10000000 ||
        config->busy_timeout_ms <= 0 || !GPIO_IS_VALID_OUTPUT_GPIO(config->cs_pin) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->dc_pin) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->reset_pin) ||
        !GPIO_IS_VALID_GPIO(config->busy_pin)) return ESP_ERR_INVALID_ARG;
    const int pins[] = {config->cs_pin, config->dc_pin, config->reset_pin, config->busy_pin};
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = i + 1; j < 4; ++j) {
            if (pins[i] == pins[j]) return ESP_ERR_INVALID_ARG;
        }
    }
    uc8179_handle_t dev = calloc(1, sizeof(*dev));
    if (!dev) return ESP_ERR_NO_MEM;
    dev->config = *config;
    const spi_device_interface_config_t spi = {
        .mode = 0,
        .clock_speed_hz = config->clock_speed_hz,
        .spics_io_num = config->cs_pin,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 1,
    };
    esp_err_t err = spi_bus_add_device(config->host, &spi, &dev->spi);
    if (err != ESP_OK) goto fail;
    /* IDF 6.1 initializes the SPI master context lazily during device attach.
     * Querying capacity before this returns ESP_ERR_INVALID_ARG on a new bus. */
    size_t max_bytes = 0;
    err = spi_bus_get_max_transaction_len(config->host, &max_bytes);
    if (err != ESP_OK) goto fail;
    if (max_bytes < 4) { err = ESP_ERR_INVALID_ARG; goto fail; }
    dev->chunk_size = (max_bytes < 4096 ? max_bytes : 4096) & ~(size_t)3;
    dev->dma_buffer = heap_caps_malloc(dev->chunk_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!dev->dma_buffer) { err = ESP_ERR_NO_MEM; goto fail; }
    *out = dev;
    return ESP_OK;
fail:
    if (dev->spi) spi_bus_remove_device(dev->spi);
    free(dev->dma_buffer);
    free(dev);
    return err;
}

esp_err_t uc8179_delete(uc8179_handle_t dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    esp_err_t err = spi_bus_remove_device(dev->spi);
    if (err != ESP_OK) return err;
    free(dev->dma_buffer);
    free(dev);
    return ESP_OK;
}

esp_err_t uc8179_wait_idle(uc8179_handle_t dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    const int64_t deadline = esp_timer_get_time() + (int64_t)dev->config.busy_timeout_ms * 1000;
    while (gpio_get_level(dev->config.busy_pin) == UC8179_BUSY_LEVEL) {
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(1); /* Poll at the RTOS tick; do not add two extra ticks. */
    }
    return ESP_OK;
}

static esp_err_t reset_panel(uc8179_handle_t dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    dev->initialized = false;
    dev->powered = dev->image_valid = dev->partial_ready = false;
    dev->gray4 = false;
    dev->stats = (uc8179_update_stats_t){0};
    esp_err_t err = gpio_set_level(dev->config.dc_pin, 0);
    if (err == ESP_OK) err = gpio_set_level(dev->config.reset_pin, 1);
    if (err != ESP_OK) return err;
    const gpio_config_t outputs = {
        .pin_bit_mask = (UINT64_C(1) << dev->config.dc_pin) | (UINT64_C(1) << dev->config.reset_pin),
        .mode = GPIO_MODE_OUTPUT,
    };
    const gpio_config_t input = {
        .pin_bit_mask = UINT64_C(1) << dev->config.busy_pin,
        .mode = GPIO_MODE_INPUT,
    };
    err = gpio_config(&outputs);
    if (err == ESP_OK) err = gpio_config(&input);
    if (err == ESP_OK) err = gpio_set_level(dev->config.reset_pin, 0);
    if (err != ESP_OK) return err;
    delay_ms(10);
    err = gpio_set_level(dev->config.reset_pin, 1);
    if (err != ESP_OK) return err;
    delay_ms(110);
    return ESP_OK;
}

esp_err_t uc8179_init(uc8179_handle_t dev)
{
    esp_err_t err = reset_panel(dev);
    if (err != ESP_OK) return err;
    const uint8_t panel = 0x1F;
    err = setting(dev, 0x00, &panel, 1);
    int64_t power_start = esp_timer_get_time();
    if (err == ESP_OK) err = command(dev, 0x04);
    if (err != ESP_OK) return err;
    dev->powered = true;
    esp_rom_delay_us(1000); /* Allow BUSY to assert; then use the ready signal. */
    err = uc8179_wait_idle(dev);
    dev->stats.power_on_us = esp_timer_get_time()-power_start;
    const uint8_t interval[] = {0x21, 0x07};
    if (err == ESP_OK) err = setting(dev, 0x50, interval, sizeof(interval));
    if (err == ESP_OK) dev->initialized = dev->powered = true;
    return err;
}

static esp_err_t display(uc8179_handle_t dev, const uint8_t *frame, uint8_t fill)
{
    if (!dev->initialized || !dev->powered || dev->partial_ready || dev->gray4) return ESP_ERR_INVALID_STATE;
    esp_err_t err = uc8179_wait_idle(dev);
    int64_t transfer_start = esp_timer_get_time();
    if (err == ESP_OK) err = command(dev, 0x10);
    if (err == ESP_OK) err = data(dev, NULL, UC8179_FRAME_BYTES, 0x00);
    if (err == ESP_OK) err = command(dev, 0x13);
    if (err == ESP_OK) err = data(dev, frame, UC8179_FRAME_BYTES, fill);
    dev->stats.transfer_us = esp_timer_get_time()-transfer_start;
    dev->stats.bytes = UC8179_FRAME_BYTES*2;
    int64_t refresh_start = esp_timer_get_time();
    if (err == ESP_OK) err = command(dev, 0x12);
    if (err == ESP_OK) {
        esp_rom_delay_us(300); /* Controller requires >=200 us before BUSY. */
        err = uc8179_wait_idle(dev);
    }
    dev->stats.refresh_us = esp_timer_get_time()-refresh_start;
    dev->image_valid = err == ESP_OK;
    if (err != ESP_OK) dev->initialized = false;
    return err;
}

esp_err_t uc8179_display_frame(uc8179_handle_t dev, const uint8_t *frame, size_t length)
{
    if (!dev || !frame || length != UC8179_FRAME_BYTES) return ESP_ERR_INVALID_ARG;
    return display(dev, frame, 0);
}

esp_err_t uc8179_fill(uc8179_handle_t dev, uint8_t byte_pattern)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    return display(dev, NULL, byte_pattern);
}

esp_err_t uc8179_init_gray4(uc8179_handle_t dev)
{
    /* Good Display ESP32epdx d5ed3b7, GDEY075T7 EPD_Init_4G.
     * This selects the factory OTP waveform; no custom LUT or voltage tuning. */
    esp_err_t err = reset_panel(dev);
    if (err != ESP_OK) return err;
    const uint8_t panel = 0x1f, interval[] = {0x10, 0x07};
    const uint8_t booster[] = {0x27, 0x27, 0x18, 0x17};
    const uint8_t fixed = 0x02, temperature = 0x5f;
    err = setting(dev, 0x00, &panel, 1);
    if (err == ESP_OK) err = setting(dev, 0x50, interval, sizeof(interval));
    int64_t start = esp_timer_get_time();
    if (err == ESP_OK) err = command(dev, 0x04);
    if (err == ESP_OK) {
        dev->powered = true;
        delay_ms(100); /* Match the vendor gray4 power-on settling time. */
        err = uc8179_wait_idle(dev);
    }
    dev->stats.power_on_us = esp_timer_get_time() - start;
    if (err == ESP_OK) err = setting(dev, 0x06, booster, sizeof(booster));
    if (err == ESP_OK) err = setting(dev, 0xe0, &fixed, 1);
    if (err == ESP_OK) err = setting(dev, 0xe5, &temperature, 1);
    if (err == ESP_OK) dev->initialized = dev->gray4 = true;
    return err;
}

static esp_err_t gray4_plane(uc8179_handle_t dev, const uint8_t *pixels, unsigned plane)
{
    esp_err_t err = gpio_set_level(dev->config.dc_pin, 1);
    if (err != ESP_OK) return err;
    /* OLD = inverted low bit, NEW = inverted high bit of each gray code.
     * Exactly 48000 outputs/plane; each consumes two source bytes. The vendor
     * EPD_WhiteScreen_ALL_4G NEW-loop bound is doubled: do not copy that overread. */
    for (size_t offset = 0; offset < UC8179_FRAME_BYTES;) {
        size_t count = UC8179_FRAME_BYTES - offset;
        if (count > dev->chunk_size) count = dev->chunk_size;
        for (size_t i = 0; i < count; ++i) {
            uint16_t packed = ((uint16_t)pixels[2*(offset+i)] << 8) | pixels[2*(offset+i)+1];
            uint8_t output = 0;
            for (int shift = 14; shift >= 0; shift -= 2)
                output = (uint8_t)((output << 1) | (((packed >> (shift+plane)) & 1U) ^ 1U));
            dev->dma_buffer[i] = output;
        }
        spi_transaction_t tx = { .length = count * 8, .tx_buffer = dev->dma_buffer };
        err = spi_device_polling_transmit(dev->spi, &tx);
        if (err != ESP_OK) return err;
        offset += count;
    }
    return ESP_OK;
}

esp_err_t uc8179_display_gray4(uc8179_handle_t dev, const uint8_t *pixels, size_t length)
{
    if (!dev || !pixels || length != UC8179_GRAY4_FRAME_BYTES) return ESP_ERR_INVALID_ARG;
    if (!dev->initialized || !dev->powered || !dev->gray4) return ESP_ERR_INVALID_STATE;
    esp_err_t err = uc8179_wait_idle(dev);
    int64_t start = esp_timer_get_time();
    if (err == ESP_OK) err = command(dev, 0x10);
    if (err == ESP_OK) err = gray4_plane(dev, pixels, 0);
    if (err == ESP_OK) err = command(dev, 0x13);
    if (err == ESP_OK) err = gray4_plane(dev, pixels, 1);
    dev->stats.transfer_us = esp_timer_get_time() - start;
    dev->stats.bytes = UC8179_FRAME_BYTES * 2;
    start = esp_timer_get_time();
    if (err == ESP_OK) err = command(dev, 0x12);
    if (err == ESP_OK) {
        esp_rom_delay_us(300);
        err = uc8179_wait_idle(dev);
    }
    dev->stats.refresh_us = esp_timer_get_time() - start;
    /* Gray4 RAM is two gray bitplanes, never a valid monochrome OLD base. */
    dev->image_valid = dev->partial_ready = false;
    if (err != ESP_OK) dev->initialized = false;
    return err;
}

esp_err_t uc8179_prepare_partial(uc8179_handle_t dev, const uint8_t *base, size_t length)
{
    if (!dev || !base || length != UC8179_FRAME_BYTES) return ESP_ERR_INVALID_ARG;
    if (!dev->initialized || !dev->powered || !dev->image_valid || dev->partial_ready || dev->gray4)
        return ESP_ERR_INVALID_STATE;
    /* Good Display ESP32epdx GDEY075T7: OTP partial TSFIX=2, TSSET=0x6e,
     * CDI=0xa9/0x07. UC8179 rev0.6 R50 N2OCP copies NEW to OLD after refresh.
     * Seed OLD explicitly because the normal/full path uses N2OCP=0. */
    const uint8_t interval[] = {0xa9,0x07}, fixed = 0x02, temperature = 0x6e;
    int64_t start = esp_timer_get_time();
    esp_err_t err = uc8179_wait_idle(dev);
    if (err == ESP_OK) err = setting(dev,0xe0,&fixed,1);
    if (err == ESP_OK) err = setting(dev,0xe5,&temperature,1);
    if (err == ESP_OK) err = setting(dev,0x50,interval,sizeof(interval));
    if (err == ESP_OK) err = command(dev,0x10);
    if (err == ESP_OK) err = data(dev,base,length,0);
    dev->stats.transfer_us += esp_timer_get_time()-start;
    dev->stats.bytes += length;
    dev->partial_ready = err == ESP_OK;
    if (err != ESP_OK) dev->initialized = dev->image_valid = false;
    return err;
}

esp_err_t uc8179_power_off(uc8179_handle_t dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    if (!dev->powered) return ESP_OK;
    int64_t start = esp_timer_get_time();
    esp_err_t err = uc8179_wait_idle(dev);
    if (err == ESP_OK) err = command(dev,0x02);
    if (err == ESP_OK) { esp_rom_delay_us(1000); err = uc8179_wait_idle(dev); }
    dev->stats.power_off_us = esp_timer_get_time()-start;
    if (err == ESP_OK) dev->powered = false;
    else dev->initialized = dev->image_valid = dev->partial_ready = false;
    return err;
}

esp_err_t uc8179_display_partial(uc8179_handle_t dev, const uint8_t *frame, size_t length)
{
    return uc8179_display_partial_region(dev,frame,length,0,0,UC8179_WIDTH,UC8179_HEIGHT);
}

esp_err_t uc8179_display_partial_region(uc8179_handle_t dev, const uint8_t *frame, size_t length,
                                       uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    if (!dev || !frame || length != UC8179_FRAME_BYTES) return ESP_ERR_INVALID_ARG;
    if (!width || !height || x >= UC8179_WIDTH || y >= UC8179_HEIGHT ||
        width > UC8179_WIDTH-x || height > UC8179_HEIGHT-y) return ESP_ERR_INVALID_ARG;
    if (!dev->initialized || !dev->partial_ready || !dev->image_valid) return ESP_ERR_INVALID_STATE;
    unsigned left = x & ~7U, right = (x+width-1U) | 7U, top = y, bottom = y+height-1U;
    if (right-left < 15) { if (left+15 < UC8179_WIDTH) right = left+15; else left = right-15; }
    if (top == bottom) { if (bottom+1 < UC8179_HEIGHT) ++bottom; else --top; }
    dev->stats = (uc8179_update_stats_t){0};
    int64_t start = esp_timer_get_time();
    esp_err_t err = uc8179_wait_idle(dev);
    if (err == ESP_OK && !dev->powered) {
        err = command(dev,0x04);
        if (err == ESP_OK) { dev->powered = true; esp_rom_delay_us(1000); err = uc8179_wait_idle(dev); }
    }
    dev->stats.power_on_us = esp_timer_get_time()-start;
    const uint8_t window[] = {left>>8,left&255,right>>8,right&255,top>>8,top&255,bottom>>8,bottom&255,1};
    start = esp_timer_get_time();
    if (err == ESP_OK) err = command(dev,0x91);
    if (err == ESP_OK) err = setting(dev,0x90,window,sizeof(window));
    if (err == ESP_OK) err = command(dev,0x13);
    size_t row_bytes = (right-left+1)/8;
    if (err == ESP_OK && row_bytes == UC8179_WIDTH/8)
        err = data(dev,frame+top*(UC8179_WIDTH/8),(bottom-top+1)*row_bytes,0);
    else for (unsigned row = top; err == ESP_OK && row <= bottom; ++row)
        err = data(dev,frame+row*(UC8179_WIDTH/8)+left/8,row_bytes,0);
    dev->stats.bytes = (bottom-top+1)*row_bytes;
    dev->stats.transfer_us = esp_timer_get_time()-start;
    start = esp_timer_get_time();
    if (err == ESP_OK) err = command(dev,0x12);
    if (err == ESP_OK) { esp_rom_delay_us(300); err = uc8179_wait_idle(dev); }
    dev->stats.refresh_us = esp_timer_get_time()-start;
    if (err == ESP_OK) err = command(dev,0x92);
    if (err != ESP_OK) dev->initialized = dev->image_valid = dev->partial_ready = false;
    return err;
}

esp_err_t uc8179_get_update_stats(uc8179_handle_t dev, uc8179_update_stats_t *out)
{
    if (!dev || !out) return ESP_ERR_INVALID_ARG;
    *out = dev->stats;
    return ESP_OK;
}

esp_err_t uc8179_sleep(uc8179_handle_t dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    if (!dev->initialized) return ESP_ERR_INVALID_STATE;
    const uint8_t interval = 0xF7;
    esp_err_t err = setting(dev, 0x50, &interval, 1);
    if (err == ESP_OK) err = uc8179_power_off(dev);
    if (err == ESP_OK) {
        delay_ms(100);
        const uint8_t check = 0xA5;
        err = setting(dev, 0x07, &check, 1);
    }
    dev->initialized = false;
    dev->image_valid = dev->partial_ready = false;
    return err;
}
