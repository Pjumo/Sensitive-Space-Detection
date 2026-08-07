#ifndef PRESENCE_UAPI_H
#define PRESENCE_UAPI_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* =========================================================
 * API Version
 * ========================================================= */
#define PRESENCE_API_VERSION      1U

/* =========================================================
 * Sensor Type
 * ========================================================= */
#define PRESENCE_SENSOR_PIR       1U
#define PRESENCE_SENSOR_RADAR     2U

/* =========================================================
 * Event Type
 * ========================================================= */
#define PRESENCE_EVENT_NONE        0U
#define PRESENCE_EVENT_ASSERTED    1U
#define PRESENCE_EVENT_DEASSERTED  2U
#define PRESENCE_EVENT_ERROR       3U

/* =========================================================
 * Event Flags
 * ========================================================= */
#define PRESENCE_EVENT_FLAG_DROPPED_BEFORE    (1U << 0)

/* =========================================================
 * Capability Flags
 * ========================================================= */
#define PRESENCE_CAP_READ             (1U << 0)
#define PRESENCE_CAP_POLL             (1U << 1)
#define PRESENCE_CAP_CURRENT_STATE    (1U << 2)
#define PRESENCE_CAP_STATS            (1U << 3)
#define PRESENCE_CAP_RISING_EDGE      (1U << 4)
#define PRESENCE_CAP_SINGLE_READER    (1U << 5)

/* =========================================================
 * Event
 * ========================================================= */
struct presence_event
{
    __u32 api_version;
    __u32 sensor_type;
    __u32 event_type;
    __u32 sequence;

    __u64 timestamp_ns;

    __u32 raw_value;
    __u32 flags;
};

/* =========================================================
 * Capability
 * ========================================================= */
struct presence_caps
{
    __u32 api_version;
    __u32 sensor_type;
    __u32 capability_flags;
    __u32 event_size;
    __u32 fifo_depth;
};

/* =========================================================
 * Current State
 * ========================================================= */
struct presence_state
{
    __u32 api_version;
    __u32 sensor_type;

    __u32 raw_value;
    __u32 sequence;

    __u64 last_timestamp_ns;
};

/* =========================================================
 * Statistics
 * ========================================================= */
struct presence_stats
{
    __u64 total_events;
    __u64 delivered_events;
    __u64 dropped_events;
    __u64 last_timestamp_ns;

    __u32 api_version;
};

/* =========================================================
 * IOCTL
 * ========================================================= */
#define PRESENCE_IOC_MAGIC    'P'

#define PRESENCE_IOC_GET_API_VERSION \
        _IOR(PRESENCE_IOC_MAGIC, 0, __u32)

#define PRESENCE_IOC_GET_CAPS \
        _IOR(PRESENCE_IOC_MAGIC, 1, struct presence_caps)

#define PRESENCE_IOC_GET_STATE \
        _IOR(PRESENCE_IOC_MAGIC, 2, struct presence_state)

#define PRESENCE_IOC_GET_STATS \
        _IOR(PRESENCE_IOC_MAGIC, 3, struct presence_stats)

#define PRESENCE_IOC_CLEAR_STATS \
        _IO(PRESENCE_IOC_MAGIC, 4)

#endif /* PRESENCE_UAPI_H */
