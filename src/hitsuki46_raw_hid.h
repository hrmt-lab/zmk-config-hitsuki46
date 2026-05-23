#pragma once

#include <stdint.h>

#define HITSUKI46_RAW_HID_PACKET_SIZE 32
#define HITSUKI46_RAW_HID_MAX_LAYER 31

enum hitsuki46_raw_hid_packet_type {
    HITSUKI46_RAW_HID_PACKET_SET_LAYER = 0x01,
    HITSUKI46_RAW_HID_PACKET_CLEAR = 0x02,
    HITSUKI46_RAW_HID_PACKET_HELLO = 0x10,
    HITSUKI46_RAW_HID_PACKET_HELLO_RESPONSE = 0x11,
    HITSUKI46_RAW_HID_PACKET_TIME_SYNC = 0x20,
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
            uint8_t layer;
            uint8_t flags;
            uint8_t seq;
        } legacy;
        struct {
            uint32_t unix_time_sec;
            int16_t tz_offset_min;
            uint8_t weekday;
            enum hitsuki46_raw_hid_time_format format_hint;
            enum hitsuki46_raw_hid_clock_mode clock_mode;
        } time_sync;
    };
};

void hitsuki46_raw_hid_layer_control_handle(const struct hitsuki46_raw_hid_packet *packet);
void hitsuki46_raw_hid_time_sync_handle(const struct hitsuki46_raw_hid_packet *packet);
