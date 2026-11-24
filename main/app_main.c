#include "usb_cdc_host.h"
#include "ble_gatt_server.h"
#include "proto_bridge.h"
#include "ui_indicator.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_psram.h"
#include "nvs_flash.h"

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI("app", "TourBox USB-to-BLE bridge 1.0.2 / ESP-IDF %s / %u cores / PSRAM %u MB",
             esp_get_idf_version(), chip.cores, (unsigned)(esp_psram_get_size() / (1024 * 1024)));
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW("app", "NVS format/full; reinitializing storage (bonds cleared)");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    /* Buffers exist before any callback producer; BLE host exists before USB. */
    ESP_ERROR_CHECK(proto_bridge_init());
    ESP_ERROR_CHECK(ble_gatt_server_init());
    ESP_ERROR_CHECK(usb_cdc_host_init());
    ESP_ERROR_CHECK(ui_indicator_init());
    ESP_LOGI("app", "Bridge started. Native USB: Host GPIO19/20; debug: UART0 GPIO43/44");
}
