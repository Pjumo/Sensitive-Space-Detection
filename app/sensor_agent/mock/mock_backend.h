#ifndef MOCK_BACKEND_H
#define MOCK_BACKEND_H

#include "../../../common/include/presence_uapi.h"

/* 이 세 함수가 나중에 real_backend.c에서도 똑같은 이름/모양으로 다시 구현될 예정.
   sensor_agent.c는 이 세 함수만 호출하고, 내부 구현은 전혀 신경쓰지 않음. */

int  mock_backend_init(void);
int  mock_backend_read(struct presence_event *ev);   /* 성공 시 0, 실패 시 음수 */
void mock_backend_close(void);

#endif
