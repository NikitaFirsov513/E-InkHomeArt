idf_component_register(SRCS "home_backend.cpp" "home_network.c" "YandexAuth.cpp" "HttpsClient.cpp"
                            "AuthStorage.cpp" "DeviceManager.cpp" "DeviceAction.cpp" "home_catalog.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES home_ui esp_wifi esp_netif esp_http_client
                       PRIV_REQUIRES cjson nvs_flash esp_event esp_timer esp_psram mbedtls lwip)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
idf_build_get_property(project_dir PROJECT_DIR)
idf_build_get_property(build_dir BUILD_DIR)
set(private_header "${build_dir}/private/home_private.h")
execute_process(COMMAND "${PYTHON}" "${project_dir}/tools/generate_private_config.py"
                        "${project_dir}" "${private_header}" RESULT_VARIABLE generated)
if(NOT generated EQUAL 0)
    message(FATAL_ERROR "Invalid local configuration in secrets (values omitted)")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${project_dir}/tools/generate_private_config.py" "${project_dir}/secrets/wifi.json" "${project_dir}/secrets/yandex.json")
target_include_directories(${COMPONENT_LIB} PRIVATE "${build_dir}/private")
