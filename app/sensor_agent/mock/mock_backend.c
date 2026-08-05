#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "mock_backend.h"

/* static: 이 파일(mock_backend.c) 안에서만 쓰는 전역변수.
   real_backend.c를 나중에 만들어도 서로 이름이 겹칠 걱정이 없음 */
static uint32_t seq_counter = 0;

int mock_backend_init(void)
{
    srand(time(NULL));   /* 매 실행마다 다른 랜덤 시퀀스가 나오게 시드 초기화 */
    printf("[mock] PIR mock backend 초기화 완료\n");
    return 0;
}

/* 진짜 PIR 드라이버의 pir_read()가 wait_event_interruptible로
   "다음 움직임이 감지될 때까지 대기"했던 것처럼,
   여기서도 sleep으로 그 "대기"를 흉내낸다 */
int mock_backend_read(struct presence_event *ev)
{
    int wait_sec = 1 + rand() % 6;   /* 1~6초 사이 랜덤 대기 (움직임이 뜸하게 발생하는 걸 흉내) */
    sleep(wait_sec);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);   /* 지난번 배운 CLOCK_MONOTONIC 원칙 그대로 적용 */

    ev->api_version  = PRESENCE_API_VERSION;
    ev->sensor_type  = PRESENCE_SENSOR_PIR;
    ev->event_type   = PRESENCE_EVENT_DETECTED;
    ev->sequence     = seq_counter++;
    ev->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    ev->raw_value    = 1;   /* 실제 PIR이면 GPIO 값, mock에서는 그냥 1(HIGH)로 고정 */
    ev->reserved     = 0;

    return 0;   /* 성공 */
}

void mock_backend_close(void)
{
    printf("[mock] PIR mock backend 종료\n");
}
