#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <raw_hid/events.h>

#include <zmk/event_manager.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "hitsuki46_raw_hid.h"

#define HITSUKI46_RAW_HID_MAGIC_0 'H'
#define HITSUKI46_RAW_HID_MAGIC_1 'L'
#define HITSUKI46_RAW_HID_VERSION 0x01

#define HITSUKI46_RAW_HID_OFFSET_MAGIC_0 0
#define HITSUKI46_RAW_HID_OFFSET_MAGIC_1 1
#define HITSUKI46_RAW_HID_OFFSET_VERSION 2
#define HITSUKI46_RAW_HID_OFFSET_TYPE 3

/* HELLO (host/device): reserved 4..6, seq at 7, reserved 8..31. */
#define HITSUKI46_RAW_HID_HELLO_RESERVED_A_START 4
#define HITSUKI46_RAW_HID_HELLO_RESERVED_A_END 6
#define HITSUKI46_RAW_HID_HELLO_SEQ 7
#define HITSUKI46_RAW_HID_HELLO_RESERVED_B_START 8

/* APP_LAYER: action at 4, layer at 5, reserved 6, seq 7, reserved 8..31. */
#define HITSUKI46_RAW_HID_APP_LAYER_ACTION 4
#define HITSUKI46_RAW_HID_APP_LAYER_LAYER 5
#define HITSUKI46_RAW_HID_APP_LAYER_RESERVED 6
#define HITSUKI46_RAW_HID_APP_LAYER_SEQ 7
#define HITSUKI46_RAW_HID_APP_LAYER_RESERVED_B_START 8

/* TIME_SYNC payload offsets (unchanged from the earlier scheme). */
#define HITSUKI46_RAW_HID_TIME_SYNC_UNIX_TIME_SEC 4
#define HITSUKI46_RAW_HID_TIME_SYNC_TZ_OFFSET_MIN 8
#define HITSUKI46_RAW_HID_TIME_SYNC_WEEKDAY 10
#define HITSUKI46_RAW_HID_TIME_SYNC_FORMAT_HINT 11
#define HITSUKI46_RAW_HID_TIME_SYNC_CLOCK_MODE 12
#define HITSUKI46_RAW_HID_TIME_SYNC_RESERVED_START 13

/* AI_USAGE payload offsets. */
#define HITSUKI46_RAW_HID_AI_PROVIDER 4
#define HITSUKI46_RAW_HID_AI_FLAGS 5
#define HITSUKI46_RAW_HID_AI_FIVE_HOUR_USED_BP 6
#define HITSUKI46_RAW_HID_AI_SEVEN_DAY_USED_BP 8
#define HITSUKI46_RAW_HID_AI_FIVE_HOUR_RESET 10
#define HITSUKI46_RAW_HID_AI_SEVEN_DAY_RESET 14
#define HITSUKI46_RAW_HID_AI_UPDATED 18
#define HITSUKI46_RAW_HID_AI_ERROR_CODE 22
#define HITSUKI46_RAW_HID_AI_RESERVED_START 23

BUILD_ASSERT(CONFIG_RAW_HID_REPORT_SIZE == HITSUKI46_RAW_HID_PACKET_SIZE,
             "hitsuki46 Raw HID requires 32 byte reports");

static uint8_t hello_response[HITSUKI46_RAW_HID_PACKET_SIZE];

static bool reserved_bytes_are_zero(const uint8_t *data, uint8_t start, uint8_t end_inclusive) {
    for (uint8_t i = start; i <= end_inclusive; i++) {
        if (data[i] != 0) {
            return false;
        }
    }

    return true;
}

static bool packet_type_is_known(uint8_t packet_type) {
    switch (packet_type) {
    case HITSUKI46_RAW_HID_PACKET_HOST_HELLO:
    case HITSUKI46_RAW_HID_PACKET_DEVICE_HELLO:
    case HITSUKI46_RAW_HID_PACKET_ERROR:
    case HITSUKI46_RAW_HID_PACKET_PING:
    case HITSUKI46_RAW_HID_PACKET_PONG:
    case HITSUKI46_RAW_HID_PACKET_AI_USAGE:
    case HITSUKI46_RAW_HID_PACKET_TIME_SYNC:
    case HITSUKI46_RAW_HID_PACKET_APP_LAYER:
        return true;
    default:
        return false;
    }
}

static bool parse_hello_packet(const uint8_t *data, struct hitsuki46_raw_hid_packet *packet) {
    if (!reserved_bytes_are_zero(data, HITSUKI46_RAW_HID_HELLO_RESERVED_A_START,
                                 HITSUKI46_RAW_HID_HELLO_RESERVED_A_END) ||
        !reserved_bytes_are_zero(data, HITSUKI46_RAW_HID_HELLO_RESERVED_B_START,
                                 HITSUKI46_RAW_HID_PACKET_SIZE - 1)) {
        return false;
    }

    packet->hello.seq = data[HITSUKI46_RAW_HID_HELLO_SEQ];
    return true;
}

