idf_component_register(SRCS "hello_world_main.c"
                       PRIV_REQUIRES spi_flash esp_psram board uc8179 gt911 lis2dw12
                       INCLUDE_DIRS "")
