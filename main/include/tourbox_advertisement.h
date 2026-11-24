#pragma once
#include <stdint.h>

#define TB_CONSOLE_MANUFACTURER_SIZE 16

/* NimBLE address bytes are little endian; Console's manufacturer payload uses
 * display order. Last six zero bytes identify an undirected pairing advert. */
void tb_console_manufacturer_data(uint8_t output[TB_CONSOLE_MANUFACTURER_SIZE],
                                  const uint8_t nimble_address[6]);
void tb_console_manufacturer_data_for_peer(uint8_t output[TB_CONSOLE_MANUFACTURER_SIZE],
                                           const uint8_t nimble_address[6], const uint8_t peer_address[6]);
