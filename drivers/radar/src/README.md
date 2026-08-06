# drivers/radar/src

Radar 커널 캐릭터 디바이스(/dev/radar) 드라이버 소스.
- 담당 칸반: Radar 드라이버 구현 / Radar 드라이버 완성 / Radar 실물 연동 / Device Driver 로깅·디버깅
- 브릿지 데몬의 write()를 트리거로 wake_up_interruptible(), read/poll/ioctl 제공

# Radar Mock Driver

TI IWR6843 레이더 보드와 레이더 알고리즘이 준비되기 전에, UserSpace 개발을
먼저 진행할 수 있도록 만든 목(mock) 캐릭터 디바이스 드라이버입니다.

## 목적

실제 레이더 드라이버와 **완전히 동일한 인터페이스**(`/dev/radar_presence`,
`presence_uapi.h`의 이벤트 구조체·`ioctl`)를 제공하여, UserSpace 프로그램
입장에서 mock인지 실제 드라이버인지 구분할 필요가 없도록 합니다.

```c
fd = open("/dev/radar_presence", O_RDONLY);
poll(...);
read(fd, &event, sizeof(event));
```

이 코드는 mock이 로드되어 있든, 나중에 실제 레이더 드라이버가 로드되든
동일하게 동작합니다. **두 드라이버를 동시에 로드하지 않고, 상황에 따라
하나만 사용**하는 것을 전제로 합니다.

## 동작 방식

`delayed_work`를 이용한 결정론적 상태 머신으로 동작합니다.

```
period_ms(기본 1000ms)마다 radarWorkFn() 실행
  → 현재 phase(occupied/empty) 경과 시간 누적
  → phase 지속시간을 넘으면 상태 반전 + presence_event 생성
  → kfifo에 저장
  → wake_up_interruptible()로 poll()/read() 깨움
  → schedule_delayed_work()로 다음 tick 예약
```

기본 설정 시 다음 패턴이 반복됩니다:

```
5초 동안 사람 없음 (empty)
10초 동안 사람 있음 (occupied)
다시 5초 동안 사람 없음
... 반복
```

이벤트는 상태가 "바뀌는 순간"에만 생성됩니다 (edge-triggered) — PIR
드라이버와 동일한 `PRESENCE_EVENT_ASSERTED`/`PRESENCE_EVENT_DEASSERTED`
의미를 사용합니다.

## 모듈 파라미터

| 파라미터 | 기본값 | 설명 |
|---|---|---|
| `period_ms` | 1000 | 상태 체크(tick) 주기 |
| `occupied_duration` | 10 | "있음" 상태 지속 시간(초) |
| `empty_duration` | 5 | "없음" 상태 지속 시간(초) |

```bash
sudo insmod radar_mock_drv.ko period_ms=500 occupied_duration=3 empty_duration=2
```

빠르게 반복되는 패턴으로 테스트하고 싶을 때 위처럼 값을 줄여서 로드하면
편합니다.

## 빌드

Pi 로컬 빌드:
```bash
make
```

Ubuntu 크로스컴파일:
```bash
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KDIR=$(HOME)/pi_bsp/kernel/linux
```

## 실행

```bash
sudo insmod radar_mock_drv.ko
ls -l /dev/radar_presence     # class_create/device_create로 자동 생성됨, mknod 불필요
sudo dmesg | tail -5          # "radar_presence: ready ..." 로그 확인
```

모듈 해제:
```bash
sudo rmmod radar_mock_drv
```

## 구현 세부사항

- **`/dev/radar_presence` 자동 생성**: `alloc_chrdev_region`(동적 major) +
  `class_create`/`device_create`를 사용하여, 모듈 로드 시 udev가 자동으로
  디바이스 노드를 만듭니다. 수동 `mknod`가 필요 없습니다.
- **`kfifo` 기반 이벤트 큐**: 커널 표준 FIFO 자료구조를 사용합니다
  (`RADAR_FIFO_DEPTH = 64`, 2의 거듭제곱 필수). FIFO가 가득 차면 가장
  오래된 이벤트를 버리고 새 이벤트를 넣으며, `dropped_events` 통계에 반영됩니다.
- **`delayed_work` 사용 이유**: 커널 타이머(하드 IRQ 컨텍스트)와 달리
  워크큐는 프로세스 컨텍스트에서 실행되어 sleep 가능한 작업을 할 수
  있습니다. 실제 IWR6843을 SPI/UART로 연동할 때 통신 자체가 sleep을
  필요로 하므로, 이 구조를 그대로 유지한 채 `radarWorkFn()` 내부 로직만
  교체하면 됩니다.

## 실제 하드웨어 연동 시 변경 지점

`radarWorkFn()` 함수 하나만 교체 대상입니다:

- 현재: 결정론적 타이머로 가짜 occupied/empty 상태 생성
- 실제: SPI/UART로 IWR6843 원시 데이터 수신 → 포인트클라우드/DBSCAN
  클러스터링 결과로 occupancy 판단

`kfifo` 저장, `wake_up_interruptible()`, `read()`/`poll()`/`ioctl()`
로직은 전부 그대로 재사용합니다.

## 알려진 이슈 / TODO

- [ ] PIR 드라이버(`pir_dev.c`)는 아직 `register_chrdev` + 수동 `mknod` +
      `/dev/pir_dev` 이름을 쓰고 있어, 이 드라이버(`cdev`+자동생성+
      `/dev/pir_presence`)와 스타일이 다릅니다. 팀 컨벤션 통일 필요.
- [ ] 설계 문서상 PIR도 FIFO 크기 64로 명시되어 있으나 `pir_dev.c`는
      32로 구현되어 있어 두 드라이버 간 불일치가 있습니다.
- [ ] 실제 레이더 연동 시 `occupied_duration`/`empty_duration` 개념을
      어떻게 매핑할지(예: 알고리즘 confidence threshold와의 관계) 설계
      필요.

## 참고

- 공통 인터페이스 정의: `common/include/presence_uapi.h`
- 자매 드라이버(PIR): `drivers/pir/src/pir_dev.c`
