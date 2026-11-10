#include "lis2dw12_idf.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct lis2dw12_device {
    i2c_master_dev_handle_t i2c;
    stmdev_ctx_t st;
    int timeout_ms;
};

static int32_t read_register(void *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
    lis2dw12_handle_t dev = handle;
    if (!data || !len || (unsigned)reg + len > 256) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(dev->i2c, &reg, 1, data, len, dev->timeout_ms);
}

static int32_t write_register(void *handle, uint8_t reg, const uint8_t *data, uint16_t len)
{
    lis2dw12_handle_t dev = handle;
    if (!data || !len || (unsigned)reg + len > 256) return ESP_ERR_INVALID_ARG;
    uint8_t packet[257];
    packet[0] = reg;
    memcpy(packet + 1, data, len);
    return i2c_master_transmit(dev->i2c, packet, len + 1, dev->timeout_ms);
}

static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) + 2);
}

esp_err_t lis2dw12_idf_new(const lis2dw12_idf_config_t *config, lis2dw12_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!config || !config->bus || !config->clock_speed_hz || config->timeout_ms <= 0 ||
        (config->address != 0x18 && config->address != 0x19)) return ESP_ERR_INVALID_ARG;
    lis2dw12_handle_t dev = calloc(1, sizeof(*dev));
    if (!dev) return ESP_ERR_NO_MEM;
    dev->timeout_ms = config->timeout_ms;
    dev->st = (stmdev_ctx_t) {
        .read_reg = read_register, .write_reg = write_register,
        .mdelay = delay_ms, .handle = dev,
    };
    const i2c_device_config_t i2c = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->address,
        .scl_speed_hz = config->clock_speed_hz,
    };
    esp_err_t err = i2c_master_bus_add_device(config->bus, &i2c, &dev->i2c);
    if (err != ESP_OK) goto fail;
    uint8_t id = 0;
    err = lis2dw12_device_id_get(&dev->st, &id);
    if (err == ESP_OK && id != LIS2DW12_ID) err = ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) goto fail;
    *out = dev;
    return ESP_OK;
fail:
    if (dev->i2c) i2c_master_bus_rm_device(dev->i2c);
    free(dev);
    return err;
}

esp_err_t lis2dw12_idf_delete(lis2dw12_handle_t dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    esp_err_t err = i2c_master_bus_rm_device(dev->i2c);
    if (err == ESP_OK) free(dev);
    return err;
}

esp_err_t lis2dw12_idf_start(lis2dw12_handle_t dev, lis2dw12_fs_t range, lis2dw12_odr_t rate)
{
    if (!dev || range < LIS2DW12_2g || range > LIS2DW12_16g ||
        rate < LIS2DW12_XL_ODR_12Hz5 || rate > LIS2DW12_XL_ODR_1k6Hz) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = lis2dw12_reset_set(&dev->st);
    if (err != ESP_OK) return err;
    /* Bounded reset polling; no unbounded waits on missing/broken hardware. */
    uint8_t resetting = 1;
    for (unsigned i = 0; i < 100; ++i) {
        delay_ms(1);
        err = lis2dw12_reset_get(&dev->st, &resetting);
        if (err != ESP_OK) return err;
        if (!resetting) break;
    }
    if (resetting) return ESP_ERR_TIMEOUT;
    err = lis2dw12_block_data_update_set(&dev->st, 1);
    if (err == ESP_OK) err = lis2dw12_auto_increment_set(&dev->st, 1);
    if (err == ESP_OK) err = lis2dw12_power_mode_set(&dev->st, LIS2DW12_HIGH_PERFORMANCE);
    if (err == ESP_OK) err = lis2dw12_full_scale_set(&dev->st, range);
    if (err == ESP_OK) err = lis2dw12_data_rate_set(&dev->st, rate);
    return err;
}

esp_err_t lis2dw12_idf_power_down(lis2dw12_handle_t dev)
{
    return dev ? lis2dw12_data_rate_set(&dev->st, LIS2DW12_XL_ODR_OFF) : ESP_ERR_INVALID_ARG;
}

esp_err_t lis2dw12_idf_read(lis2dw12_handle_t dev, lis2dw12_sample_t *sample)
{
    if (!dev || !sample) return ESP_ERR_INVALID_ARG;
    memset(sample, 0, sizeof(*sample));
    uint8_t ready = 0;
    esp_err_t err = lis2dw12_flag_data_ready_get(&dev->st, &ready);
    if (err != ESP_OK) return err;
    if (!ready) return ESP_ERR_NOT_FINISHED;
    lis2dw12_fs_t range;
    lis2dw12_mode_t mode;
    err = lis2dw12_full_scale_get(&dev->st, &range);
    if (err == ESP_OK) err = lis2dw12_power_mode_get(&dev->st, &mode);
    if (err == ESP_OK) err = lis2dw12_acceleration_raw_get(&dev->st, sample->raw);
    if (err != ESP_OK) return err;
    const bool lp12 = (mode & 0x07) == 0;
    for (unsigned i = 0; i < 3; ++i) {
        switch (range) {
        case LIS2DW12_2g:
            sample->mg[i] = lp12 ? lis2dw12_from_fs2_lp1_to_mg(sample->raw[i]) : lis2dw12_from_fs2_to_mg(sample->raw[i]);
            break;
        case LIS2DW12_4g:
            sample->mg[i] = lp12 ? lis2dw12_from_fs4_lp1_to_mg(sample->raw[i]) : lis2dw12_from_fs4_to_mg(sample->raw[i]);
            break;
        case LIS2DW12_8g:
            sample->mg[i] = lp12 ? lis2dw12_from_fs8_lp1_to_mg(sample->raw[i]) : lis2dw12_from_fs8_to_mg(sample->raw[i]);
            break;
        case LIS2DW12_16g:
            sample->mg[i] = lp12 ? lis2dw12_from_fs16_lp1_to_mg(sample->raw[i]) : lis2dw12_from_fs16_to_mg(sample->raw[i]);
            break;
        default: return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}

const stmdev_ctx_t *lis2dw12_idf_context(lis2dw12_handle_t dev)
{
    return dev ? &dev->st : NULL;
}
