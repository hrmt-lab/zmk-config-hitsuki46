zephyr_library()
zephyr_include_directories(include)

if(CONFIG_HITSUKI46_RAW_HID)
  zephyr_library_sources(src/hitsuki46_raw_hid.c)
endif()

if(CONFIG_HITSUKI46_RAW_HID_LAYER_CONTROL)
  zephyr_library_sources(src/hitsuki46_raw_hid_layer_control.c)
endif()

if(CONFIG_HITSUKI46_RAW_HID_TIME_SYNC)
  zephyr_library_sources(src/hitsuki46_raw_hid_time_sync.c)
endif()

if(CONFIG_HITSUKI46_RAW_HID_AI_USAGE)
  zephyr_library_sources(src/hitsuki46_raw_hid_ai_usage.c)
endif()
