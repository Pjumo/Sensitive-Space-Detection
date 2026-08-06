# HC-SR501 PIR Linux Device Driver

HC-SR501 PIR 센서를 제어하는 Linux 커널 문자 디바이스 드라이버.

GPIO 상승·하강 인터럽트로 센서 상태 변화를 감지하고, 발생한 이벤트를 FIFO에 저장한다. 사용자 애플리케이션은 `read()`, `poll()`, `epoll()`, `ioctl()`로 이벤트와 센서 상태를 확인할 수 있다.

---

## 주요 기능

* HC-SR501 GPIO 입력 처리
* 상승·하강 에지 인터럽트 감지
* 이벤트 FIFO 저장
* Blocking `read()` 지원
* Non-blocking `read()` 지원
* `poll()`, `select()`, `epoll()` 지원
* 현재 센서 상태 조회
* 이벤트 통계 조회 및 초기화
* 이벤트 순서 번호 기록
* 이벤트 발생 시간 기록
* FIFO 오버플로 감지
* 단일 Reader 제한
* 모듈 파라미터로 GPIO 번호 변경

---

## 동작 구조

```text
HC-SR501 OUT
      │
      ▼
GPIO 상승/하강 인터럽트
      │
      ▼
인터럽트 핸들러
      │
      ├── GPIO 값 확인
      ├── 이벤트 생성
      ├── 시간 및 순서 번호 기록
      └── FIFO 저장
              │
              ▼
       사용자 애플리케이션
       read / poll / ioctl
```

센서 상태가 변경되면 다음 순서로 처리한다.

1. GPIO 인터럽트 발생
2. 현재 GPIO 값 확인
3. `presence_event` 생성
4. 이벤트 순서 번호 증가
5. 이벤트 발생 시간 기록
6. 이벤트를 FIFO에 저장
7. 대기 중인 애플리케이션 깨우기
8. 애플리케이션에서 이벤트 읽기

---

## 장치 정보

| 항목         | 값              |
| ---------- | -------------- |
| 드라이버 이름    | `pir_dev`      |
| 장치 파일      | `/dev/pir_dev` |
| Major 번호   | `230`          |
| 기본 GPIO 번호 | `529`          |
| FIFO 크기    | 32개            |
| 센서 종류      | HC-SR501 PIR   |
| 라이선스       | GPL            |

문자 디바이스 등록 코드:

```c
register_chrdev(
    PIR_DEV_MAJOR,
    PIR_DEV_NAME,
    &pir_fops
);
```

현재 코드는 `/dev/pir_dev`를 자동 생성하지 않는다. 장치 파일은 직접 생성해야 한다.

```bash
sudo mknod /dev/pir_dev c 230 0
sudo chmod 666 /dev/pir_dev
```

---

## GPIO 설정

기본 GPIO 번호:

```c
static int pir_gpio = 529;
```

현재 테스트 환경 기준:

```text
gpiochip base 512 + BCM GPIO17 = Global GPIO 529
```

HC-SR501 연결 예시:

| HC-SR501 | Raspberry Pi |
| -------- | ------------ |
| VCC      | 5V           |
| OUT      | BCM GPIO17   |
| GND      | GND          |

GPIO 번호는 커널과 GPIO Chip Base 값에 따라 달라질 수 있다.

확인 명령:

```bash
gpioinfo
```

또는:

```bash
sudo cat /sys/kernel/debug/gpio
```

---

## 모듈 파라미터

`pir_gpio`를 모듈 파라미터로 등록한다.

```c
module_param(pir_gpio, int, 0444);
MODULE_PARM_DESC(pir_gpio, "HC-SR501 global GPIO number");
```

모듈 삽입 시 GPIO 번호 지정:

```bash
sudo insmod pir_dev.ko pir_gpio=529
```

현재 설정값 확인:

```bash
cat /sys/module/pir_dev/parameters/pir_gpio
```

---

## 인터럽트 처리

상승 에지와 하강 에지를 모두 감지한다.

```c
IRQF_TRIGGER_RISING |
IRQF_TRIGGER_FALLING
```

### 상승 에지

센서 출력이 `LOW → HIGH`로 변경된 상태.

```text
event_type = PRESENCE_EVENT_ASSERTED
raw_value  = 1
```

인체 움직임 감지 상태에 해당한다.

### 하강 에지

센서 출력이 `HIGH → LOW`로 변경된 상태.

```text
event_type = PRESENCE_EVENT_DEASSERTED
raw_value  = 0
```

