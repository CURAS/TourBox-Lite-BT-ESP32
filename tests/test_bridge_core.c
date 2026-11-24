#include "bridge_core.h"
#include "tourbox_advertisement.h"
#include "lite_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static tb_core_t core;
static uint8_t out[TB_BUFFER_SIZE];

static void connected(const tb_rule_t *rules, size_t count)
{
    assert(tb_core_init(&core, rules, count));
    tb_core_set_usb(&core, true);
    tb_core_set_ble(&core, true, true);
}

static size_t take(tb_direction_t direction)
{
    uint32_t epoch = 0;
    size_t length = tb_core_take(&core, direction, out, sizeof(out), &epoch);
    if (length) assert(tb_core_epoch_valid(&core, direction, epoch));
    return length;
}

static void test_transparent_and_mtu(void)
{
    connected(NULL, 0);
    const uint8_t unlock[] = TB_UNLOCK_BYTES;
    assert(tb_core_push(&core, TB_DOWNLINK, unlock, sizeof(unlock), 0));
    assert(take(TB_DOWNLINK) == sizeof(unlock));
    assert(!memcmp(out, unlock, sizeof(unlock)));
    uint8_t events[256];
    for (size_t i = 0; i < sizeof(events); ++i) events[i] = i;
    assert(tb_core_push(&core, TB_UPLINK, events, sizeof(events), 0));
    assert(take(TB_UPLINK) == 20);
    assert(!memcmp(out, events, 20));
    tb_core_set_mtu(&core, 247);
    assert(take(TB_UPLINK) == 236);
    assert(!memcmp(out, events + 20, 236));
    tb_core_set_mtu(&core, 517);
    assert(core.mtu == 247);
    tb_core_set_mtu(&core, 0);
    assert(core.mtu == 23);
    const uint8_t one = 0x81;
    assert(tb_core_push(&core, TB_UPLINK, &one, 1, 0));
    assert(take(TB_UPLINK) == 1 && out[0] == one);
    puts("PASS: all byte values, unlock, single-byte events, MTU limits");
}

static void test_sessions(void)
{
    connected(NULL, 0);
    const uint8_t event[] = {1, 0x81};
    assert(tb_core_push(&core, TB_UPLINK, event, sizeof(event), 0));
    uint32_t old_epoch;
    assert(tb_core_take(&core, TB_UPLINK, out, sizeof(out), &old_epoch) == 2);
    tb_core_set_ble(&core, false, false);
    tb_core_set_ble(&core, true, false);
    assert(!tb_core_epoch_valid(&core, TB_UPLINK, old_epoch));
    assert(tb_core_push(&core, TB_UPLINK, event, sizeof(event), 0));
    /* Writes before subscription are preserved, input before it is discarded. */
    assert(tb_core_push(&core, TB_DOWNLINK, event, sizeof(event), 0));
    tb_core_set_ble(&core, true, true);
    assert(!take(TB_UPLINK));
    assert(take(TB_DOWNLINK) == sizeof(event));
    assert(tb_core_push(&core, TB_UPLINK, event, sizeof(event), 0));
    tb_core_set_ble(&core, true, false);
    tb_core_set_ble(&core, true, true);
    assert(!take(TB_UPLINK));
    assert(tb_core_push(&core, TB_DOWNLINK, event, sizeof(event), 0));
    uint32_t old_down_epoch;
    assert(tb_core_take(&core, TB_DOWNLINK, out, sizeof(out), &old_down_epoch) == 2);
    tb_core_set_usb(&core, false);
    assert(!tb_core_push(&core, TB_DOWNLINK, event, sizeof(event), 0));
    tb_core_set_usb(&core, true);
    assert(!tb_core_epoch_valid(&core, TB_DOWNLINK, old_down_epoch));
    assert(!take(TB_DOWNLINK) && !take(TB_UPLINK));
    puts("PASS: disconnect/reconnect, USB removal, CCCD changes, stale packet rejection");
}

