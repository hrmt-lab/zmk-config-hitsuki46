#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/* AI usage flag bits (RawHID Host AI Usage spec v1). */
#define HITSUKI46_AI_USAGE_FLAG_FIVE_HOUR_VALID  BIT(0)
#define HITSUKI46_AI_USAGE_FLAG_SEVEN_DAY_VALID  BIT(1)
#define HITSUKI46_AI_USAGE_FLAG_ESTIMATED        BIT(2)
#define HITSUKI46_AI_USAGE_FLAG_LOCAL_HISTORY    BIT(3)
#define HITSUKI46_AI_USAGE_FLAG_QUOTA_SOURCE     BIT(4)
#define HITSUKI46_AI_USAGE_FLAG_STALE            BIT(5)
#define HITSUKI46_AI_USAGE_FLAG_FALLBACK_LIMIT   BIT(6)
#define HITSUKI46_AI_USAGE_FLAG_ERROR_PRESENT    BIT(7)

/* Latest received state for a single provider.  Usage values are basis points
 * (10000 = 100.00%) already clamped to 0..10000 on receipt. */
struct hitsuki46_ai_usage_provider {
    bool present;
    uint8_t provider; /* 1 = codex, 2 = claude_code */
    uint8_t flags;
    uint16_t five_hour_used_bp;
    uint16_t seven_day_used_bp;
    uint32_t five_hour_reset_unix;
    uint32_t seven_day_reset_unix;
    uint32_t updated_unix;
    uint8_t error_code;
};

#if IS_ENABLED(CONFIG_HITSUKI46_RAW_HID_AI_USAGE)
/* Copy the latest state for `provider` (1=codex, 2=claude_code) into `out`.
 * Returns false (and leaves out->present = false) if nothing has been received
 * for that provider yet or the provider id is unknown. */
bool hitsuki46_raw_hid_ai_usage_get(uint8_t provider,
                                    struct hitsuki46_ai_usage_provider *out);
#else
static inline bool hitsuki46_raw_hid_ai_usage_get(uint8_t provider,
                                                  struct hitsuki46_ai_usage_provider *out) {
    ARG_UNUSED(provider);
    if (out != NULL) {
        out->present = false;
    }
    return false;
}
#endif
