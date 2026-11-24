#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t ble_gatt_server_init(void);
/* Only bridge_tx_task may call this; dispatches the operation on NimBLE host. */
int ble_gatt_server_notify(const uint8_t *data, size_t length, uint32_t epoch);
void ble_gatt_server_disconnect(void);
void ble_gatt_server_clear_bonds(void);
