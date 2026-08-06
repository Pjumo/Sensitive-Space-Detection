# mock backend

실제 PIR/Radar 하드웨어 없이, sensor_agent의 재실 판단 로직을 테스트하기 위한
가짜 이벤트 생성기.

## 파일

| 파일 | 역할 |
|---|---|
| `mock_backend.h` | 함수 선언 (real_backend.h와 동일한 시그니처) |
| `mock_backend.c` | 실제 구현 |

## 동작 방식
```
mock_backend_init()
→ srand()로 랜덤 시드 초기화

mock_backend_read()
→ 1~6초 사이 랜덤 대기 (sleep)
→ PRESENCE_EVENT_ASSERTED 이벤트 하나 생성해서 리턴
→ DEASSERTED 이벤트는 생성하지 않음

mock_backend_close()
→ 별도 정리 작업 없음
```

## 한계

- ASSERTED만 생성하고 DEASSERTED는 만들지 않으므로, real backend와 완전히
  동일한 occupancy 판단 흐름(DEASSERTED 기반 타이머)은 테스트할 수 없다.
- 순수하게 "이벤트를 받아서 처리하는 코드 경로 자체"가 정상 동작하는지
  확인하는 용도로만 사용.

## 사용처

`sensor_agent.c`에서 `#include "mock/mock_backend.h"`로 사용된다.
real backend로 교체 시, 동일한 함수 시그니처를 가진 `real_backend.h`로
include만 바꾸면 나머지 코드는 수정할 필요가 없다.
