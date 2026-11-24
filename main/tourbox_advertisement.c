#include "tourbox_advertisement.h"
#include <string.h>

void tb_console_manufacturer_data(uint8_t output[TB_CONSOLE_MANUFACTURER_SIZE],
                                  const uint8_t nimble_address[6])
{
    tb_console_manufacturer_data_for_peer(output, nimble_address, NULL);
}

void tb_console_manufacturer_data_for_peer(uint8_t output[TB_CONSOLE_MANUFACTURER_SIZE],
                                           const uint8_t nimble_address[6], const uint8_t peer_address[6])
{
    /* Confirmed from the installed Console's BLE discovery parser:
     * TBE<space> | device address (6 bytes) | target PC address (6 bytes).
     * The prefix already occupies the vendor-specific AD payload; do not
     * prepend an Espressif company ID, which would shift the parser fields. */
    memcpy(output, "TBE ", 4);
    for (unsigned i = 0; i < 6; ++i) output[4 + i] = nimble_address[5 - i];
    if (peer_address) memcpy(output + 10, peer_address, 6);
    else memset(output + 10, 0, 6);
}
