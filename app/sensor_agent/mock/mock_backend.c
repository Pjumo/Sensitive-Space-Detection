#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "mock_backend.h"

static __u32 seq_counter = 0;

int mock_backend_init(void)
{
    srand(time(NULL));
    printf("[mock] PIR mock backend 초기화 완료\n");
    return 0;
}

int mock_backend_read(struct presence_event *ev)
{
    int wait_sec = 1 + rand() % 6;
    sleep(wait_sec);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    ev->api_version  = PRESENCE_API_VERSION;
    ev->sensor_type  = PRESENCE_SENSOR_PIR;
    ev->event_type   = PRESENCE_EVENT_ASSERTED;
    ev->sequence     = seq_counter++;
    ev->timestamp_ns = (__u64)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    ev->raw_value    = 1;
    ev->flags        = 0;

    return 0;
}

void mock_backend_close(void)
{
    printf("[mock] PIR mock backend 종료\n");
}
