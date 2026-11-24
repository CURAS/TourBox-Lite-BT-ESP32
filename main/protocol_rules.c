#include "bridge_core.h"

/* Add only rules confirmed by a Console/real-device capture here.
 * Match and output arrays must live for the entire firmware lifetime.
 * TB_RULE_REPLY consumes a complete downlink pattern and emits output upstream.
 * TB_RULE_REPLACE rewrites a complete pattern in its original direction.
 * TB_RULE_DROP consumes a pattern without forwarding it.
 * Patterns span USB/GATT packet boundaries; unmatched bytes pass unchanged.
 * See tests/test_bridge_core.c for examples using synthetic test patterns.
 */
const tb_rule_t *const tb_protocol_rules = NULL;
const size_t tb_protocol_rule_count = 0;
