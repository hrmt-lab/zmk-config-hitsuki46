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
#define HITSUKI46_RAW_HID_OFFSET_LAYER 4
#define HITSUKI46_RAW_HID_OFFSET_FLAGS 5
#define HITSUKI46_RAW_HID_OFFSET_SEQ 6
#define HITSUKI46_RAW_HID_LEGACY_RESERVED_START 7
#define HITSUKI46_RAW_HID_TIME_SYNC_UNIX_TIME_SEC 4
#define HITSUKI46_RAW_HID_TIME_SYNC_TZ_OFFSET_MIN 8
#define HITSUKI46_RAW_HID_TIME_SYNC_WEEKDAY 10
#define HITSUKI46_RAW_HID_TIME_SYNC_FORMAT_HINT 11
#define HITSUKI46_RAW_HID_TIME_SYNC_CLOCK_MODE 12
#define HITSUKI46_RAW_HID_TIME_SYNC_RESERVED_START 13

BUILD_ASSERT(CONFIG_RAW_HID_REPORT_SIZE == HITSUKI46_RAW_HID_PACKET_SIZE,
             "hitsuki46 Raw HID requires 32 byte reports");

static uint8_t hello_response[HITSUKI46_RAW_HID_PACKET_SIZE];

static bool legacy_reserved_bytes_are_zero(const uint8_t *data) {
    for (uint8_t i = HITSUKI46_RAW_HID_LEGACY_RESERVED_START; i < HITSUKI46_RAW_HID_PACKET_SIZE;
         i++) {
        if (data[i] != 0) {
            return false;
        }
    }

    return true;
}

static bool time_sync_reserved_bytes_are_zero(const uint8_t *data) {
    for (uint8_t i = HITSUKI46_RAW_HID_TIME_SYNC_RESERVED_START;
         i < HITSUKI46_RAW_HID_PACKET_SIZE; i++) {
        if (data[i] != 0) {
            return false;
        }
    }

    return true;
}

static bool packet_type_is_known(uint8_t packet_type) {
    switch (packet_type) {
    case HITSUKI46_RAW_HID_PACKET_SET_LAYER:
    case HITSUKI46_RAW_HID_PACKET_CLEAR:
    case HITSUKI46_RAW_HID_PACKET_HELLO:
    case HITSUKI46_RAW_HID_PACKET_HELLO_RESPONSE:
    case HITSUKI46_RAW_HID_PACKET_TIME_SYNC:
        return true;
    default:
        return false;
    }
}

static bool parse_legacy_packet(const uint8_t *data, struct hitsuki46_raw_hid_packet *packet) {
    if (data[HITSUKI46_RAW_HID_OFFSET_LAYER] > HITSUKI46_RAW_HID_MAX_LAYER) {
        return false;
    }

    if (!legacy_reserved_bytes_are_zero(data)) {
        return false;
    }

    packet->legacy.layer = data[HITSUKI46_RAW_HID_OFFSET_LAYER];
    packet->legacy.flags = data[HITSUKI46_RAW_HID_OFFSET_FLAGS];
    packet->legacy.seq = data[HITSUKI46_RAW_HID_OFFSET_SEQ];
    return true;
}

static bool parse_time_sync_packet(const uint8_t *data, struct hitsuki46_raw_hid_packet *packet) {
    uint8_t weekday = data[HITSUKI46_RAW_HID_TIME_SYNC_WEEKDAY];
    uint8_t format_hint = data[HITSUKI46_RAW_HID_TIME_SYNC_FORMAT_HINT];
    uint8_t clock_mode = data[HITSUKI46_RAW_HID_TIME_SYNC_CLOCK_MODE];

    if (weekday < 1 || weekday > 7) {
        return false;
    }

    if (!time_sync_reserved_bytes_are_zero(data)) {
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
    case HITSUKI46_RAW_HID_PACKET_SET_LAYER:
    case HITSUKI46_RAW_HID_PACKET_CLEAR:
    case HITSUKI46_RAW_HID_PACKET_HELLO:
    case HITSUKI46_RAW_HID_PACKET_HELLO_RESPONSE:
        return parse_legacy_packet(data, packet);
    case HITSUKI46_RAW_HID_PACKET_TIME_SYNC:
        return parse_time_sync_packet(data, packet);
    default:
        return false;
    }
}

static void send_hello_response(uint8_t seq) {
    memset(hello_response, 0, sizeof(hello_response));
    hello_response[HITSUKI46_RAW_HID_OFFSET_MAGIC_0] = HITSUKI46_RAW_HID_MAGIC_0;
    hello_response[HITSUKI46_RAW_HID_OFFSET_MAGIC_1] = HITSUKI46_RAW_HID_MAGIC_1;
    hello_response[HITSUKI46_RAW_HID_OFFSET_VERSION] = HITSUKI46_RAW_HID_VERSION;
    hello_response[HITSUKI46_RAW_HID_OFFSET_TYPE] = HITSUKI46_RAW_HID_PACKET_HELLO_RESPONSE;
    hello_response[HITSUKI46_RAW_HID_OFFSET_SEQ] = seq;

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
    case HITSUKI46_RAW_HID_PACKET_HELLO:
        send_hello_response(packet.legacy.seq);
        break;
    case HITSUKI46_RAW_HID_PACKET_SET_LAYER:
    case HITSUKI46_RAW_HID_PACKET_CLEAR:
#if IS_ENABLED(CONFIG_HITSUKI46_RAW_HID_LAYER_CONTROL)
        hitsuki46_raw_hid_layer_control_handle(&packet);
#endif
        break;
    case HITSUKI46_RAW_HID_PACKET_TIME_SYNC:
#if IS_ENABLED(CONFIG_HITSUKI46_RAW_HID_TIME_SYNC)
        hitsuki46_raw_hid_time_sync_handle(&packet);
#endif
        break;
    default:
        break;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(hitsuki46_raw_hid, hitsuki46_raw_hid_received_listener);
ZMK_SUBSCRIPTION(hitsuki46_raw_hid, raw_hid_received_event);