감지 출력 해제 상태에 해당한다.

---

## 이벤트 구조

GPIO 인터럽트 발생 시 `struct presence_event`를 생성한다.

주요 필드:

| 필드             | 설명          |
| -------------- | ----------- |
| `api_version`  | UAPI 버전     |
| `sensor_type`  | 센서 종류       |
| `event_type`   | 감지 또는 감지 해제 |
| `timestamp_ns` | 이벤트 발생 시간   |
| `raw_value`    | GPIO 값      |
| `sequence`     | 이벤트 순서 번호   |
| `flags`        | 이벤트 상태 플래그  |

시간 기록:

```c
timestamp_ns = ktime_get_ns();
```

순서 번호 증가:

```c
dev->sequence++;
event.sequence = dev->sequence;
```

---

## FIFO

이벤트 저장용 원형 FIFO.

```c
#define PIR_FIFO_DEPTH 32U
```

FIFO 관리 변수:

| 변수           | 설명        |
| ------------ | --------- |
| `fifo_head`  | 다음 저장 위치  |
| `fifo_tail`  | 다음 읽기 위치  |
| `fifo_count` | 현재 이벤트 개수 |
| `fifo`       | 이벤트 배열    |

### FIFO 오버플로

FIFO가 가득 찬 상태에서 새 이벤트가 발생하면 가장 오래된 이벤트를 제거한다.

```text
가장 오래된 이벤트 제거
        ↓
dropped_events 증가
        ↓
새 이벤트에 DROPPED_BEFORE 설정
        ↓
새 이벤트 저장
```

사용 플래그:

```c
PRESENCE_EVENT_FLAG_DROPPED_BEFORE
```

이 플래그가 설정되면 현재 이벤트 이전에 누락된 이벤트가 있다는 의미다.

---

## open()

장치 파일을 열면 `pir_open()` 호출.

```c
open("/dev/pir_dev", O_RDONLY);
```

한 번에 하나의 애플리케이션만 장치를 열 수 있다.

```c
atomic_cmpxchg(&pir_dev.reader_open, 0, 1)
```

이미 장치가 열려 있으면:

```text
-EBUSY
```

장치 데이터를 `private_data`에 저장한다.

```c
filp->private_data = &pir_dev;
```

---

## read()

FIFO에서 이벤트 한 개를 읽는다.

```c
read(fd, &event, sizeof(event));
```

정상 반환값:

```c
sizeof(struct presence_event)
```

### Blocking 모드

```c
open("/dev/pir_dev", O_RDONLY);
```

FIFO에 이벤트가 없으면 Wait Queue에서 대기한다.

```c
wait_event_interruptible(
    dev->read_wait,
    pirEventAvailable(dev)
);
```

인터럽트 발생 후 대기 중인 프로세스를 깨운다.

```c
wake_up_interruptible(&dev->read_wait);
```

### Non-blocking 모드

```c
open("/dev/pir_dev", O_RDONLY | O_NONBLOCK);
```

이벤트가 없으면 즉시 반환:

```text
-EAGAIN
```

### 버퍼 크기 오류

사용자 버퍼가 이벤트 구조체보다 작으면:

```text
-EINVAL
```

### 사용자 공간 복사 오류

`copy_to_user()` 실패 시:

```text
-EFAULT
```

FIFO에서 이미 제거된 이벤트는 누락 이벤트로 기록한다.

---

## poll(), select(), epoll()

파일 연산 구조체의 `poll` 등록:

```c
.poll = pir_poll
```

Wait Queue 등록:

```c
poll_wait(filp, &dev->read_wait, wait);
```

FIFO에 이벤트가 있으면 다음 플래그 반환:

```c
EPOLLIN | EPOLLRDNORM
```

동작 흐름:

```text
poll() 대기
    ↓
GPIO 인터럽트 발생
    ↓
FIFO에 이벤트 저장
    ↓
wake_up_interruptible()
    ↓
poll() 반환
    ↓
read() 실행
```

주기적으로 GPIO를 확인하는 Busy Polling 없이 이벤트 대기 가능.

---

## ioctl()

파일 연산 구조체의 `ioctl` 등록:

```c
.unlocked_ioctl = pir_ioctl
```

지원 명령:

