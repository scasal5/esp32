idf_component_register(
    SRCS "app_main.c" "app_menu.c" "board_rtc_pcf85063.c" "boot_splash.c" "home_screen.c"
         "menu_button.c" "pm_axp2101.cpp" "pm_format.c" "splash_gif.c" "svc_wifi.c"
         "time_console.c" "wifi_scan_ui.c"
    INCLUDE_DIRS "."
)