static bool parse_app_layer_packet(const uint8_t *data, struct hitsuki46_raw_hid_packet *packet) {
    uint8_t action = data[HITSUKI46_RAW_HID_APP_LAYER_ACTION];
    uint8_t layer = data[HITSUKI46_RAW_HID_APP_LAYER_LAYER];

    if (action != HITSUKI46_RAW_HID_APP_LAYER_SET &&
        action != HITSUKI46_RAW_HID_APP_LAYER_CLEAR) {
        return false;
    }

    if (action == HITSUKI46_RAW_HID_APP_LAYER_SET && layer > HITSUKI46_RAW_HID_MAX_LAYER) {
        return false;
    }

    if (data[HITSUKI46_RAW_HID_APP_LAYER_RESERVED] != 0 ||
        !reserved_bytes_are_zero(data, HITSUKI46_RAW_HID_APP_LAYER_RESERVED_B_START,
                                 HITSUKI46_RAW_HID_PACKET_SIZE - 1)) {
        return false;
    }

    packet->app_layer.action = action;
    packet->app_layer.layer = layer;
    packet->app_layer.seq = data[HITSUKI46_RAW_HID_APP_LAYER_SEQ];
    return true;
}

static bool parse_time_sync_packet(const uint8_t *data, struct hitsuki46_raw_hid_packet *packet) {
    uint8_t weekday = data[HITSUKI46_RAW_HID_TIME_SYNC_WEEKDAY];
    uint8_t format_hint = data[HITSUKI46_RAW_HID_TIME_SYNC_FORMAT_HINT];
    uint8_t clock_mode = data[HITSUKI46_RAW_HID_TIME_SYNC_CLOCK_MODE];

    if (weekday < 1 || weekday > 7) {
        return false;
    }

    if (!reserved_bytes_are_zero(data, HITSUKI46_RAW_HID_TIME_SYNC_RESERVED_START,
                                 HITSUKI46_RAW_HID_PACKET_SIZE - 1)) {
        return false;
    }

    if (format_hint > HITSUKI46_RAW_HID_TIME_FORMAT_WEEKDAY_HM) {
        format_hint = HITSUKI46_RAW_HID_TIME_FORMAT_TIME_HM;
    }

    if (clock_mode > HITSUKI46_RAW_HID_CLOCK_12H) {
        clock_mode = HITSUKI46_RAW_HID_CLOCK_24H;
    }

    packet->time_sync.unix_time_sec =
        sys_get_le32(&data[HITSUKI46_RAW_HID_TIME_SYNC_UNIX_TIME_SEC]);
    packet->time_sync.tz_offset_min =
        (int16_t)sys_get_le16(&data[HITSUKI46_RAW_HID_TIME_SYNC_TZ_OFFSET_MIN]);
    packet->time_sync.weekday = weekday;
    packet->time_sync.format_hint = (enum hitsuki46_raw_hid_time_format)format_hint;
    packet->time_sync.clock_mode = (enum hitsuki46_raw_hid_clock_mode)clock_mode;
    return true;
}

static bool parse_ai_usage_packet(const uint8_t *data, struct hitsuki46_raw_hid_packet *packet) {
    uint8_t provider = data[HITSUKI46_RAW_HID_AI_PROVIDER];

    if (provider != HITSUKI46_RAW_HID_AI_PROVIDER_CODEX &&
        provider != HITSUKI46_RAW_HID_AI_PROVIDER_CLAUDE_CODE) {
        return false;
    }

    if (!reserved_bytes_are_zero(data, HITSUKI46_RAW_HID_AI_RESERVED_START,
                                 HITSUKI46_RAW_HID_PACKET_SIZE - 1)) {
        return false;
    }

    uint16_t five_hour_bp = sys_get_le16(&data[HITSUKI46_RAW_HID_AI_FIVE_HOUR_USED_BP]);
    uint16_t seven_day_bp = sys_get_le16(&data[HITSUKI46_RAW_HID_AI_SEVEN_DAY_USED_BP]);

    packet->ai_usage.provider = provider;
    packet->ai_usage.flags = data[HITSUKI46_RAW_HID_AI_FLAGS];
    packet->ai_usage.five_hour_used_bp = MIN(five_hour_bp, 10000);
    packet->ai_usage.seven_day_used_bp = MIN(seven_day_bp, 10000);
    packet->ai_usage.five_hour_reset_unix = sys_get_le32(&data[HITSUKI46_RAW_HID_AI_FIVE_HOUR_RESET]);
    packet->ai_usage.seven_day_reset_unix = sys_get_le32(&data[HITSUKI46_RAW_HID_AI_SEVEN_DAY_RESET]);
    packet->ai_usage.updated_unix = sys_get_le32(&data[HITSUKI46_RAW_HID_AI_UPDATED]);
    packet->ai_usage.error_code = data[HITSUKI46_RAW_HID_AI_ERROR_CODE];
    return true;
}

