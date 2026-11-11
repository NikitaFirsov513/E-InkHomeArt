#pragma once
/* Host-only SDK boundary. Production drivers are compiled unchanged against
 * these declarations; the test runner supplies fault-injected I/O functions. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_NOT_FINISHED 0x10C

typedef int gpio_num_t;
#define GPIO_NUM_NC -1
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_NEGEDGE 2
#define GPIO_IS_VALID_GPIO(n) ((n) >= 0 && (n) <= 48 && ((n) < 22 || (n) > 25))
#define GPIO_IS_VALID_OUTPUT_GPIO(n) GPIO_IS_VALID_GPIO(n)
typedef struct {
    uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_set_direction(gpio_num_t pin, int mode);
esp_err_t gpio_intr_disable(gpio_num_t pin);
esp_err_t gpio_reset_pin(gpio_num_t pin);

typedef int spi_host_device_t;
#define SPI2_HOST 1
#define SPI3_HOST 2
#define SPI_DEVICE_HALFDUPLEX 16
#define SPI_TRANS_USE_TXDATA 4
typedef void *spi_device_handle_t;
typedef struct { int mode, clock_speed_hz, spics_io_num; uint32_t flags; int queue_size; } spi_device_interface_config_t;
typedef struct { uint32_t flags; size_t length; uint8_t tx_data[4]; const void *tx_buffer; } spi_transaction_t;
esp_err_t spi_bus_get_max_transaction_len(spi_host_device_t host, size_t *length);
esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *config, spi_device_handle_t *out);
esp_err_t spi_bus_remove_device(spi_device_handle_t device);
esp_err_t spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t *transaction);

#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_INTERNAL 2
void *heap_caps_malloc(size_t size, unsigned caps);
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) ((ms) / 10U)
void vTaskDelay(TickType_t ticks);
int64_t esp_timer_get_time(void);
void esp_rom_delay_us(uint32_t delay);

typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
#define I2C_ADDR_BIT_LEN_7 0
typedef struct { int dev_addr_length; uint16_t device_address; uint32_t scl_speed_hz; } i2c_device_config_t;
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *config, i2c_master_dev_handle_t *out);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *reg, size_t reg_length,
                                    uint8_t *data, size_t length, int timeout);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t length, int timeout);

typedef void *esp_lcd_panel_io_handle_t;
typedef struct {
    uint32_t dev_addr, scl_speed_hz;
    int transaction_timeout_ms;
} esp_lcd_panel_io_i2c_config_t;
typedef struct esp_lcd_touch_s *esp_lcd_touch_handle_t;
typedef void (*esp_lcd_touch_interrupt_callback_t)(esp_lcd_touch_handle_t touch);
typedef struct {
    uint16_t x_max, y_max;
    gpio_num_t rst_gpio_num, int_gpio_num;
    struct { unsigned reset, interrupt; } levels;
    struct { unsigned swap_xy, mirror_x, mirror_y; } flags;
    esp_lcd_touch_interrupt_callback_t interrupt_callback;
} esp_lcd_touch_config_t;
struct esp_lcd_touch_s { esp_lcd_touch_config_t config; };
typedef struct { uint8_t track_id; uint16_t x, y, strength; } esp_lcd_touch_point_data_t;
#define ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() { .dev_addr = 0x5D, .scl_speed_hz = 100000 }
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t bus, const esp_lcd_panel_io_i2c_config_t *config, esp_lcd_panel_io_handle_t *out);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io);
esp_err_t esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t io, int reg, void *data, size_t length);
esp_err_t esp_lcd_touch_new_i2c_gt911(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out);
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t touch);
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t touch);
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t touch, esp_lcd_touch_point_data_t *points, uint8_t *count, uint8_t max_points);
esp_err_t esp_lcd_touch_enter_sleep(esp_lcd_touch_handle_t touch);