static void test_overflow_atomicity(void)
{
    connected(NULL, 0);
    uint8_t bytes[TB_BUFFER_SIZE];
    memset(bytes, 0x51, sizeof(bytes));
    const uint8_t command[] = {0x55, 0x00, 0x07, 0x88};
    assert(tb_core_push(&core, TB_DOWNLINK, bytes, sizeof(bytes) - 2, 0));
    assert(!tb_core_push(&core, TB_DOWNLINK, command, sizeof(command), 0));
    assert(take(TB_DOWNLINK) == sizeof(bytes) - 2);
    for (size_t i = 0; i < sizeof(bytes) - 2; ++i) assert(out[i] == 0x51);
    assert(!take(TB_DOWNLINK));
    assert(tb_core_push(&core, TB_UPLINK, bytes, sizeof(bytes), 0));
    assert(!tb_core_push(&core, TB_UPLINK, command, sizeof(command), 0));
    assert(core.up.length == TB_BUFFER_SIZE);
    puts("PASS: queue overflow rejects whole commands and preserves buffered data");
}

/* Synthetic patterns: these are tests, not TourBox protocol constants. */
static const uint8_t query[] = {0xaa, 0xbb, 0xcc};
static const uint8_t reply[] = {0x01, 0x64};
static const uint8_t old_model[] = {0xde, 0xad, 0xbe, 0xef};
static const uint8_t new_model[] = {0xca, 0xfe};
static const uint8_t ignored[] = {0xf0, 0x0f};
static const tb_rule_t rules[] = {
    {TB_DOWNLINK, query, sizeof(query), TB_RULE_REPLY, reply, sizeof(reply), NULL},
    {TB_UPLINK, old_model, sizeof(old_model), TB_RULE_REPLACE, new_model, sizeof(new_model), NULL},
    {TB_DOWNLINK, ignored, sizeof(ignored), TB_RULE_DROP, NULL, 0, NULL},
};

static void test_rules_fragmentation(void)
{
    const uint8_t combined[] = {0x11, 0xaa, 0xbb, 0xcc, 0xf0, 0x0f, 0x22, 0xaa, 0xbb, 0xcc, 0x33};
    const uint8_t expected[] = {0x11, 0x22, 0x33};
    for (size_t split = 0; split <= sizeof(combined); ++split) {
        connected(rules, 3);
        assert(tb_core_push(&core, TB_DOWNLINK, combined, split, 0));
        assert(tb_core_push(&core, TB_DOWNLINK, combined + split, sizeof(combined) - split, 1));
        assert(take(TB_DOWNLINK) == sizeof(expected));
        assert(!memcmp(out, expected, sizeof(expected)));
        assert(take(TB_UPLINK) == sizeof(reply) * 2);
        assert(!memcmp(out, reply, sizeof(reply)) && !memcmp(out + sizeof(reply), reply, sizeof(reply)));
    }
    for (size_t split = 0; split <= sizeof(old_model); ++split) {
        connected(rules, 3);
        assert(tb_core_push(&core, TB_UPLINK, old_model, split, 0));
        assert(tb_core_push(&core, TB_UPLINK, old_model + split, sizeof(old_model) - split, 1));
        assert(take(TB_UPLINK) == sizeof(new_model) && !memcmp(out, new_model, sizeof(new_model)));
    }
    connected(rules, 3);
    const uint8_t mismatch[] = {0xaa, 0xaa, 0xbb, 0x17};
    for (size_t i = 0; i < sizeof(mismatch); ++i) assert(tb_core_push(&core, TB_DOWNLINK, mismatch + i, 1, 0));
    assert(take(TB_DOWNLINK) == sizeof(mismatch) && !memcmp(out, mismatch, sizeof(mismatch)));
    puts("PASS: query replies, response rewriting, drop rules, all fragment splits, prefix mismatch");
}

