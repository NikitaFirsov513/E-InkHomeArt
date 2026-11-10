idf_component_register(SRCS "gt911.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c esp_lcd esp_lcd_touch_gt911
                       PRIV_REQUIRES esp_rom)
