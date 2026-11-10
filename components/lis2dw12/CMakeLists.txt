idf_component_register(SRCS "lis2dw12_idf.c" "vendor/lis2dw12_reg.c"
                       INCLUDE_DIRS "include" "vendor"
                       REQUIRES esp_driver_i2c)
