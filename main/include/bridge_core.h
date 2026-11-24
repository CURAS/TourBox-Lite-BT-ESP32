#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tourbox_proto.h"

typedef enum { TB_UPLINK, TB_DOWNLINK } tb_direction_t;
typedef enum { TB_RULE_REPLACE, TB_RULE_REPLY, TB_RULE_DROP } tb_rule_action_t;
typedef struct {
    tb_direction_t direction;
    const uint8_t *match;
    size_t match_len;
    tb_rule_action_t action;
    const uint8_t *output;
    size_t output_len;
    const uint8_t *mask; /* NULL = exact bytes; 0 bits are ignored when matching. */
} tb_rule_t;
typedef struct {
    uint8_t bytes[TB_BUFFER_SIZE];
    size_t head, length;
} tb_ring_t;
typedef struct {
    uint8_t bytes[TB_RULE_MAX_BYTES];
    size_t length;
    uint32_t last_ms;
} tb_prefix_t;
typedef struct {
    uint32_t usb_rx, ble_rx, ble_tx, usb_tx;
    uint32_t dropped_up, rejected_down, rule_hits, notify_failures;
} tb_stats_t;
typedef struct {
    tb_ring_t up, down;
    tb_prefix_t prefixes[2];
    const tb_rule_t *rules;
    size_t rule_count;
    bool usb_ready, ble_connected, subscribed;
    uint16_t mtu;
    uint32_t epoch, up_epoch;
    tb_stats_t stats;
} tb_core_t;

/* All calls require external serialization; core has no RTOS dependency. */
bool tb_core_init(tb_core_t *core, const tb_rule_t *rules, size_t count);
bool tb_core_set_rules(tb_core_t *core, const tb_rule_t *rules, size_t count);
void tb_core_set_usb(tb_core_t *core, bool ready);
void tb_core_set_ble(tb_core_t *core, bool connected, bool subscribed);
void tb_core_set_mtu(tb_core_t *core, uint16_t mtu);
bool tb_core_push(tb_core_t *core, tb_direction_t direction,
                  const uint8_t *data, size_t length, uint32_t now_ms);
void tb_core_flush(tb_core_t *core, uint32_t now_ms, uint32_t timeout_ms);
size_t tb_core_take(tb_core_t *core, tb_direction_t direction,
                    uint8_t *output, size_t capacity, uint32_t *epoch);
bool tb_core_epoch_valid(const tb_core_t *core, tb_direction_t direction, uint32_t epoch);
extern const tb_rule_t *const tb_protocol_rules;
extern const size_t tb_protocol_rule_count;
