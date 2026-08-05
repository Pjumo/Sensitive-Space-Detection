#include <stdio.h>
#include "mock/mock_backend.h"

#define OCCUPANCY_TIMEOUT_SEC 5

static void print_event(const struct presence_event *ev)
{
    const char *type_str =
        (ev->event_type == PRESENCE_EVENT_ASSERTED)   ? "ASSERTED"   :
        (ev->event_type == PRESENCE_EVENT_DEASSERTED) ? "DEASSERTED" :
        (ev->event_type == PRESENCE_EVENT_ERROR)      ? "ERROR"      : "NONE";

    printf("[sensor_agent] seq=%u type=%s raw=%u\n",
           ev->sequence, type_str, ev->raw_value);

    if (ev->flags & PRESENCE_EVENT_FLAG_DROPPED_BEFORE) {
        printf("[sensor_agent] 경고: 이전 이벤트 일부 유실됨\n");
    }
}

int main(void)
{
    struct presence_event ev;
    __u64 last_event_ns = 0;
    int occupied = 0;

    if (mock_backend_init() != 0) {
        fprintf(stderr, "backend 초기화 실패\n");
        return 1;
    }

    printf("[sensor_agent] 시작 (occupancy timeout = %d초)\n", OCCUPANCY_TIMEOUT_SEC);

    while (1) {
        if (mock_backend_read(&ev) != 0) {
            fprintf(stderr, "read 실패\n");
            break;
        }

        print_event(&ev);

        if (ev.event_type == PRESENCE_EVENT_ASSERTED) {
            if (last_event_ns != 0) {
                __u64 gap_ns = ev.timestamp_ns - last_event_ns;
                double gap_sec = gap_ns / 1000000000.0;

                if (occupied && gap_sec > OCCUPANCY_TIMEOUT_SEC) {
                    occupied = 0;
                    printf("[sensor_agent] >>> 재실 아님으로 전환 (마지막 감지 후 %.1f초 경과)\n", gap_sec);
                }
            }

            if (!occupied) {
                occupied = 1;
                printf("[sensor_agent] >>> 재실 있음으로 전환\n");
            }

            last_event_ns = ev.timestamp_ns;
        }
    }

    mock_backend_close();
    return 0;
}
