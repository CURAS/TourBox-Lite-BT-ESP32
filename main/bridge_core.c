#include "bridge_core.h"
#include <string.h>

static bool ring_put(tb_ring_t *ring, const uint8_t *data, size_t length)
{
    if (length > TB_BUFFER_SIZE - ring->length) return false;
    size_t tail = (ring->head + ring->length) % TB_BUFFER_SIZE;
    size_t first = TB_BUFFER_SIZE - tail;
    if (first > length) first = length;
    if (first) memcpy(ring->bytes + tail, data, first);
    if (length > first) memcpy(ring->bytes, data + first, length - first);
    ring->length += length;
    return true;
}

static bool emit(tb_core_t *core, tb_direction_t direction, const uint8_t *data, size_t length)
{
    if (!length) return true;
    if (direction == TB_UPLINK && !core->subscribed) {
        core->stats.dropped_up += length;
        return true;
    }
    return ring_put(direction == TB_UPLINK ? &core->up : &core->down, data, length);
}

static void clear_session(tb_core_t *core)
{
    core->up.head = core->up.length = core->down.head = core->down.length = 0;
    memset(core->prefixes, 0, sizeof(core->prefixes));
    ++core->epoch;
    ++core->up_epoch;
}

static bool rules_valid(const tb_rule_t *rules, size_t count)
{
    if (count && !rules) return false;
    for (size_t i = 0; i < count; ++i) {
        const tb_rule_t *rule = &rules[i];
        if (!rule->match || !rule->match_len || rule->match_len > TB_RULE_MAX_BYTES ||
            rule->output_len > TB_RULE_MAX_BYTES || (rule->output_len && !rule->output) ||
            rule->direction > TB_DOWNLINK || rule->action > TB_RULE_DROP ||
            (rule->action == TB_RULE_REPLY && rule->direction != TB_DOWNLINK)) return false;
        /* Prefix-overlapping rules are ambiguous; require disjoint patterns. */
        for (size_t j = 0; j < i; ++j) {
            size_t n = rule->match_len < rules[j].match_len ? rule->match_len : rules[j].match_len;
            bool overlap = rule->direction == rules[j].direction;
            for (size_t k = 0; k < n && overlap; ++k) {
                uint8_t mask_a = rule->mask ? rule->mask[k] : 0xff;
                uint8_t mask_b = rules[j].mask ? rules[j].mask[k] : 0xff;
                if ((rule->match[k] ^ rules[j].match[k]) & mask_a & mask_b) overlap = false;
            }
            if (overlap) return false;
        }
    }
    return true;
}

bool tb_core_set_rules(tb_core_t *core, const tb_rule_t *rules, size_t count)
{
    if (!rules_valid(rules, count)) return false;
    core->rules = rules;
    core->rule_count = count;
    clear_session(core);
    return true;
}

bool tb_core_init(tb_core_t *core, const tb_rule_t *rules, size_t count)
{
    memset(core, 0, sizeof(*core));
    core->mtu = 23;
    return tb_core_set_rules(core, rules, count);
}

static bool prefix_matches(const tb_rule_t *rule, const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if ((bytes[i] ^ rule->match[i]) & (rule->mask ? rule->mask[i] : 0xff)) return false;
    return true;
}

void tb_core_set_usb(tb_core_t *core, bool ready)
{
    if (core->usb_ready != ready) {
        core->usb_ready = ready;
        clear_session(core);
    }
}

void tb_core_set_ble(tb_core_t *core, bool connected, bool subscribed)
{
    subscribed = subscribed && connected;
    if (core->ble_connected != connected) {
        clear_session(core);
        core->mtu = 23;
    }
    if (core->subscribed != subscribed) {
        core->up.head = core->up.length = 0;
        memset(&core->prefixes[TB_UPLINK], 0, sizeof(tb_prefix_t));
        ++core->up_epoch;
    }
    core->ble_connected = connected;
    core->subscribed = subscribed;
}

void tb_core_set_mtu(tb_core_t *core, uint16_t mtu)
{
    core->mtu = mtu < 23 ? 23 : (mtu > TB_MAX_NOTIFY + 3 ? TB_MAX_NOTIFY + 3 : mtu);
}

