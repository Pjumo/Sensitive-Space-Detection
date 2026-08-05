# Presence Device Interface

## 1. 장치 파일

PIR:

/dev/pir_presence

Radar:

/dev/radar_presence

두 장치는 공통 presence_event 구조체를 사용한다.

## 2. open()

현재 버전에서는 장치별 사용자 프로그램 하나만 열 수 있도록 한다.

첫 번째 open()은 성공한다.

이미 열려 있는 상태에서 다시 open()하면:

-EBUSY

여러 프로그램에서 센서 결과가 필요하면 sensor_agent가 한 번 읽고 다른 프로그램에 전달한다.

## 3. read()

read() 한 번에 presence_event 구조체 한 개를 반환한다.

정상 반환값:

sizeof(struct presence_event)

사용자 버퍼가 구조체보다 작으면:

-EINVAL

이벤트가 없고 일반 blocking 방식이면 새로운 이벤트가 발생할 때까지 대기한다.

이벤트가 없고 O_NONBLOCK으로 열었으면:

-EAGAIN

read()가 시그널에 의해 중단되면:

-ERESTARTSYS

이벤트를 성공적으로 읽으면 이벤트 큐에서 해당 이벤트를 제거한다.

## 4. poll()

이벤트 큐에 데이터가 있으면 다음 값을 반환한다.

POLLIN | POLLRDNORM

이벤트가 없으면 wait queue에 등록한 뒤 대기한다.

장치 오류가 발생한 경우:

POLLERR

## 5. 이벤트 형식

PIR 상승 에지 발생 시:

sensor_type = PRESENCE_SENSOR_PIR
event_type = PRESENCE_EVENT_ASSERTED
raw_value = 1

초기 버전에서는 PIR 하강 에지를 전달하지 않는다.

추후 하강 에지를 지원하면:

event_type = PRESENCE_EVENT_DEASSERTED
raw_value = 0

PIR 출력이 LOW가 됐다는 사실만 전달하며 사람 없음으로 판정하지 않는다.

## 6. 시간 정보

timestamp_ns는 다음 함수를 사용한다.

ktime_get_boottime_ns()

단위:

nanosecond

시스템 부팅 이후 경과 시간을 기준으로 한다.

## 7. Sequence

첫 이벤트의 sequence 값은 1이다.

이벤트가 발생할 때마다 1씩 증가한다.

__u32 최대값을 넘으면 자연스럽게 0으로 순환한다.

## 8. 이벤트 큐

드라이버 내부에 고정 크기 FIFO 큐를 사용한다.

권장 큐 크기:

64 events

큐가 가득 찬 상태에서 새 이벤트가 발생하면 가장 오래된 이벤트를 삭제하고 새 이벤트를 저장한다.

삭제된 이벤트 수는 dropped_count에 기록한다.

IRQ 핸들러와 read()가 동시에 큐에 접근할 수 있으므로 spinlock으로 보호한다.

## 9. ioctl()

공통 ioctl:

PRESENCE_IOC_GET_API_VERSION
PRESENCE_IOC_GET_CAPS
PRESENCE_IOC_GET_STATE
PRESENCE_IOC_GET_STATS
PRESENCE_IOC_CLEAR_STATS

### PRESENCE_IOC_GET_API_VERSION

현재 UAPI 버전을 반환한다.

### PRESENCE_IOC_GET_CAPS

센서 종류와 지원 기능을 반환한다.

PIR 드라이버 지원 기능:

- read
- poll
- GPIO current state
- statistics

### PRESENCE_IOC_GET_STATE

현재 GPIO 값과 마지막 sequence를 반환한다.

### PRESENCE_IOC_GET_STATS

다음 통계를 반환한다.

- 전체 발생 이벤트 수
- 사용자에게 전달된 이벤트 수
- 큐에서 유실된 이벤트 수
- 마지막 이벤트 timestamp

### PRESENCE_IOC_CLEAR_STATS

통계값을 0으로 초기화한다.

현재 GPIO 상태와 sequence는 초기화하지 않는다.

## 10. 담당 범위

커널 드라이버:

- GPIO 이벤트 감지
- 이벤트 시간 기록
- 이벤트 순서 번호 기록
- 이벤트 큐 관리
- 이벤트 유실 통계 관리
- read, poll, ioctl 제공

sensor_agent:

- 마지막 움직임 이후 점유 시간 계산
- PIR과 Radar 결과 결합
- 사람 있음 또는 사람 없음 최종 판정
- timeout 정책 관리
