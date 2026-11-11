idf_component_register(SRCS "diagnostic_main.c" "diagnostic_screen.c"
                       PRIV_REQUIRES spi_flash esp_psram board uc8179 gt911 lis2dw12
                                     esp_adc esp_timer esp_driver_uart esp_driver_usb_serial_jtag
                       INCLUDE_DIRS "")
