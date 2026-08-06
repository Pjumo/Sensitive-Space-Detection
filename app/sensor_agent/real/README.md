# real backend

실제 PIR 커널 드라이버(/dev/pir_dev)와 연동하는 backend 구현.
mock_backend와 동일한 함수 시그니처(init/wait_read/close)를 가지므로,
sensor_agent_client.c에서 include만 바꾸면 mock과 교체 가능하다.

## 파일

| 파일 | 역할 |
|---|---|
| `real_backend.h` | 함수 선언 |
| `real_backend.c` | 실제 구현 |

## 동작 방식

```
real_backend_init()
→ /dev/pir_dev 파일이 없으면 mknod로 자동 생성 (major 230, minor 0)
→ O_RDONLY | O_NONBLOCK 으로 open

real_backend_wait_read(ev, timeout_ms)
→ poll()로 timeout_ms 동안 이벤트 대기
→ 타임아웃(이벤트 없음): 0 리턴
→ 이벤트 있음: read()로 struct presence_event 채워서 1 리턴
→ 에러: -1 리턴

real_backend_close()
→ fd 닫기
```

## 장치 파일 요구사항

- 커널 모듈(pir_dev.ko)이 insmod 되어 있어야 함
- major 번호는 230으로 고정 (pir_dev.c의 PIR_DEV_MAJOR)
- 장치를 여는 프로세스는 한 번에 하나만 가능 (PRESENCE_CAP_SINGLE_READER 정책)

## 왜 poll + 타임아웃 방식인가

블로킹 read()만 쓰면 다음 이벤트가 올 때까지 코드가 멈춰있어서,
occupancy timeout(사람이 나간 뒤 N초 경과 판정)을 실시간으로 체크할 수 없다.
poll()에 짧은 타임아웃(sensor_agent_client.c에서 1초로 호출)을 걸어
반복 호출하면, 이벤트가 없어도 주기적으로 깨어나 타이머를 확인할 수 있다.

## 사용처

`sensor_agent_client.c`에서 `#include "real/real_backend.h"`로 사용된다.
