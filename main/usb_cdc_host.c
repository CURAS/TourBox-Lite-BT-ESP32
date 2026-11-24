#include "usb_cdc_host.h"
#include "proto_bridge.h"
#include "ble_gatt_server.h"
#include "tourbox_proto.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include <stdatomic.h>

static const char *TAG = "usb_cdc";
typedef struct { uint16_t vid, pid; uint8_t address; bool accepted; } usb_candidate_t;
static QueueHandle_t candidates;
static atomic_bool device_lost;
static atomic_int device_error;
static atomic_bool reset_requested;

static bool known_id(uint16_t vid, uint16_t pid)
{
    return (vid == CONFIG_TB_USB_VID && pid == CONFIG_TB_USB_PID) ||
           (vid == TB_USB_VID_OBSERVED_LITE && pid == TB_USB_PID_OBSERVED_LITE) ||
           (vid == TB_USB_VID_STM32 && (pid == TB_USB_PID_ELITE || pid == TB_USB_PID_AMBIGUOUS)) ||
           (vid == TB_USB_VID_C251 && pid == TB_USB_PID_2005) ||
           (vid == TB_USB_VID_ARTERY && pid == TB_USB_PID_ARTERY);
}

static bool has_cdc_interface(usb_device_handle_t dev)
{
    const usb_config_desc_t *config;
    if (usb_host_get_active_config_descriptor(dev, &config) != ESP_OK) return false;
    const uint8_t *bytes = (const uint8_t *)config;
    for (size_t offset = 0; offset + 2 <= config->wTotalLength;) {
        size_t length = bytes[offset];
        if (length < 2 || offset + length > config->wTotalLength) break;
        if (bytes[offset + 1] == USB_B_DESCRIPTOR_TYPE_INTERFACE && length >= 9 &&
            bytes[offset + 5] == 0x02 && bytes[offset + 6] == 0x02) return true;
        offset += length;
    }
    return false;
}

static void new_device(usb_device_handle_t dev)
{
    const usb_device_desc_t *desc;
    usb_device_info_t info;
    if (usb_host_get_device_descriptor(dev, &desc) != ESP_OK || usb_host_device_info(dev, &info) != ESP_OK) return;
    usb_candidate_t candidate = {.vid = desc->idVendor, .pid = desc->idProduct, .address = info.dev_addr};
    candidate.accepted = known_id(candidate.vid, candidate.pid);
#if CONFIG_TB_USB_ANY_CDC
    if (!candidate.accepted) candidate.accepted = has_cdc_interface(dev);
#endif
    xQueueSend(candidates, &candidate, 0);
}

static bool on_data(const uint8_t *data, size_t length, void *arg)
{
    return bridge_on_usb_rx(data, length);
}

static void on_event(const cdc_acm_host_dev_event_data_t *event, void *arg)
{
    if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED || event->type == CDC_ACM_HOST_ERROR) {
        if (event->type == CDC_ACM_HOST_ERROR) {
            atomic_store(&device_error, event->data.error);
            atomic_store(&reset_requested, true);
        }
        atomic_store(&device_lost, true);
        bridge_set_usb_ready(false);
        /* close() waits for this driver task: defer it to usb_cdc_task. */
    }
}

