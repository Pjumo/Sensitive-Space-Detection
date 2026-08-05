#ifndef PRESENCE_UAPI_H
#define PRESENCE_UAPI_H

#include <linux/types.h>

#define PRESENCE_API_VERSION 1

enum presence_sensor_type {
    PRESENCE_SENSOR_PIR = 1,
    PRESENCE_SENSOR_RADAR = 2,
};

enum presence_event_type {
    PRESENCE_EVENT_NONE = 0,
    PRESENCE_EVENT_DETECTED = 1,
    PRESENCE_EVENT_CLEARED = 2,
    PRESENCE_EVENT_ERROR = 3,
};

struct presence_event {
    __u32 api_version;
    __u32 sensor_type;
    __u32 event_type;
    __u32 sequence;
    __u64 timestamp_ns;
    __u32 raw_value;
    __u32 reserved;
};

#endif
