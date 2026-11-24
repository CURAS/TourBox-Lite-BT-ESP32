idf_component_register(
    SRCS "app_main.c" "usb_cdc_host.c" "ble_gatt_server.c"
         "proto_bridge.c" "bridge_core.c" "protocol_rules.c" "ui_indicator.c"
         "tourbox_advertisement.c" "lite_profile.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bt nvs_flash esp_driver_gpio esp_timer esp_system esp_psram
                  espressif__usb_host_cdc_acm espressif__led_strip)
