# Presence 공통 UAPI

`presence_uapi.h`는 PIR 드라이버, Radar 드라이버와 UserSpace 프로그램이 함께 사용하는 공통 인터페이스 헤더이다.

커널 드라이버와 UserSpace가 같은 이벤트 구조체와 `ioctl` 명령을 사용하도록 정의한다.

## 파일 위치

```text
common/include/presence_uapi.h
```

## 사용 대상

이 헤더는 다음 구성요소에서 공통으로 사용한다.

- PIR 커널 드라이버
- Radar 커널 드라이버
- PIR 테스트 프로그램
- Radar 테스트 프로그램
- 센서 데이터를 읽는 UserSpace 애플리케이션

## 전체 데이터 흐름

```text
PIR 또는 Radar 센서
        ↓
커널 디바이스 드라이버
        ↓
presence_event 생성
        ↓
/dev 장치 파일
        ↓
UserSpace에서 read(), poll(), ioctl() 사용
```

드라이버 종류가 달라도 UserSpace에서는 동일한 구조체를 사용하여 센서 데이터를 처리할 수 있다.

---

## API 버전

```c
#define PRESENCE_API_VERSION 1U
```

현재 공통 인터페이스의 버전은 `1`이다.

UserSpace 프로그램은 드라이버에서 API 버전을 조회하여 자신이 지원하는 인터페이스와 호환되는지 확인할 수 있다.

구조체 형식이나 `ioctl` 동작이 호환되지 않게 변경되는 경우 API 버전도 함께 변경해야 한다.

---

## 센서 종류

```c
enum presence_sensor_type {
    PRESENCE_SENSOR_PIR = 1,
    PRESENCE_SENSOR_RADAR = 2,
};
```

| 값 | 의미 |
|---|---|
| `PRESENCE_SENSOR_PIR` | PIR 센서 |
| `PRESENCE_SENSOR_RADAR` | Radar 센서 |

이 값은 이벤트를 발생시킨 센서의 종류를 구분하는 데 사용한다.

---

## 이벤트 종류

```c
enum presence_event_type {
    PRESENCE_EVENT_NONE = 0,
    PRESENCE_EVENT_ASSERTED = 1,
    PRESENCE_EVENT_DEASSERTED = 2,
    PRESENCE_EVENT_ERROR = 3,
};
```

| 이벤트 | 의미 |
|---|---|
| `PRESENCE_EVENT_NONE` | 유효한 이벤트 없음 |
| `PRESENCE_EVENT_ASSERTED` | 센서가 감지 상태로 변경됨 |
| `PRESENCE_EVENT_DEASSERTED` | 센서가 비감지 상태로 변경됨 |
| `PRESENCE_EVENT_ERROR` | 센서 또는 드라이버 오류 |

현재 PIR 드라이버는 움직임을 감지했을 때 주로 `PRESENCE_EVENT_ASSERTED` 이벤트를 생성한다.

`PRESENCE_EVENT_DEASSERTED`가 정의되어 있다고 해서 모든 드라이버가 반드시 이 이벤트를 발생시키는 것은 아니다.

사람이 없는 상태나 최종 점유 상태는 UserSpace 정책에 따라 별도로 판단할 수 있다.

---

## 이벤트 플래그

```c
#define PRESENCE_EVENT_FLAG_DROPPED_BEFORE (1U << 0)
```

`PRESENCE_EVENT_FLAG_DROPPED_BEFORE`는 현재 이벤트보다 앞선 이벤트 일부가 손실되었음을 의미한다.

예를 들어 커널 FIFO가 가득 찬 경우 가장 오래된 이벤트를 제거하고, 새 이벤트의 `flags`에 이 값을 설정할 수 있다.

UserSpace에서는 다음과 같이 확인한다.

```c
if (event.flags & PRESENCE_EVENT_FLAG_DROPPED_BEFORE) {
    /* 이전 이벤트 일부가 누락됨 */
}
```

---

## 드라이버 기능 플래그

드라이버가 지원하는 기능은 `capability_flags` 비트 값으로 전달된다.

```c
#define PRESENCE_CAP_READ          (1U << 0)
#define PRESENCE_CAP_POLL          (1U << 1)
#define PRESENCE_CAP_CURRENT_STATE (1U << 2)
#define PRESENCE_CAP_STATS         (1U << 3)
#define PRESENCE_CAP_RISING_EDGE   (1U << 4)
#define PRESENCE_CAP_SINGLE_READER (1U << 5)
```

