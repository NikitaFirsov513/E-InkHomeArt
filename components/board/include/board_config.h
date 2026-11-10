#pragma once

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration data only: nothing here installs a driver or touches hardware. */
#define BOARD_EPD_SPI_HOST             SPI2_HOST
#define BOARD_EPD_SPI_DMA              SPI_DMA_CH_AUTO
#define BOARD_EPD_SPI_CLOCK_HZ         1000000
#define BOARD_EPD_SPI_MAX_TRANSFER     4096

#define BOARD_I2C_PORT                 I2C_NUM_0
#define BOARD_I2C_CLOCK_HZ             100000
#define BOARD_I2C_TIMEOUT_MS           100
#define BOARD_LIS2DW12_I2C_ADDRESS      0x19 /* SA0 tied to 3V3, 7-bit address. */

#define BOARD_ADC_UNIT                 ADC_UNIT_1
#define BOARD_ADC_BATTERY_CHANNEL      ADC_CHANNEL_0
#define BOARD_ADC_USB_CC1_CHANNEL      ADC_CHANNEL_7
#define BOARD_ADC_USB_CC2_CHANNEL      ADC_CHANNEL_2
#define BOARD_BATTERY_DIVIDER_TOP_OHM  100000
#define BOARD_BATTERY_DIVIDER_LOW_OHM  100000
#define BOARD_USB_CC_SERIES_OHM        100000

#define BOARD_DEBUG_UART_PORT          UART_NUM_0
#define BOARD_DEBUG_UART_BAUD          115200

#define BOARD_BUTTON_ACTIVE_LEVEL     0
#define BOARD_LED_ACTIVE_LEVEL        1
#define BOARD_RESET_ACTIVE_LEVEL      0
#define BOARD_GPIO_OUTPUT_COUNT       6

typedef struct {
    gpio_num_t pin;
    uint32_t level;
} board_gpio_output_level_t;

typedef enum {
    BOARD_ADC_BATTERY,
    BOARD_ADC_USB_CC1,
    BOARD_ADC_USB_CC2,
    BOARD_ADC_INPUT_COUNT,
} board_adc_input_id_t;

typedef struct {
    gpio_num_t pin;
    adc_channel_t channel;
    /* Scale calibrated ADC millivolts, not raw ADC counts. */
    uint32_t voltage_scale_numerator;
    uint32_t voltage_scale_denominator;
} board_adc_input_t;

/* Future init must preload output levels BEFORE enabling output directions. */
extern const board_gpio_output_level_t board_gpio_output_levels[BOARD_GPIO_OUTPUT_COUNT];
extern const gpio_config_t board_gpio_output_config;
/* Interrupt sources stay disabled until their device drivers configure them. */
extern const gpio_config_t board_gpio_input_config;
extern const gpio_config_t board_gpio_analog_config;

extern const spi_bus_config_t board_epd_spi_bus_config;
/* Bring-up baseline: mode 0, MSB first, 1 MHz, active-low hardware CS. */
extern const spi_device_interface_config_t board_epd_spi_device_config;
extern const i2c_master_bus_config_t board_i2c_bus_config;
extern const i2c_device_config_t board_lis2dw12_i2c_config;
/* Touch address and RESET/INT sequence belong to the selected touch driver. */

extern const adc_oneshot_unit_init_cfg_t board_adc_unit_config;
extern const adc_oneshot_chan_cfg_t board_adc_channel_config;
extern const board_adc_input_t board_adc_inputs[BOARD_ADC_INPUT_COUNT];
extern const uart_config_t board_debug_uart_config;

#ifdef __cplusplus
}
#endif