static bool parse_packet(const struct raw_hid_received_event *event,
                         struct hitsuki46_raw_hid_packet *packet) {
    if (event == NULL || event->data == NULL || event->length != HITSUKI46_RAW_HID_PACKET_SIZE) {
        return false;
    }

    const uint8_t *data = event->data;

    if (data[HITSUKI46_RAW_HID_OFFSET_MAGIC_0] != HITSUKI46_RAW_HID_MAGIC_0 ||
        data[HITSUKI46_RAW_HID_OFFSET_MAGIC_1] != HITSUKI46_RAW_HID_MAGIC_1) {
        return false;
    }

    if (data[HITSUKI46_RAW_HID_OFFSET_VERSION] != HITSUKI46_RAW_HID_VERSION) {
        return false;
    }

    if (!packet_type_is_known(data[HITSUKI46_RAW_HID_OFFSET_TYPE])) {
        return false;
    }

    packet->type = data[HITSUKI46_RAW_HID_OFFSET_TYPE];

    switch (packet->type) {
    case HITSUKI46_RAW_HID_PACKET_HOST_HELLO:
    case HITSUKI46_RAW_HID_PACKET_DEVICE_HELLO:
        return parse_hello_packet(data, packet);
    case HITSUKI46_RAW_HID_PACKET_APP_LAYER:
        return parse_app_layer_packet(data, packet);
    case HITSUKI46_RAW_HID_PACKET_TIME_SYNC:
        return parse_time_sync_packet(data, packet);
    case HITSUKI46_RAW_HID_PACKET_AI_USAGE:
        return parse_ai_usage_packet(data, packet);
    /* ERROR / PING / PONG are reserved in v1: accepted but not acted upon. */
    case HITSUKI46_RAW_HID_PACKET_ERROR:
    case HITSUKI46_RAW_HID_PACKET_PING:
    case HITSUKI46_RAW_HID_PACKET_PONG:
        return false;
    default:
        return false;
    }
}

static void send_device_hello(uint8_t seq) {
    memset(hello_response, 0, sizeof(hello_response));
    hello_response[HITSUKI46_RAW_HID_OFFSET_MAGIC_0] = HITSUKI46_RAW_HID_MAGIC_0;
    hello_response[HITSUKI46_RAW_HID_OFFSET_MAGIC_1] = HITSUKI46_RAW_HID_MAGIC_1;
    hello_response[HITSUKI46_RAW_HID_OFFSET_VERSION] = HITSUKI46_RAW_HID_VERSION;
    hello_response[HITSUKI46_RAW_HID_OFFSET_TYPE] = HITSUKI46_RAW_HID_PACKET_DEVICE_HELLO;
    hello_response[HITSUKI46_RAW_HID_HELLO_SEQ] = seq;

    raise_raw_hid_sent_event((struct raw_hid_sent_event){
        .data = hello_response,
        .length = sizeof(hello_response),
    });
}

static int hitsuki46_raw_hid_received_listener(const zmk_event_t *eh) {
    struct raw_hid_received_event *event = as_raw_hid_received_event(eh);
    struct hitsuki46_raw_hid_packet packet;

    if (!parse_packet(event, &packet)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    switch (packet.type) {
    case HITSUKI46_RAW_HID_PACKET_HOST_HELLO:
        send_device_hello(packet.hello.seq);
        break;
    case HITSUKI46_RAW_HID_PACKET_APP_LAYER:
#if IS_ENABLED(CONFIG_HITSUKI46_RAW_HID_LAYER_CONTROL)
        hitsuki46_raw_hid_layer_control_handle(&packet);
#endif
        break;
    case HITSUKI46_RAW_HID_PACKET_TIME_SYNC:
#if IS_ENABLED(CONFIG_HITSUKI46_RAW_HID_TIME_SYNC)
        hitsuki46_raw_hid_time_sync_handle(&packet);
#endif
        break;
    case HITSUKI46_RAW_HID_PACKET_AI_USAGE:
#if IS_ENABLED(CONFIG_HITSUKI46_RAW_HID_AI_USAGE)
        hitsuki46_raw_hid_ai_usage_handle(&packet);
#endif
        break;
    default:
        break;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(hitsuki46_raw_hid, hitsuki46_raw_hid_received_listener);
ZMK_SUBSCRIPTION(hitsuki46_raw_hid, raw_hid_received_event);