| 기능 | 의미 |
|---|---|
| `PRESENCE_CAP_READ` | `read()`로 이벤트 읽기 지원 |
| `PRESENCE_CAP_POLL` | `poll()` 또는 `select()` 지원 |
| `PRESENCE_CAP_CURRENT_STATE` | 현재 센서 상태 조회 지원 |
| `PRESENCE_CAP_STATS` | 이벤트 통계 조회 지원 |
| `PRESENCE_CAP_RISING_EDGE` | 상승 에지 이벤트 감지 |
| `PRESENCE_CAP_SINGLE_READER` | 한 번에 하나의 프로세스만 장치 열기 가능 |

기능 플래그는 여러 개가 동시에 설정될 수 있으므로 비트 연산으로 확인해야 한다.

```c
if (caps.capability_flags & PRESENCE_CAP_POLL) {
    /* poll() 사용 가능 */
}
```

---

## 이벤트 구조체

```c
struct presence_event {
    __u32 api_version;
    __u32 sensor_type;
    __u32 event_type;
    __u32 sequence;
    __aligned_u64 timestamp_ns;
    __u32 raw_value;
    __u32 flags;
};
```

드라이버의 `read()`를 호출하면 이 구조체 형식으로 이벤트를 전달받는다.

| 필드 | 의미 |
|---|---|
| `api_version` | 이벤트가 사용하는 API 버전 |
| `sensor_type` | PIR 또는 Radar 센서 구분 |
| `event_type` | 감지, 비감지 또는 오류 이벤트 |
| `sequence` | 이벤트 발생 순서 번호 |
| `timestamp_ns` | 이벤트 발생 시각을 나노초 단위로 기록 |
| `raw_value` | 센서의 원시 출력값 |
| `flags` | 이벤트 누락 등의 추가 상태 |

### sequence

이벤트가 생성될 때마다 증가하는 순서 번호이다.

UserSpace에서는 이전 이벤트와 번호가 연속적인지 확인하여 이벤트 누락 여부를 판단할 수 있다.

### raw_value

센서가 출력한 원시값이다.

PIR 센서에서는 일반적으로 다음과 같이 사용한다.

```text
0 = LOW
1 = HIGH
```

`raw_value`는 센서의 물리적인 출력값이며, 최종 점유 상태와 동일한 의미는 아니다.

---

## 기능 정보 구조체

```c
struct presence_caps {
    __u32 api_version;
    __u32 sensor_type;
    __u32 capability_flags;
    __u32 event_size;
    __u32 fifo_depth;
    __u32 reserved[3];
};
```

드라이버가 어떤 기능을 지원하는지 조회할 때 사용한다.

| 필드 | 의미 |
|---|---|
| `api_version` | 드라이버 API 버전 |
| `sensor_type` | 센서 종류 |
| `capability_flags` | 지원 기능 비트맵 |
| `event_size` | 이벤트 구조체 크기 |
| `fifo_depth` | 커널 FIFO에 저장 가능한 이벤트 수 |
| `reserved` | 향후 기능 확장을 위한 예약 영역 |

UserSpace에서는 이벤트 크기를 직접 고정하지 말고 `event_size`를 확인할 수 있다.

`reserved` 필드는 현재 사용하지 않으며 값을 해석하면 안 된다.

---

## 현재 상태 구조체

```c
struct presence_state {
    __u32 api_version;
    __u32 sensor_type;
    __u32 raw_value;
    __u32 sequence;
    __aligned_u64 last_timestamp_ns;
};
```

센서의 현재 상태를 조회할 때 사용한다.

| 필드 | 의미 |
|---|---|
| `api_version` | API 버전 |
| `sensor_type` | 센서 종류 |
| `raw_value` | 현재 센서 원시값 |
| `sequence` | 마지막 이벤트 순서 번호 |
| `last_timestamp_ns` | 마지막 이벤트 발생 시각 |

이 구조체는 현재 센서의 원시 상태를 제공한다.

PIR 센서의 값이 `0`이라고 해서 사람이 없다는 의미로 바로 판단하면 안 된다.

---

## 통계 구조체

```c
struct presence_stats {
    __aligned_u64 total_events;
    __aligned_u64 delivered_events;
    __aligned_u64 dropped_events;
    __aligned_u64 last_timestamp_ns;
    __u32 api_version;
    __u32 reserved;
};
```