| ioctl 명령                       | 설명          |
| ------------------------------ | ----------- |
| `PRESENCE_IOC_GET_API_VERSION` | API 버전 조회   |
| `PRESENCE_IOC_GET_CAPS`        | 드라이버 기능 조회  |
| `PRESENCE_IOC_GET_STATE`       | 현재 센서 상태 조회 |
| `PRESENCE_IOC_GET_STATS`       | 이벤트 통계 조회   |
| `PRESENCE_IOC_CLEAR_STATS`     | 통계 초기화      |

잘못된 Magic Number 또는 지원하지 않는 명령:

```text
-ENOTTY
```

---

### API 버전 조회

```c
__u32 version;

ioctl(
    fd,
    PRESENCE_IOC_GET_API_VERSION,
    &version
);
```

반환값:

```c
PRESENCE_API_VERSION
```

---

### 기능 조회

```c
struct presence_caps caps;

ioctl(
    fd,
    PRESENCE_IOC_GET_CAPS,
    &caps
);
```

지원 기능:

```c
PRESENCE_CAP_READ
PRESENCE_CAP_POLL
PRESENCE_CAP_CURRENT_STATE
PRESENCE_CAP_STATS
PRESENCE_CAP_RISING_EDGE
PRESENCE_CAP_SINGLE_READER
```

반환 정보:

| 필드                 | 설명         |
| ------------------ | ---------- |
| `api_version`      | API 버전     |
| `sensor_type`      | 센서 종류      |
| `capability_flags` | 지원 기능      |
| `event_size`       | 이벤트 구조체 크기 |
| `fifo_depth`       | FIFO 크기    |

---

### 현재 상태 조회

```c
struct presence_state state;

ioctl(
    fd,
    PRESENCE_IOC_GET_STATE,
    &state
);
```

반환 정보:

| 필드                  | 설명         |
| ------------------- | ---------- |
| `raw_value`         | 현재 GPIO 값  |
| `sequence`          | 마지막 이벤트 번호 |
| `last_timestamp_ns` | 마지막 이벤트 시간 |
| `sensor_type`       | 센서 종류      |
| `api_version`       | API 버전     |

---

### 통계 조회

```c
struct presence_stats stats;

ioctl(
    fd,
    PRESENCE_IOC_GET_STATS,
    &stats
);
```

통계 항목:

| 필드                  | 설명              |
| ------------------- | --------------- |
| `total_events`      | 전체 생성 이벤트 수     |
| `delivered_events`  | 사용자에게 전달된 이벤트 수 |
| `dropped_events`    | 누락된 이벤트 수       |
| `last_timestamp_ns` | 마지막 이벤트 시간      |

---

### 통계 초기화

```c
ioctl(
    fd,
    PRESENCE_IOC_CLEAR_STATS
);
```

초기화 대상:

```text
total_events
delivered_events
dropped_events
stats_last_timestamp_ns
```

초기화하지 않는 항목:

* 현재 GPIO 상태
* 이벤트 순서 번호
* FIFO에 저장된 이벤트

---

## 동기화

인터럽트 핸들러와 사용자 프로세스가 같은 데이터를 공유하므로 동기화가 필요하다.

### Spinlock

FIFO와 상태 정보 보호:

```c
spinlock_t lock;
```

인터럽트 컨텍스트 보호:

```c
spin_lock_irqsave()
spin_unlock_irqrestore()
```

### Wait Queue

이벤트 대기 처리:

```c
wait_queue_head_t read_wait;
```

### Atomic 변수

단일 Reader 관리:

```c
atomic_t reader_open;
```

---

## 주요 함수

| 함수                       | 역할           |
| ------------------------ | ------------ |
| `pir_module_init()`      | 모듈 초기화       |
| `pir_module_exit()`      | 모듈 자원 해제     |
| `pirIntHandler()`        | GPIO 인터럽트 처리 |
| `pir_open()`             | 장치 열기        |
| `pir_read()`             | 이벤트 읽기       |
| `pir_poll()`             | 이벤트 대기       |
| `pir_ioctl()`            | 상태 및 통계 처리   |
| `pir_release()`          | 장치 닫기        |
| `pirEventAvailable()`    | FIFO 이벤트 확인  |
| `pirMarkDroppedLocked()` | 이벤트 누락 기록    |

---

## 모듈 초기화

`pir_module_init()` 처리 순서:

```text
Spinlock 초기화
        ↓
Wait Queue 초기화
        ↓
Atomic 변수 초기화
        ↓
FIFO 초기화
        ↓
gpio_request()
        ↓
gpio_direction_input()
        ↓
현재 GPIO 값 읽기
        ↓
gpio_to_irq()
        ↓
request_irq()
        ↓
register_chrdev()
```