static void test_prefix_timeout_and_transaction(void)
{
    connected(rules, 3);
    assert(tb_core_push(&core, TB_DOWNLINK, query, 2, UINT32_MAX - 5));
    tb_core_flush(&core, 5, 20);
    assert(!take(TB_DOWNLINK));
    tb_core_flush(&core, 15, 20);
    assert(take(TB_DOWNLINK) == 2 && !memcmp(out, query, 2));
    assert(tb_core_push(&core, TB_DOWNLINK, query, 2, 20));
    tb_core_set_usb(&core, false);
    tb_core_set_usb(&core, true);
    assert(tb_core_push(&core, TB_DOWNLINK, query + 2, 1, 21));
    assert(take(TB_DOWNLINK) == 1 && out[0] == query[2]);
    assert(!take(TB_UPLINK));
    connected(rules, 3);
    uint8_t fill[TB_BUFFER_SIZE];
    memset(fill, 0x51, sizeof(fill));
    assert(tb_core_push(&core, TB_UPLINK, fill, sizeof(fill) - 1, 0));
    assert(tb_core_push(&core, TB_DOWNLINK, query, 2, 0));
    assert(!tb_core_push(&core, TB_DOWNLINK, query + 2, 1, 0));
    assert(core.prefixes[TB_DOWNLINK].length == 2 && !core.stats.rule_hits);
    assert(take(TB_UPLINK) == 20);
    assert(tb_core_push(&core, TB_DOWNLINK, query + 2, 1, 0));
    assert(core.stats.rule_hits == 1);
    tb_rule_t ambiguous[] = {rules[0], rules[0]};
    ambiguous[1].match_len = 2;
    assert(!tb_core_init(&core, ambiguous, 2));
    puts("PASS: prefix timeout including clock wrap, session reset, rule reply backpressure rollback");
}

static uint32_t random_state = 0x19a50cda;
static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void test_random_queue_oracle(void)
{
    connected(NULL, 0);
    uint8_t expected[TB_BUFFER_SIZE], bytes[511];
    size_t buffered = 0;
    for (unsigned iteration = 0; iteration < 50000; ++iteration) {
        if (next_random() % 3) {
            size_t length = next_random() % sizeof(bytes);
            for (size_t i = 0; i < length; ++i) bytes[i] = next_random();
            bool fits = buffered + length <= sizeof(expected);
            assert(tb_core_push(&core, TB_DOWNLINK, bytes, length, iteration) == fits);
            if (fits) { memcpy(expected + buffered, bytes, length); buffered += length; }
        } else {
            size_t capacity = next_random() % sizeof(bytes), expected_len = buffered < capacity ? buffered : capacity;
            uint32_t epoch;
            assert(tb_core_take(&core, TB_DOWNLINK, out, capacity, &epoch) == expected_len);
            assert(!memcmp(out, expected, expected_len));
            memmove(expected, expected + expected_len, buffered - expected_len);
            buffered -= expected_len;
        }
        assert(core.down.length == buffered);
    }
    puts("PASS: 50,000 randomized queue operations checked against FIFO oracle");
}

static size_t read_hex_fixture(const char *name, uint8_t *output, size_t capacity)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", TB_TEST_FIXTURE_DIR, name);
    FILE *file = fopen(path, "r");
    assert(file);
    size_t length = 0;
    unsigned byte;
    while (fscanf(file, "%x", &byte) == 1) {
        assert(length < capacity && byte < 256);
        output[length++] = byte;
    }
    fclose(file);
    return length;
}

