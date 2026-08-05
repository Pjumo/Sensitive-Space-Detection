#include <stdio.h>
#include "mock/mock_backend.h"

/* 마지막 움직임 감지로부터 이 시간(초)이 지나면 "사람 없음"으로 판단.
   드라이버가 아니라 여기(앱)에 있으니까, 재빌드 없이 이 숫자만 바꾸면
   30초든 5분이든 자유롭게 조정 가능 */
#define OCCUPANCY_TIMEOUT_SEC 5

static void print_event(const struct presence_event *ev)
{
    const char *type_str =
        (ev->event_type == PRESENCE_EVENT_DETECTED) ? "DETECTED" :
        (ev->event_type == PRESENCE_EVENT_CLEARED)  ? "CLEARED"  :
        (ev->event_type == PRESENCE_EVENT_ERROR)    ? "ERROR"    : "NONE";

    printf("[sensor_agent] seq=%u type=%s raw=%u\n",
           ev->sequence, type_str, ev->raw_value);
}

int main(void)
{
    struct presence_event ev;
    uint64_t last_event_ns = 0;
    int occupied = 0;

    /* 지금은 mock_backend_*를 직접 호출.
       나중에 real_backend.c가 생기면 이 세 줄의 함수 이름만 바꾸면 됨 */
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

        if (ev.event_type == PRESENCE_EVENT_DETECTED) {
            if (last_event_ns != 0) {
                uint64_t gap_ns = ev.timestamp_ns - last_event_ns;
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
