#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_HITSUKI46_RAW_HID_TIME_SYNC)
bool hitsuki46_raw_hid_time_sync_format(char *buf, size_t len);
bool hitsuki46_raw_hid_time_sync_wants_seconds(void);
#else
static inline bool hitsuki46_raw_hid_time_sync_format(char *buf, size_t len) {
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    return false;
}

static inline bool hitsuki46_raw_hid_time_sync_wants_seconds(void) {
    return false;
}
#endif
