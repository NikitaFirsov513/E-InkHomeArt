if(CONFIG_APP_DISPLAY_TOUCH_TEST)
    set(app_sources "display_touch_main.c" "diagnostic_screen.c" "touch_test.c" "heartbeat.c")
elseif(CONFIG_APP_LED_TEST)
    set(app_sources "led_test_main.c" "heartbeat.c")
else()
    set(app_sources "diagnostic_main.c" "diagnostic_screen.c")
endif()

# IDF expands component requirements before loading CONFIG_* values. Keep
# requirements constant; the linker drops unused driver code in LED mode.
idf_component_register(SRCS ${app_sources}
                       PRIV_REQUIRES esp_driver_gpio spi_flash esp_psram board uc8179 gt911 lis2dw12
                                     esp_adc esp_timer esp_driver_uart esp_driver_usb_serial_jtag
                       INCLUDE_DIRS "")
