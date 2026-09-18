# ESP-IDF's early dependency expansion runs in a separate CMake script;
# custom cache variables are not inherited, but the environment is.
set(HARNESS_MODE "$ENV{WS183_BUILD_MODE}")
if(HARNESS_MODE STREQUAL "probe")
    idf_component_register(SRCS "probe.c" REQUIRES app_update esp_timer)
    return()
endif()
set(src "main.c" "board_ws183.c" "battery.cpp" "native_wifi.c" "metrics.c" "present.c" "scheduler.c")
set(req esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_ledc esp_lcd esp_lcd_touch_cst816s
    esp_timer esp_psram esp_wifi esp_event esp_netif nvs_flash spiffs console esp_driver_usb_serial_jtag
    app_update xpowerslib)
if(HARNESS_MODE STREQUAL "headless" OR HARNESS_MODE STREQUAL "pocket")
    list(APPEND src "guest_host.c")
    list(APPEND req pocketjs_guest pocketjs_package)
endif()
if(HARNESS_MODE STREQUAL "pocket")
    list(APPEND src "pocket_ui.c")
    list(APPEND req pocketjs_ui_qjs pocketjs_render_rgb565)
endif()
idf_component_register(SRCS ${src} INCLUDE_DIRS "." REQUIRES ${req})
target_compile_definitions(${COMPONENT_LIB} PRIVATE HARNESS_MODE_${HARNESS_MODE}=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
if(HARNESS_MODE STREQUAL "headless" OR HARNESS_MODE STREQUAL "pocket")
    execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/host_contract.py"
        "${CMAKE_CURRENT_LIST_DIR}/../pocket.host.json" "${CMAKE_CURRENT_BINARY_DIR}/host_contract.h"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Could not generate trusted host contract")
    endif()
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
