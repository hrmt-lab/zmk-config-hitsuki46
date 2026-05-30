#pragma once

#include <stdint.h>

#define HITSUKI46_RAW_HID_PACKET_SIZE 32
#define HITSUKI46_RAW_HID_MAX_LAYER 31

/* Packet types follow the RawHID Host AI Usage spec (v1).
 *
 * NOTE: this numbering replaces the earlier hitsuki46-only scheme where HELLO
 * was 0x10 and layer control used 0x01/0x02.  The host now sends:
 *   - HELLO probe on 0x01, device replies on 0x02
 *   - layer control via APP_LAYER (0x30) with an action byte
 *   - AI usage on 0x10
 */
enum hitsuki46_raw_hid_packet_type {
    HITSUKI46_RAW_HID_PACKET_HOST_HELLO = 0x01,
    HITSUKI46_RAW_HID_PACKET_DEVICE_HELLO = 0x02,
    HITSUKI46_RAW_HID_PACKET_ERROR = 0x03,
    HITSUKI46_RAW_HID_PACKET_PING = 0x04,
    HITSUKI46_RAW_HID_PACKET_PONG = 0x05,
    HITSUKI46_RAW_HID_PACKET_AI_USAGE = 0x10,
    HITSUKI46_RAW_HID_PACKET_TIME_SYNC = 0x20,
    HITSUKI46_RAW_HID_PACKET_APP_LAYER = 0x30,
};

enum hitsuki46_raw_hid_app_layer_action {
    HITSUKI46_RAW_HID_APP_LAYER_SET = 1,
    HITSUKI46_RAW_HID_APP_LAYER_CLEAR = 2,
};

enum hitsuki46_raw_hid_ai_provider {
    HITSUKI46_RAW_HID_AI_PROVIDER_CODEX = 1,
    HITSUKI46_RAW_HID_AI_PROVIDER_CLAUDE_CODE = 2,
};

enum hitsuki46_raw_hid_time_format {
    HITSUKI46_RAW_HID_TIME_FORMAT_TIME_HM = 0,
    HITSUKI46_RAW_HID_TIME_FORMAT_TIME_HMS = 1,
    HITSUKI46_RAW_HID_TIME_FORMAT_DATE_YMD = 2,
    HITSUKI46_RAW_HID_TIME_FORMAT_DATE_MD = 3,
    HITSUKI46_RAW_HID_TIME_FORMAT_DATETIME_HM = 4,
    HITSUKI46_RAW_HID_TIME_FORMAT_WEEKDAY_HM = 5,
};

enum hitsuki46_raw_hid_clock_mode {
    HITSUKI46_RAW_HID_CLOCK_24H = 0,
    HITSUKI46_RAW_HID_CLOCK_12H = 1,
};

struct hitsuki46_raw_hid_packet {
    enum hitsuki46_raw_hid_packet_type type;
    union {
        struct {
            uint8_t seq;
        } hello;
        struct {
            uint8_t action;
            uint8_t layer;
            uint8_t seq;
        } app_layer;
        struct {
            uint32_t unix_time_sec;
            int16_t tz_offset_min;
            uint8_t weekday;
            enum hitsuki46_raw_hid_time_format format_hint;
            enum hitsuki46_raw_hid_clock_mode clock_mode;
        } time_sync;
        struct {
            uint8_t provider;
            uint8_t flags;
            uint16_t five_hour_used_bp;
            uint16_t seven_day_used_bp;
            uint32_t five_hour_reset_unix;
            uint32_t seven_day_reset_unix;
            uint32_t updated_unix;
            uint8_t error_code;
        } ai_usage;
    };
};

void hitsuki46_raw_hid_layer_control_handle(const struct hitsuki46_raw_hid_packet *packet);
void hitsuki46_raw_hid_time_sync_handle(const struct hitsuki46_raw_hid_packet *packet);
void hitsuki46_raw_hid_ai_usage_handle(const struct hitsuki46_raw_hid_packet *packet);
