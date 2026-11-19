if(CONFIG_APP_ART_GRAY_TEST)
    set(app_sources "art_gray_test_main.c" "heartbeat.c")
elseif(CONFIG_APP_DISPLAY_TOUCH_TEST)
    set(app_sources "display_touch_main.c" "diagnostic_screen.c" "touch_test.c" "frame_pipeline.c" "heartbeat.c")
elseif(CONFIG_APP_LED_TEST)
    set(app_sources "led_test_main.c" "heartbeat.c")
else()
    set(app_sources "diagnostic_main.c" "diagnostic_screen.c")
endif()

# IDF expands component requirements before loading CONFIG_* values. Keep
# requirements constant; the linker drops unused driver code in LED mode.
idf_component_register(SRCS ${app_sources}
                       PRIV_REQUIRES esp_driver_gpio spi_flash esp_psram board uc8179 gt911 lis2dw12 art_store
                                     esp_adc esp_timer esp_driver_uart esp_driver_usb_serial_jtag
                       INCLUDE_DIRS "")

# Temporary stage-2 photo verification; normal Wi-Fi upload is a later stage.
# The path is a CMake argument, never a user photograph committed to the repo.
if(CONFIG_APP_ART_GRAY_TEST AND ART_TEST_FRAME)
    get_filename_component(art_test_file "${ART_TEST_FRAME}" ABSOLUTE BASE_DIR "${PROJECT_DIR}")
    if(NOT EXISTS "${art_test_file}")
        message(FATAL_ERROR "ART_TEST_FRAME does not exist: ${art_test_file}")
    endif()
    file(SIZE "${art_test_file}" art_test_size)
    if(NOT art_test_size EQUAL 96064)
        message(FATAL_ERROR "ART_TEST_FRAME must be a 96064-byte .ehart packet")
    endif()
    target_add_binary_data(${COMPONENT_LIB} "${art_test_file}" BINARY RENAME_TO art_test_frame)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ART_TEST_FRAME_EMBEDDED=1)
endif()
