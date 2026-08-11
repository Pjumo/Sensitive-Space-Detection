/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef PRESENCE_UAPI_H
#define PRESENCE_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* =========================================================
 * API Version
 * ========================================================= */
#define PRESENCE_API_VERSION 1U

/* =========================================================
 * Sensor Type
 * ========================================================= */
enum presence_sensor_type {
	PRESENCE_SENSOR_PIR   = 1,
	PRESENCE_SENSOR_RADAR = 2,
};

/* =========================================================
 * Event Type
 * ========================================================= */
enum presence_event_type {
	PRESENCE_EVENT_NONE       = 0,
	PRESENCE_EVENT_ASSERTED   = 1,
	PRESENCE_EVENT_DEASSERTED = 2,
	PRESENCE_EVENT_ERROR      = 3,
};

/* =========================================================
 * Event Flags
 * ========================================================= */
#define PRESENCE_EVENT_FLAG_DROPPED_BEFORE (1U << 0)

/* =========================================================
 * Capability Flags
 * ========================================================= */
#define PRESENCE_CAP_READ          (1U << 0)
#define PRESENCE_CAP_POLL          (1U << 1)
#define PRESENCE_CAP_CURRENT_STATE (1U << 2)
#define PRESENCE_CAP_STATS         (1U << 3)
#define PRESENCE_CAP_RISING_EDGE   (1U << 4)
#define PRESENCE_CAP_SINGLE_READER (1U << 5)

/* =========================================================
 * Event
 * ========================================================= */
struct presence_event {
	__u32 api_version;
	__u32 sensor_type;
	__u32 event_type;
	__u32 sequence;
	__aligned_u64 timestamp_ns;
	__u32 raw_value;
	__u32 flags;
};

/* =========================================================
 * Capability
 * ========================================================= */
struct presence_caps {
	__u32 api_version;
	__u32 sensor_type;
	__u32 capability_flags;
	__u32 event_size;
	__u32 fifo_depth;
	__u32 reserved[3];
};

/* =========================================================
 * Current State
 * ========================================================= */
struct presence_state {
	__u32 api_version;
	__u32 sensor_type;
	__u32 raw_value;
	__u32 sequence;
	__aligned_u64 last_timestamp_ns;
};

/* =========================================================
 * Statistics
 * ========================================================= */
struct presence_stats {
	__aligned_u64 total_events;
	__aligned_u64 delivered_events;
	__aligned_u64 dropped_events;
	__aligned_u64 last_timestamp_ns;
	__u32 api_version;
	__u32 reserved;
};

/* =========================================================
 * IOCTL
 * ========================================================= */
#define PRESENCE_IOC_MAGIC 'P'

#define PRESENCE_IOC_GET_API_VERSION \
	_IOR(PRESENCE_IOC_MAGIC, 0x00, __u32)
#define PRESENCE_IOC_GET_CAPS \
	_IOR(PRESENCE_IOC_MAGIC, 0x01, struct presence_caps)
#define PRESENCE_IOC_GET_STATE \
	_IOR(PRESENCE_IOC_MAGIC, 0x02, struct presence_state)
#define PRESENCE_IOC_GET_STATS \
	_IOR(PRESENCE_IOC_MAGIC, 0x03, struct presence_stats)
#define PRESENCE_IOC_CLEAR_STATS \
	_IO(PRESENCE_IOC_MAGIC, 0x04)

#endif /* PRESENCE_UAPI_H */
