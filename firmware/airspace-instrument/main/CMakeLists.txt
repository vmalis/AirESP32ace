# AirESP32ace
# Created by VM, August 2026

idf_component_register(
    SRCS
        "app_config.c"
        "api_logos.c"
        "aircraft_icons.c"
        "aircraft_type_registry.c"
        "cargo_logos.c"
        "clock_hand_assets.c"
        "nasalization_countdown_50.c"
        "nasalization_countdown_68.c"
        "nasalization_prefix_34.c"
        "nasalization_expected_22.c"
        "detail_metric_icons.c"
        "gate_arrow_assets.c"
        "main.c"
        "data.c"
        "locations.c"
        "menu_icons.c"
        "muller_clock_22.c"
        "muller_clock_34.c"
        "network.c"
        "remote.c"
        "route_direction.c"
    INCLUDE_DIRS "."
    EMBED_FILES
        "assets/startup.png"
    REQUIRES
        esp_event
        esp_driver_gpio
        esp_driver_usb_serial_jtag
        esp_hw_support
        esp_http_client
        esp_http_server
        esp_netif
        esp-tls
        esp_timer
        esp_wifi
        fatfs
        heap
        json
        lwip
        mbedtls
        nvs_flash
        spi_flash
        wear_levelling
        espressif__libpng
        waveshare__esp32_s3_touch_amoled_1_75c
)

target_link_options(
    ${COMPONENT_LIB}
    INTERFACE
        "-Wl,--wrap=esp_lcd_new_panel_co5300"
        "-Wl,--wrap=esp_lcd_panel_io_tx_param"
)
