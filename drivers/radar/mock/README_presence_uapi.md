# Presence UAPI

## 1. 개요

`presence_uapi.h`는 커널 드라이버(Kernel Driver)와 사용자 프로그램(User Application)이
동일한 인터페이스를 사용하여 통신하기 위한 공통 헤더 파일이다.

Mock Driver와 실제 Radar Driver는 모두 이 헤더를 사용하며,
사용자 프로그램은 어떤 드라이버가 동작하는지 구분하지 않고 동일한 방식으로 사용할 수 있다.

```
                +-----------------------+
                |   User Application    |
                +-----------+-----------+
                            |
                 read / poll / ioctl
                            |
                +-----------v-----------+
                |   presence_uapi.h     |
                +-----------+-----------+
                            |
          +-----------------+-----------------+
          |                                   |
          v                                   v
+------------------------+      +------------------------+
|   Radar Mock Driver    |      |   Real Radar Driver    |
+------------------------+      +------------------------+
```

---

## 2. 목적

`presence_uapi.h`의 목적은 다음과 같다.

- 커널 드라이버와 사용자 프로그램이 동일한 구조체를 사용하도록 정의
- Mock Driver와 실제 Driver가 동일한 인터페이스를 제공하도록 설계
- 사용자 프로그램이 드라이버 변경 여부를 신경 쓰지 않고 사용할 수 있도록 지원

즉,

```
Mock Driver
      ↓

Real Radar Driver
```

로 변경되더라도 사용자 프로그램은 수정 없이 그대로 사용할 수 있다.

---

## 3. 구성 요소

### API Version

```c
PRESENCE_API_VERSION
```

현재 인터페이스 버전을 나타낸다.

Driver와 Application은 동일한 API Version을 사용해야 한다.

---

### Sensor Type

```c
PRESENCE_SENSOR_PIR
PRESENCE_SENSOR_RADAR
```

센서의 종류를 나타낸다.

| Sensor | 설명 |
|---------|------|
| PIR | PIR 센서 |
| RADAR | IWR6843 Radar |

---

### Event Type

```c
PRESENCE_EVENT_NONE
PRESENCE_EVENT_ASSERTED
PRESENCE_EVENT_DEASSERTED
PRESENCE_EVENT_ERROR
```

센서 이벤트의 종류를 나타낸다.

| Event | 설명 |
|--------|------|
| NONE | 이벤트 없음 |
| ASSERTED | 사람 감지 |
| DEASSERTED | 사람 사라짐 |
| ERROR | 오류 발생 |

---

### Capability Flags

드라이버가 지원하는 기능을 나타낸다.

- READ
- POLL
- CURRENT_STATE
- STATS
- RISING_EDGE
- SINGLE_READER

사용자 프로그램은 `ioctl(GET_CAPS)`를 통해 드라이버의 기능을 확인할 수 있다.

---

## 4. 데이터 구조

### struct presence_event

드라이버가 사용자 프로그램으로 전달하는 이벤트 정보이다.

| 멤버 | 설명 |
|------|------|
| api_version | API 버전 |
| sensor_type | 센서 종류 |
| event_type | 이벤트 종류 |
| sequence | 이벤트 번호 |
| timestamp_ns | 이벤트 발생 시간 |
| raw_value | 센서 원본 값 |
| flags | 이벤트 부가 정보 |

---

### struct presence_state

현재 센서의 상태를 저장한다.

예를 들어

- 현재 사람 감지 여부
- 마지막 이벤트 번호
- 마지막 이벤트 시간

등을 저장한다.

---

### struct presence_caps

드라이버가 지원하는 기능 정보를 저장한다.

예를 들어

- poll 지원 여부
- read 지원 여부
- FIFO 크기

등이 포함된다.

---

### struct presence_stats

드라이버 내부의 통계 정보를 저장한다.

예를 들어

- 전체 이벤트 수
- 전달된 이벤트 수
- 손실된 이벤트 수

등을 저장한다.

---

## 5. ioctl 명령

Application은 다음 ioctl을 사용할 수 있다.

| 명령 | 설명 |
|------|------|
| GET_API_VERSION | API 버전 조회 |
| GET_CAPS | Driver 기능 조회 |
| GET_STATE | 현재 상태 조회 |
| GET_STATS | 이벤트 통계 조회 |
| CLEAR_STATS | 통계 초기화 |

---

## 6. 이벤트 처리 과정

Mock Driver는 일정 주기로 가상의 Presence Event를 생성한다.

```
delayed_work 실행
        │
        ▼
Presence Event 생성
        │
        ▼
KFIFO 저장
        │
        ▼
poll() 대기 중인 App 깨움
        │
        ▼
Application에서 read()
```

사용자 프로그램은 실제 Radar Driver인지 Mock Driver인지 구분할 필요 없이
동일한 방식으로 이벤트를 처리할 수 있다.

---

## 7. 설계 목표

본 프로젝트의 목표는 Mock Driver와 실제 Radar Driver가
동일한 인터페이스를 제공하도록 하는 것이다.

```
Application
      │
      ▼
/dev/radar_presence
      │
      ├── Mock Driver
      └── Real Radar Driver
```

따라서 실제 레이더가 연결되더라도 사용자 프로그램은 수정 없이 그대로 사용할 수 있다.

---

## 8. 파일 구성

```
include/
    presence_uapi.h

src/
    radar_mock_drv.c

user/
    radar_app.c
```

---

## 9. 참고 사항

- `presence_uapi.h`는 커널 공간(Kernel Space)과 사용자 공간(User Space)이 함께 사용하는 공통 인터페이스이다.
- 모든 구조체와 매크로는 Driver와 Application에서 동일하게 사용되어야 한다.
- 새로운 기능을 추가할 경우 기존 구조체와의 호환성을 고려하여 API Version을 관리한다.
