idf_component_register(SRCS "board_config.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2c
                                esp_driver_uart esp_adc)
