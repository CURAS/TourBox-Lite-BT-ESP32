#pragma once
#include "bridge_core.h"
#include "esp_err.h"

esp_err_t proto_bridge_init(void);
void bridge_set_usb_ready(bool ready);
void bridge_set_usb_identity(uint16_t vid, uint16_t pid);
void bridge_set_ble_state(bool connected, bool subscribed);
void bridge_set_mtu(uint16_t mtu);
bool bridge_on_usb_rx(const uint8_t *data, size_t length);
esp_err_t bridge_on_ble_rx(const uint8_t *data, size_t length);
size_t bridge_take_usb_tx(uint8_t *output, size_t capacity, uint32_t *epoch);
bool bridge_epoch_valid(tb_direction_t direction, uint32_t epoch);
void bridge_record_usb_tx(size_t length);
void bridge_status_snapshot(tb_core_t *output);
void bridge_print_status(void);
void bridge_trace(const char *label, const uint8_t *data, size_t length);