static void usb_lib_task(void *arg)
{
    for (;;) {
        uint32_t flags = 0;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) ESP_LOGE(TAG, "USB host events: %s", esp_err_to_name(err));
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

static void reset_root_port(void)
{
    /* Toggles host-controller port state, not the externally supplied 5V VBUS. */
    ESP_LOGW(TAG, "Resetting USB root port for enumeration recovery");
    esp_err_t err = usb_host_lib_set_root_port_power(false);
    if (err != ESP_OK) ESP_LOGW(TAG, "Root port off: %s", esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(150));
    err = usb_host_lib_set_root_port_power(true);
    if (err != ESP_OK) ESP_LOGW(TAG, "Root port on: %s", esp_err_to_name(err));
}

static esp_err_t open_candidate(const usb_candidate_t *candidate, cdc_acm_dev_hdl_t *device)
{
    cdc_acm_host_open_config_t config = {
        .vid = candidate->vid, .pid = candidate->pid, .dev_addr = candidate->address,
        .interface_idx = CONFIG_TB_USB_INTERFACE, .connection_timeout_ms = 300,
        .out_buffer_size = TB_MAX_WRITE, .in_buffer_size = TB_MAX_WRITE,
        .event_cb = on_event, .data_cb = on_data,
    };
    esp_err_t err = cdc_acm_host_open(&config, device);
    if (err != ESP_OK) return err;
    cdc_acm_line_coding_t line = {
        .dwDTERate = CONFIG_TB_USB_BAUDRATE, .bCharFormat = 0, .bParityType = 0, .bDataBits = 8,
    };
    err = cdc_acm_host_line_coding_set(*device, &line);
    if (err == ESP_OK) err = cdc_acm_host_set_control_line_state(*device, true, true);
    if (err != ESP_OK) {
        cdc_acm_host_close(*device);
        *device = NULL;
    }
    return err;
}

static void usb_cdc_task(void *arg)
{
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    cdc_acm_dev_hdl_t device = NULL;
    usb_candidate_t candidate = {0};
    unsigned open_failures = 0, tx_timeouts = 0;
    uint8_t tx[TB_MAX_WRITE];
    for (;;) {
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        if (device && atomic_load(&device_lost)) {
            bridge_set_usb_ready(false);
            ESP_LOGW(TAG, "USB disconnected/error=%d; closing CDC handle", atomic_load(&device_error));
            ESP_ERROR_CHECK(cdc_acm_host_close(device));
            device = NULL;
            candidate.accepted = false;
            tx_timeouts = 0;
            if (atomic_exchange(&reset_requested, false)) reset_root_port();
        }
        if (!device) {
            usb_candidate_t next;
            if (xQueueReceive(candidates, &next, pdMS_TO_TICKS(100)) == pdTRUE) {
                ESP_LOGI(TAG, "USB device %04x:%04x address=%u %s", next.vid, next.pid,
                         next.address, next.accepted ? "candidate" : "ignored (not matching CDC)");
                if (next.accepted) { candidate = next; open_failures = 0; }
            }
            if (!candidate.accepted) continue;
            atomic_store(&device_lost, false);
            atomic_store(&device_error, 0);
            atomic_store(&reset_requested, false);
            esp_err_t err = open_candidate(&candidate, &device);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "CDC open/configure: %s", esp_err_to_name(err));
                if (++open_failures >= 3) {
                    candidate.accepted = false;
                    open_failures = 0;
                    reset_root_port();
                }
                vTaskDelay(pdMS_TO_TICKS(250));
                continue;
            }
            if (atomic_load(&device_lost)) continue;
            bridge_set_usb_identity(candidate.vid, candidate.pid);
            bridge_set_usb_ready(true);
            if (atomic_load(&device_lost)) { bridge_set_usb_ready(false); continue; }
            ESP_LOGI(TAG, "TourBox USB ready: %u 8N1, DTR=1 RTS=1", CONFIG_TB_USB_BAUDRATE);
#if CONFIG_TB_USB_AUTO_UNLOCK
            const uint8_t unlock[] = TB_UNLOCK_BYTES;
            bridge_trace("USB auto unlock", unlock, sizeof(unlock));
            err = cdc_acm_host_data_tx_blocking(device, unlock, sizeof(unlock), CONFIG_TB_USB_TX_TIMEOUT_MS);
            if (err != ESP_OK) { atomic_store(&device_lost, true); reset_root_port(); }
#endif
        }
        uint32_t epoch;
        size_t length = bridge_take_usb_tx(tx, sizeof(tx), &epoch);
        if (!length) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }
        if (!bridge_epoch_valid(TB_DOWNLINK, epoch) || atomic_load(&device_lost)) continue;
        bridge_trace("USB TX", tx, length);
        esp_err_t err = cdc_acm_host_data_tx_blocking(device, tx, length, CONFIG_TB_USB_TX_TIMEOUT_MS);
        if (err == ESP_OK) { tx_timeouts = 0; bridge_record_usb_tx(length); }
        else {
            /* A timeout may have transmitted some bytes: do not replay blindly. */
            ESP_LOGW(TAG, "USB TX: %s", esp_err_to_name(err));
            ble_gatt_server_disconnect();
            if (err != ESP_ERR_TIMEOUT || ++tx_timeouts >= 3) {
                bridge_set_usb_ready(false);
                ESP_ERROR_CHECK(cdc_acm_host_close(device));
                device = NULL;
                candidate.accepted = false;
                tx_timeouts = 0;
                reset_root_port();
            }
        }
    }
}

esp_err_t usb_cdc_host_init(void)
{
    candidates = xQueueCreate(8, sizeof(usb_candidate_t));
    if (!candidates) return ESP_ERR_NO_MEM;
    const usb_host_config_t config = {.skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1};
    esp_err_t err = usb_host_install(&config);
    if (err != ESP_OK) return err;
    if (xTaskCreatePinnedToCore(usb_lib_task, "usb_lib_task", 4096, NULL, 20, NULL, 1) != pdPASS) return ESP_ERR_NO_MEM;
    const cdc_acm_host_driver_config_t driver = {
        .driver_task_stack_size = 4096, .driver_task_priority = 19, .xCoreID = 1, .new_dev_cb = new_device,
    };
    err = cdc_acm_host_install(&driver);
    if (err != ESP_OK) return err;
    return xTaskCreatePinnedToCore(usb_cdc_task, "usb_cdc_task", 4096, NULL, 18, NULL, 1) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