초기화 실패 시 확보한 자원을 역순으로 해제한다.

```text
register_chrdev 실패
        ↓
free_irq()
        ↓
gpio_free()
```

---

## 모듈 제거

`pir_module_exit()` 처리 순서:

```text
unregister_chrdev()
        ↓
free_irq()
        ↓
gpio_free()
```

모듈 제거:

```bash
sudo rmmod pir_dev
```

---

## 프로젝트 구성

```text
HC-SR501-driver/
├── pir_dev.c
├── presence_uapi.h
├── pir_app.c
└── Makefile
```

`presence_uapi.h`에 필요한 항목:

* API 버전
* ioctl Magic Number
* ioctl 명령
* 센서 종류
* 이벤트 종류
* Capability 플래그
* 이벤트 플래그
* `struct presence_event`
* `struct presence_caps`
* `struct presence_state`
* `struct presence_stats`

---

## Makefile 예시

```makefile
obj-m += pir_dev.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

빌드:

```bash
make
```

생성 파일:

```text
pir_dev.ko
```

---

## 실행 방법

### 1. 모듈 삽입

```bash
sudo insmod pir_dev.ko pir_gpio=529
```

### 2. 모듈 확인

```bash
lsmod | grep pir_dev
```

### 3. 커널 로그 확인

```bash
dmesg | tail
```

정상 로그 예시:

```text
pir_dev: module init
pir_dev: ready major=230 gpio=529 irq=<IRQ 번호>
```

### 4. 장치 파일 생성

```bash
sudo mknod /dev/pir_dev c 230 0
sudo chmod 666 /dev/pir_dev
```

### 5. 장치 파일 확인

```bash
ls -l /dev/pir_dev
```

### 6. 애플리케이션 실행

```bash
./pir_app
```

### 7. 모듈 제거

```bash
sudo rmmod pir_dev
```

---

## 로그 확인

실시간 커널 로그:

```bash
sudo dmesg -w
```

장치 열기:

```text
pir_dev: open major=230 minor=0
```

장치 닫기:

```text
pir_dev: release
```

모듈 제거:

```text
pir_dev: module exit
```

---

## 오류 코드

| 오류                     | 원인                       |
| ---------------------- | ------------------------ |
| `-EBUSY`               | 다른 프로그램이 장치 사용 중         |
| `-EINVAL`              | `read()` 버퍼 크기 부족        |
| `-EAGAIN`              | Non-blocking 모드에서 이벤트 없음 |
| `-EFAULT`              | 사용자 공간 메모리 복사 실패         |
| `-ENOTTY`              | 지원하지 않는 ioctl 명령         |
| `gpio_request()` 실패    | GPIO 번호 오류 또는 GPIO 사용 중  |
| `request_irq()` 실패     | IRQ 요청 실패                |
| `register_chrdev()` 실패 | Major 번호 충돌              |

---

## 주의 사항

### GPIO 번호

기본값 `529`는 현재 테스트 환경 기준 Global GPIO 번호다.

커널 또는 보드가 바뀌면 GPIO 번호도 달라질 수 있다.

### Major 번호

Major 번호 `230`을 다른 드라이버가 사용 중이면 등록에 실패한다.

```bash
cat /proc/devices
```

### 단일 Reader

한 번에 하나의 애플리케이션만 `/dev/pir_dev`를 열 수 있다.

두 번째 `open()` 호출은 `EBUSY` 오류를 반환한다.

### FIFO 오버플로

애플리케이션이 이벤트를 늦게 읽으면 FIFO가 가득 찰 수 있다.

FIFO가 가득 차면 가장 오래된 이벤트를 제거하고 `dropped_events`를 증가시킨다.

### 장치 파일 자동 생성

현재 코드는 `class_create()`와 `device_create()`를 사용하지 않는다.

따라서 `/dev/pir_dev`를 직접 생성해야 한다.

### Capability 플래그

인터럽트는 상승·하강 에지를 모두 사용한다.

하지만 현재 `GET_CAPS`에서는 `PRESENCE_CAP_RISING_EDGE`만 설정한다. `presence_uapi.h`에 하강 에지 Capability가 있다면 추가 검토가 필요하다.

---

## 라이선스

```c
MODULE_LICENSE("GPL");
```

SPDX:

```text
GPL-2.0
```

---

## 작성자

```c
MODULE_AUTHOR("ygy");
MODULE_DESCRIPTION("HC-SR501 presence event driver");
```