드라이버의 이벤트 처리 상태를 확인할 때 사용한다.

| 필드 | 의미 |
|---|---|
| `total_events` | 드라이버가 생성한 전체 이벤트 수 |
| `delivered_events` | UserSpace에 전달된 이벤트 수 |
| `dropped_events` | FIFO 초과 또는 오류로 누락된 이벤트 수 |
| `last_timestamp_ns` | 마지막 이벤트 발생 시각 |
| `api_version` | API 버전 |
| `reserved` | 향후 확장용 예약 필드 |

---

## ioctl 명령

`ioctl()`은 이벤트 읽기 이외의 드라이버 정보를 조회하거나 설정할 때 사용한다.

모든 명령은 다음 Magic 값을 사용한다.

```c
#define PRESENCE_IOC_MAGIC 'P'
```

### API 버전 조회

```c
PRESENCE_IOC_GET_API_VERSION
```

드라이버가 지원하는 API 버전을 `__u32` 형식으로 가져온다.

### 기능 조회

```c
PRESENCE_IOC_GET_CAPS
```

드라이버가 지원하는 기능, 센서 종류, FIFO 크기 등을 `struct presence_caps`로 가져온다.

### 현재 상태 조회

```c
PRESENCE_IOC_GET_STATE
```

현재 센서 원시값과 마지막 이벤트 정보를 `struct presence_state`로 가져온다.

### 통계 조회

```c
PRESENCE_IOC_GET_STATS
```

전체 이벤트 수, 전달 이벤트 수와 누락 이벤트 수를 `struct presence_stats`로 가져온다.

### 통계 초기화

```c
PRESENCE_IOC_CLEAR_STATS
```

드라이버가 기록한 이벤트 통계를 초기화한다.

---

## UserSpace 기본 사용 순서

```text
1. /dev 장치 파일 open()
2. ioctl()로 API 버전 확인
3. ioctl()로 드라이버 기능 확인
4. read() 또는 poll()로 이벤트 대기
5. presence_event 구조체 확인
6. 사용이 끝나면 close()
```

PIR 드라이버의 장치 파일 예시는 다음과 같다.

```text
/dev/pir_presence
```

---

## UserSpace에서 헤더 포함

```c
#include "presence_uapi.h"
```

컴파일할 때 `common/include` 경로를 Include 경로에 추가해야 한다.

예시:

```bash
gcc -I../../../common/include -o pir_test pir_test.c
```

---

## 커널 드라이버에서 사용

커널 모듈 Makefile에는 공통 헤더 경로를 추가한다.

```make
ccflags-y += -I$(src)/../../../common/include
```

드라이버 소스에서 다음과 같이 포함한다.

```c
#include "presence_uapi.h"
```

---

## 자료형 사용 이유

구조체에는 일반적인 `int`, `long` 대신 다음 Linux 고정 크기 자료형을 사용한다.

```text
__u32
__aligned_u64
```

커널과 UserSpace 또는 32비트와 64비트 환경 사이에서 구조체 크기와 정렬 차이가 발생하는 것을 줄이기 위한 것이다.

---

## 수정 시 주의사항

이 파일은 PIR, Radar 및 UserSpace에서 공통으로 사용한다.

따라서 구조체 필드, 열거형 값 또는 `ioctl` 번호를 변경하면 관련된 모든 코드에 영향을 준다.

수정할 때는 다음 사항을 확인해야 한다.

- 기존 열거형 숫자를 임의로 변경하지 않는다.
- 기존 `ioctl` 번호를 재사용하지 않는다.
- 구조체 필드를 삭제하거나 순서를 변경하지 않는다.
- 새로운 필드는 호환성을 고려하여 추가한다.
- 호환되지 않는 변경이면 API 버전을 올린다.
- PIR, Radar 및 UserSpace 담당자와 변경 내용을 공유한다.

## 요약

`presence_uapi.h`는 PIR과 Radar 드라이버가 UserSpace에 동일한 데이터 형식으로 이벤트를 제공하기 위한 공통 규약이다.

각 드라이버의 내부 구현은 달라도 UserSpace는 `presence_event`, `read()`, `poll()` 및 공통 `ioctl` 명령을 사용하여 같은 방식으로 센서 데이터를 처리할 수 있다.
