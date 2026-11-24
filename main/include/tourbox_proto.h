#pragma once
#include <stdint.h>

/* Verified against AndyCappDev/tuxbox tag v3.8.1, device_{usb,ble}.py.
 * The BLE driver identifies a TourBox name prefix, not a specific Lite name.
 * STM32 IDs below come from source; the other IDs come from the design doc. */
#define TB_SERVICE_UUID "0000fff0-0000-1000-8000-00805f9b34fb"
#define TB_NOTIFY_UUID  "0000fff1-0000-1000-8000-00805f9b34fb"
#define TB_WRITE_UUID   "0000fff2-0000-1000-8000-00805f9b34fb"
#define TB_UNLOCK_BYTES {0x55, 0x00, 0x07, 0x88, 0x94, 0x00, 0x1a, 0xfe}
#define TB_MAX_NOTIFY 244
#define TB_MAX_WRITE 512
#define TB_BUFFER_SIZE 2048
#define TB_RULE_MAX_BYTES 128
#define TB_USB_VID_OBSERVED_LITE 0xcafe
#define TB_USB_PID_OBSERVED_LITE 0x4001
#define TB_USB_VID_STM32 0x0483
#define TB_USB_PID_ELITE 0x5741
#define TB_USB_PID_AMBIGUOUS 0x5740
#define TB_USB_VID_C251 0xc251
#define TB_USB_PID_2005 0x2005
#define TB_USB_VID_ARTERY 0x2e3c
#define TB_USB_PID_ARTERY 0x5740