static void test_captured_lite_configuration(void)
{
    uint8_t console[TB_LITE_CONFIG_SIZE], expected[TB_LITE_CONFIG_SIZE];
    assert(read_hex_fixture("console-b5-nonzero.hex", console, sizeof(console)) == sizeof(console));
    assert(read_hex_fixture("lite-config-zero.hex", expected, sizeof(expected)) == sizeof(expected));
    for (size_t split = 0; split <= sizeof(console); ++split) {
        connected(&tb_lite_zero_config_rule, 1);
        assert(tb_core_push(&core, TB_DOWNLINK, console, split, 0));
        assert(tb_core_push(&core, TB_DOWNLINK, console + split, sizeof(console) - split, 30));
        assert(take(TB_DOWNLINK) == sizeof(expected));
        assert(!memcmp(out, expected, sizeof(expected)));
        assert(core.stats.rule_hits == 1);
    }
    connected(&tb_lite_zero_config_rule, 1);
    for (size_t pos = 0; pos < sizeof(console); pos += 20) {
        size_t length = sizeof(console) - pos < 20 ? sizeof(console) - pos : 20;
        assert(tb_core_push(&core, TB_DOWNLINK, console + pos, length, (uint32_t)pos * 3 / 2));
        tb_core_flush(&core, (uint32_t)pos * 3 / 2 + 20, 250);
        if (pos + length < sizeof(console)) assert(!take(TB_DOWNLINK));
    }
    assert(take(TB_DOWNLINK) == sizeof(expected) && !memcmp(out, expected, sizeof(expected)));
    /* Unknown layouts pass unchanged, including all original parameter values. */
    connected(&tb_lite_zero_config_rule, 1);
    console[5] ^= 1;
    assert(tb_core_push(&core, TB_DOWNLINK, console, sizeof(console), 0));
    assert(take(TB_DOWNLINK) == sizeof(console) && !memcmp(out, console, sizeof(console)));
    console[5] ^= 1;
    /* Another USB profile disables normalization and clears incomplete frames. */
    assert(tb_core_push(&core, TB_DOWNLINK, console, 20, 0));
    assert(tb_core_set_rules(&core, NULL, 0));
    assert(tb_core_push(&core, TB_DOWNLINK, console, sizeof(console), 0));
    assert(take(TB_DOWNLINK) == sizeof(console) && !memcmp(out, console, sizeof(console)));
    connected(&tb_lite_zero_config_rule, 1);
    assert(tb_core_push(&core, TB_DOWNLINK, console, 20, 0));
    tb_core_flush(&core, 249, 250);
    assert(!take(TB_DOWNLINK));
    tb_core_flush(&core, 250, 250);
    assert(take(TB_DOWNLINK) == 20 && !memcmp(out, console, 20));
    /* Recorded rotary step codes and a modifier/release retain their order. */
    const uint8_t events[] = {0x00, 0x44, 0x44, 0x04, 0x04, 0x80, 0x02, 0x49, 0x09, 0x82};
    assert(tb_core_push(&core, TB_UPLINK, events, sizeof(events), 300));
    assert(take(TB_UPLINK) == sizeof(events) && !memcmp(out, events, sizeof(events)));
    puts("PASS: real Console configuration replay, every split, 30ms fragments, unknown layouts, profile change, rotary ordering");
}

int main(void)
{
    const uint8_t identity[] = {0xde, 0xc8, 0x97, 0x1b, 0x5b, 0xf8};
    const uint8_t expected_manufacturer[] = {
        0x54, 0x42, 0x45, 0x20, 0xf8, 0x5b, 0x1b, 0x97, 0xc8, 0xde,
        0, 0, 0, 0, 0, 0
    };
    uint8_t manufacturer[TB_CONSOLE_MANUFACTURER_SIZE];
    tb_console_manufacturer_data(manufacturer, identity);
    assert(!memcmp(manufacturer, expected_manufacturer, sizeof(manufacturer)));
    const uint8_t target_pc[] = {0xd0, 0x11, 0xe5, 0x6e, 0xd1, 0xb3};
    tb_console_manufacturer_data_for_peer(manufacturer, identity, target_pc);
    assert(!memcmp(manufacturer, expected_manufacturer, 10));
    assert(!memcmp(manufacturer + 10, target_pc, sizeof(target_pc)));
    puts("PASS: Console discovery signature, display-order device address, undirected target host");
    test_transparent_and_mtu();
    test_sessions();
    test_overflow_atomicity();
    test_rules_fragmentation();
    test_prefix_timeout_and_transaction();
    test_random_queue_oracle();
    test_captured_lite_configuration();
    return 0;
}
