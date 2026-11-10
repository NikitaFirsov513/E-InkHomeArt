#include "uc8179.h"

#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
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
    size_t max_bytes = 0;
    esp_err_t err = spi_bus_get_max_transaction_len(config->host, &max_bytes);
    if (err != ESP_OK) return err;
    if (max_bytes < 4) return ESP_ERR_INVALID_ARG;
    uc8179_handle_t dev = calloc(1, sizeof(*dev));
    if (!dev) return ESP_ERR_NO_MEM;
    dev->config = *config;
    dev->chunk_size = (max_bytes < 4096 ? max_bytes : 4096) & ~(size_t)3;
    dev->dma_buffer = heap_caps_malloc(dev->chunk_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!dev->dma_buffer) { free(dev); return ESP_ERR_NO_MEM; }
    const spi_device_interface_config_t spi = {
        .mode = 0,
        .clock_speed_hz = config->clock_speed_hz,
        .spics_io_num = config->cs_pin,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 1,
    };
    err = spi_bus_add_device(config->host, &spi, &dev->spi);
    if (err != ESP_OK) { free(dev->dma_buffer); free(dev); return err; }
    *out = dev;
    return ESP_OK;
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
        delay_ms(1);
    }
    return ESP_OK;
}

esp_err_t uc8179_init(uc8179_handle_t dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    dev->initialized = false;
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
    const uint8_t panel = 0x1F;
    err = setting(dev, 0x00, &panel, 1);
    if (err == ESP_OK) err = command(dev, 0x04);
    if (err != ESP_OK) return err;
    delay_ms(300);
    err = uc8179_wait_idle(dev);
    const uint8_t interval[] = {0x21, 0x07};
    if (err == ESP_OK) err = setting(dev, 0x50, interval, sizeof(interval));
    if (err == ESP_OK) dev->initialized = true;
    return err;
}

static esp_err_t display(uc8179_handle_t dev, const uint8_t *frame, uint8_t fill)
{
    if (!dev->initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = uc8179_wait_idle(dev);
    if (err == ESP_OK) err = command(dev, 0x10);
    if (err == ESP_OK) err = data(dev, NULL, UC8179_FRAME_BYTES, 0x00);
    if (err == ESP_OK) err = command(dev, 0x13);
    if (err == ESP_OK) err = data(dev, frame, UC8179_FRAME_BYTES, fill);
    if (err == ESP_OK) err = command(dev, 0x12);
    if (err == ESP_OK) {
        delay_ms(1); /* Controller needs >=200 us before testing BUSY. */
        err = uc8179_wait_idle(dev);
    }
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

esp_err_t uc8179_sleep(uc8179_handle_t dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    if (!dev->initialized) return ESP_ERR_INVALID_STATE;
    const uint8_t interval = 0xF7;
    esp_err_t err = setting(dev, 0x50, &interval, 1);
    if (err == ESP_OK) err = command(dev, 0x02);
    if (err == ESP_OK) {
        delay_ms(1);
        err = uc8179_wait_idle(dev);
    }
    if (err == ESP_OK) {
        delay_ms(100);
        const uint8_t check = 0xA5;
        err = setting(dev, 0x07, &check, 1);
    }
    dev->initialized = false;
    return err;
}
