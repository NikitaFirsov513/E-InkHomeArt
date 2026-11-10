#include "gt911.h"

#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct gt911_device {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t touch;
    gpio_num_t reset_pin;
    gpio_num_t interrupt_pin;
    uint16_t width;
    uint16_t height;
    bool sleeping;
    bool pins_owned;
};

static void delay_ms(uint32_t ms)
{
    /* Extra ticks guarantee the minimum delay even near a tick boundary. */
    vTaskDelay(pdMS_TO_TICKS(ms) + 2);
}

static esp_err_t interrupt_input(gt911_handle_t dev)
{
    const gpio_config_t input = {
        .pin_bit_mask = UINT64_C(1) << dev->interrupt_pin,
        .mode = GPIO_MODE_INPUT,
        .intr_type = dev->touch && dev->touch->config.interrupt_callback ? GPIO_INTR_NEGEDGE : GPIO_INTR_DISABLE,
    };
    return gpio_config(&input);
}

static esp_err_t interrupt_output(gt911_handle_t dev, uint32_t level)
{
    esp_err_t err = gpio_intr_disable(dev->interrupt_pin);
    if (err == ESP_OK) err = gpio_set_level(dev->interrupt_pin, level);
    if (err == ESP_OK) err = gpio_set_direction(dev->interrupt_pin, GPIO_MODE_OUTPUT);
    return err;
}

static esp_err_t reset_address(gt911_handle_t dev, uint8_t address)
{
    /* GT911 reset/address latch and INT synchronization. Avoid sub-tick
     * vTaskDelay(1 ms) in upstream: the project may use a 100 Hz RTOS tick. */
    esp_err_t err = gpio_set_level(dev->reset_pin, 0);
    if (err == ESP_OK) err = gpio_set_direction(dev->reset_pin, GPIO_MODE_OUTPUT);
    if (err == ESP_OK) err = interrupt_output(dev, 0);
    if (err != ESP_OK) return err;
    delay_ms(20);
    err = gpio_set_level(dev->interrupt_pin, address == GT911_ADDRESS_HIGH);
    if (err != ESP_OK) return err;
    delay_ms(2);
    err = gpio_set_level(dev->reset_pin, 1);
    if (err != ESP_OK) return err;
    delay_ms(6);
    err = gpio_set_level(dev->interrupt_pin, 0);
    if (err != ESP_OK) return err;
    delay_ms(50);
    return interrupt_input(dev);
}

esp_err_t gt911_new(const gt911_config_t *config, gt911_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!config || !config->bus || !config->width || !config->height ||
        !config->clock_speed_hz || !GPIO_IS_VALID_OUTPUT_GPIO(config->reset_pin) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->interrupt_pin) ||
        config->reset_pin == config->interrupt_pin ||
        (config->address != GT911_ADDRESS_LOW && config->address != GT911_ADDRESS_HIGH)) {
        return ESP_ERR_INVALID_ARG;
    }
    gt911_handle_t dev = calloc(1, sizeof(*dev));
    if (!dev) return ESP_ERR_NO_MEM;
    dev->reset_pin = config->reset_pin;
    dev->interrupt_pin = config->interrupt_pin;
    dev->width = config->swap_xy ? config->height : config->width;
    dev->height = config->swap_xy ? config->width : config->height;
    esp_lcd_panel_io_i2c_config_t io = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io.dev_addr = config->address;
    io.scl_speed_hz = config->clock_speed_hz;
    esp_err_t err = esp_lcd_new_panel_io_i2c(config->bus, &io, &dev->io);
    if (err != ESP_OK) goto fail;
    dev->pins_owned = true;
    err = reset_address(dev, config->address);
    if (err != ESP_OK) goto fail;
    const esp_lcd_touch_config_t touch = {
        /* Upstream mirrors as x_max - x, not x_max - 1 - x. */
        .x_max = config->width - 1,
        .y_max = config->height - 1,
        .rst_gpio_num = GPIO_NUM_NC, /* Reset/address already handled above. */
        .int_gpio_num = config->interrupt_pin,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = {
            .swap_xy = config->swap_xy,
            .mirror_x = config->mirror_x,
            .mirror_y = config->mirror_y,
        },
    };
    err = esp_lcd_touch_new_i2c_gt911(dev->io, &touch, &dev->touch);
    if (err != ESP_OK) goto fail;
    err = gpio_intr_disable(dev->interrupt_pin); /* Polling until callback registered. */
    if (err != ESP_OK) goto fail;
    uint8_t id[4] = {0};
    err = esp_lcd_panel_io_rx_param(dev->io, 0x8140, id, sizeof(id));
    if (err == ESP_OK && memcmp(id, "911", 3) != 0) err = ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) goto fail;
    *out = dev;
    return ESP_OK;
fail:
    if (dev->touch) esp_lcd_touch_del(dev->touch);
    if (dev->io) esp_lcd_panel_io_del(dev->io);
    if (dev->pins_owned) {
        gpio_reset_pin(dev->reset_pin);
        gpio_reset_pin(dev->interrupt_pin);
    }
    free(dev);
    return err;
}

esp_err_t gt911_delete(gt911_handle_t dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    esp_err_t err = esp_lcd_touch_del(dev->touch);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_io_del(dev->io);
    gpio_reset_pin(dev->reset_pin);
    free(dev);
    return err;
}

esp_err_t gt911_read(gt911_handle_t dev,
                    esp_lcd_touch_point_data_t points[GT911_MAX_POINTS], uint8_t *count)
{
    if (!count) return ESP_ERR_INVALID_ARG;
    *count = 0;
    if (!dev || !points) return ESP_ERR_INVALID_ARG;
    if (dev->sleeping) return ESP_ERR_INVALID_STATE;
    memset(points, 0, GT911_MAX_POINTS * sizeof(*points));
    esp_err_t err = esp_lcd_touch_read_data(dev->touch);
    if (err != ESP_OK) return err;
    err = esp_lcd_touch_get_data(dev->touch, points, count, GT911_MAX_POINTS);
    if (err != ESP_OK) { *count = 0; return err; }
    for (uint8_t i = 0; i < *count; ++i) {
        if (points[i].x >= dev->width || points[i].y >= dev->height) {
            *count = 0;
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

esp_err_t gt911_sleep(gt911_handle_t dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    if (dev->sleeping) return ESP_OK;
    esp_err_t err = interrupt_output(dev, 0);
    if (err != ESP_OK) return err;
    delay_ms(5);
    err = esp_lcd_touch_enter_sleep(dev->touch);
    if (err == ESP_OK) {
        dev->sleeping = true;
        delay_ms(60); /* GT911 requires >58 ms before a wake-up. */
    } else {
        interrupt_input(dev);
    }
    return err;
}

esp_err_t gt911_wake(gt911_handle_t dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    if (!dev->sleeping) return ESP_OK;
    esp_err_t err = interrupt_output(dev, 1);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(3000); /* Wake pulse is 2..5 ms; independent of RTOS tick. */
    err = gpio_set_level(dev->interrupt_pin, 0);
    if (err != ESP_OK) return err;
    delay_ms(50);
    err = interrupt_input(dev);
    if (err != ESP_OK) return err;
    uint8_t id[4];
    err = esp_lcd_panel_io_rx_param(dev->io, 0x8140, id, sizeof(id));
    if (err == ESP_OK && memcmp(id, "911", 3) != 0) err = ESP_ERR_NOT_FOUND;
    if (err == ESP_OK) dev->sleeping = false;
    return err;
}

esp_lcd_touch_handle_t gt911_touch_handle(gt911_handle_t dev)
{
    return dev ? dev->touch : NULL;
}