static bool resolve_prefix(tb_core_t *core, tb_direction_t direction)
{
    tb_prefix_t *prefix = &core->prefixes[direction];
    while (prefix->length) {
        bool partial = false;
        for (size_t i = 0; i < core->rule_count; ++i) {
            const tb_rule_t *rule = &core->rules[i];
            if (rule->direction != direction || prefix->length > rule->match_len ||
                !prefix_matches(rule, prefix->bytes, prefix->length)) continue;
            if (prefix->length < rule->match_len) { partial = true; continue; }
            if (rule->action != TB_RULE_DROP &&
                !emit(core, rule->action == TB_RULE_REPLY ? TB_UPLINK : direction,
                      rule->output, rule->output_len)) return false;
            ++core->stats.rule_hits;
            prefix->length = 0;
            return true;
        }
        if (partial) return true;
        if (!emit(core, direction, prefix->bytes, 1)) return false;
        --prefix->length;
        memmove(prefix->bytes, prefix->bytes + 1, prefix->length);
    }
    return true;
}

bool tb_core_push(tb_core_t *core, tb_direction_t direction,
                  const uint8_t *data, size_t length, uint32_t now_ms)
{
    if (direction > TB_DOWNLINK || (!data && length)) return false;
    if (direction == TB_UPLINK) core->stats.usb_rx += length;
    else core->stats.ble_rx += length;
    if (!core->usb_ready || !core->ble_connected || (direction == TB_UPLINK && !core->subscribed)) {
        if (direction == TB_UPLINK) { core->stats.dropped_up += length; return true; }
        core->stats.rejected_down += length;
        return false;
    }
    /* Transaction: a rejected GATT write must not forward a truncated command.
     * Producer only touches unused ring space, so restoring lengths is enough. */
    const size_t up_length = core->up.length, down_length = core->down.length;
    const tb_prefix_t saved = core->prefixes[direction];
    const uint32_t hits = core->stats.rule_hits, drops = core->stats.dropped_up;
    bool ok = true;
    if (!core->rule_count) ok = emit(core, direction, data, length);
    else {
        tb_prefix_t *prefix = &core->prefixes[direction];
        for (size_t i = 0; i < length && ok; ++i) {
            prefix->bytes[prefix->length++] = data[i];
            prefix->last_ms = now_ms;
            ok = resolve_prefix(core, direction);
        }
    }
    if (!ok) {
        core->up.length = up_length;
        core->down.length = down_length;
        core->prefixes[direction] = saved;
        core->stats.rule_hits = hits;
        core->stats.dropped_up = drops;
        if (direction == TB_UPLINK) core->stats.dropped_up += length;
        else core->stats.rejected_down += length;
    }
    return ok;
}

void tb_core_flush(tb_core_t *core, uint32_t now_ms, uint32_t timeout_ms)
{
    for (unsigned i = 0; i < 2; ++i) {
        tb_prefix_t *prefix = &core->prefixes[i];
        if (prefix->length && (uint32_t)(now_ms - prefix->last_ms) >= timeout_ms &&
            emit(core, (tb_direction_t)i, prefix->bytes, prefix->length)) prefix->length = 0;
    }
}

size_t tb_core_take(tb_core_t *core, tb_direction_t direction,
                    uint8_t *output, size_t capacity, uint32_t *epoch)
{
    if (!core->usb_ready || !core->ble_connected ||
        (direction == TB_UPLINK && !core->subscribed)) return 0;
    tb_ring_t *ring = direction == TB_UPLINK ? &core->up : &core->down;
    if (direction == TB_UPLINK && capacity > core->mtu - 3u) capacity = core->mtu - 3u;
    size_t length = ring->length < capacity ? ring->length : capacity;
    size_t first = TB_BUFFER_SIZE - ring->head;
    if (first > length) first = length;
    if (first) memcpy(output, ring->bytes + ring->head, first);
    if (length > first) memcpy(output + first, ring->bytes, length - first);
    ring->head = (ring->head + length) % TB_BUFFER_SIZE;
    ring->length -= length;
    *epoch = direction == TB_UPLINK ? core->up_epoch : core->epoch;
    return length;
}

bool tb_core_epoch_valid(const tb_core_t *core, tb_direction_t direction, uint32_t epoch)
{
    return core->usb_ready && core->ble_connected &&
        (direction == TB_UPLINK ? core->subscribed && epoch == core->up_epoch : epoch == core->epoch);
}
