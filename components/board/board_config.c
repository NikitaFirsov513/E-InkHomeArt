#include "board_config.h"
#include "soc/adc_channel.h"
#include "soc/soc_caps.h"

/* Detect duplicate assignments and attempts to use invalid or memory GPIOs. */
_Static_assert(__builtin_popcountll(BOARD_GPIO_USED_MASK) == 22,
               "The board must have 22 distinct external GPIO assignments");
_Static_assert((BOARD_GPIO_USED_MASK & ~SOC_GPIO_VALID_GPIO_MASK) == 0,
               "Invalid ESP32-S3 GPIO assignment");
_Static_assert((BOARD_GPIO_USED_MASK & BOARD_GPIO_MEMORY_MASK) == 0,
               "Peripheral GPIO overlaps Flash/PSRAM");
_Static_assert((BOARD_GPIO_USED_MASK & BOARD_GPIO_UNUSED_MASK) == 0,
               "Used and unconnected GPIOs overlap");
_Static_assert(__builtin_popcountll(BOARD_GPIO_OUTPUT_MASK) == BOARD_GPIO_OUTPUT_COUNT,
               "Output count does not match the GPIO mask");
_Static_assert(BOARD_ADC_UNIT == ADC_UNIT_1 && BOARD_ADC_BATTERY_CHANNEL == ADC_CHANNEL_0 &&
               BOARD_PIN_BATTERY_ADC == ADC1_CHANNEL_0_GPIO_NUM,
               "Battery GPIO must match ADC1 channel 0");
_Static_assert(BOARD_ADC_USB_CC1_CHANNEL == ADC_CHANNEL_7 && BOARD_PIN_USB_CC1_ADC == ADC1_CHANNEL_7_GPIO_NUM,
               "CC1 GPIO must match ADC1 channel 7");
_Static_assert(BOARD_ADC_USB_CC2_CHANNEL == ADC_CHANNEL_2 && BOARD_PIN_USB_CC2_ADC == ADC1_CHANNEL_2_GPIO_NUM,
               "CC2 GPIO must match ADC1 channel 2");

const board_gpio_output_level_t board_gpio_output_levels[BOARD_GPIO_OUTPUT_COUNT] = {
    { .pin = BOARD_PIN_EPD_CS,      .level = 1 }, /* Deselect the display. */
    { .pin = BOARD_PIN_EPD_DC,      .level = 0 },
    { .pin = BOARD_PIN_EPD_RESET,   .level = 1 }, /* Reset deasserted. */
    { .pin = BOARD_PIN_TOUCH_RESET, .level = 1 }, /* No reset pulse yet. */
    { .pin = BOARD_PIN_STATUS_LED,  .level = 0 },
    { .pin = BOARD_PIN_ETA_USB_DET, .level = 0 }, /* Q2 off, USB_DET not forced low. */
};

const gpio_config_t board_gpio_output_config = {
    .pin_bit_mask = BOARD_GPIO_OUTPUT_MASK,
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

const gpio_config_t board_gpio_input_config = {
    .pin_bit_mask = BOARD_GPIO_INPUT_MASK,
    .mode = GPIO_MODE_INPUT,
    /* BOOT and POWER already have external pull-ups; sensors drive INT/BUSY. */
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

const gpio_config_t board_gpio_analog_config = {
    .pin_bit_mask = BOARD_GPIO_ADC_MASK,
    .mode = GPIO_MODE_DISABLE,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

const spi_bus_config_t board_epd_spi_bus_config = {
    .mosi_io_num = BOARD_PIN_EPD_MOSI,
    .miso_io_num = GPIO_NUM_NC,
    .sclk_io_num = BOARD_PIN_EPD_SCLK,
    .quadwp_io_num = GPIO_NUM_NC,
    .quadhd_io_num = GPIO_NUM_NC,
    .data4_io_num = GPIO_NUM_NC,
    .data5_io_num = GPIO_NUM_NC,
    .data6_io_num = GPIO_NUM_NC,
    .data7_io_num = GPIO_NUM_NC,
    .data_io_default_level = false,
    .max_transfer_sz = BOARD_EPD_SPI_MAX_TRANSFER,
    .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_MOSI | SPICOMMON_BUSFLAG_SCLK,
};

const spi_device_interface_config_t board_epd_spi_device_config = {
    .mode = 0,
    .clock_speed_hz = BOARD_EPD_SPI_CLOCK_HZ,
    .spics_io_num = BOARD_PIN_EPD_CS,
    .flags = SPI_DEVICE_HALFDUPLEX,
    .queue_size = 1,
};

const i2c_master_bus_config_t board_i2c_bus_config = {
    .i2c_port = BOARD_I2C_PORT,
    .sda_io_num = BOARD_PIN_I2C_SDA,
    .scl_io_num = BOARD_PIN_I2C_SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = false, /* R19/R20: 2.2 kOhm external. */
};

const i2c_device_config_t board_lis2dw12_i2c_config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = BOARD_LIS2DW12_I2C_ADDRESS,
    .scl_speed_hz = BOARD_I2C_CLOCK_HZ,
};

const adc_oneshot_unit_init_cfg_t board_adc_unit_config = {
    .unit_id = BOARD_ADC_UNIT,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
};

const adc_oneshot_chan_cfg_t board_adc_channel_config = {
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12,
};

const board_adc_input_t board_adc_inputs[BOARD_ADC_INPUT_COUNT] = {
    [BOARD_ADC_BATTERY] = {
        .pin = BOARD_PIN_BATTERY_ADC,
        .channel = BOARD_ADC_BATTERY_CHANNEL,
        .voltage_scale_numerator = BOARD_BATTERY_DIVIDER_TOP_OHM + BOARD_BATTERY_DIVIDER_LOW_OHM,
        .voltage_scale_denominator = BOARD_BATTERY_DIVIDER_LOW_OHM,
    },
    [BOARD_ADC_USB_CC1] = {
        .pin = BOARD_PIN_USB_CC1_ADC,
        .channel = BOARD_ADC_USB_CC1_CHANNEL,
        /* R22 is a series resistor, not a 1:2 voltage divider. */
        .voltage_scale_numerator = 1,
        .voltage_scale_denominator = 1,
    },
    [BOARD_ADC_USB_CC2] = {
        .pin = BOARD_PIN_USB_CC2_ADC,
        .channel = BOARD_ADC_USB_CC2_CHANNEL,
        .voltage_scale_numerator = 1,
        .voltage_scale_denominator = 1,
    },
};

/* Optional connector J2. USB remains the primary application console. */
const uart_config_t board_debug_uart_config = {
    .baud_rate = BOARD_DEBUG_UART_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
};
