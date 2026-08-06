# drivers/radar/src

Radar 커널 캐릭터 디바이스(/dev/radar) 드라이버 소스.
- 담당 칸반: Radar 드라이버 구현 / Radar 드라이버 완성 / Radar 실물 연동 / Device Driver 로깅·디버깅
- 브릿지 데몬의 write()를 트리거로 wake_up_interruptible(), read/poll/ioctl 제공

# Radar Mock Driver

TI IWR6843 레이더 센서가 아직 준비되지 않은 상태에서, PIR 드라이버와 동일한
UserSpace 인터페이스(`presence_uapi.h`)로 상위 애플리케이션을 먼저 개발·검증할
수 있도록 만든 목(mock) 캐릭터 디바이스 드라이버입니다.

## 배경

`common/include/presence_uapi.h`에 PIR/Radar가 공통으로 사용하는
`presence_event` 구조체와 `ioctl` 인터페이스가 정의되어 있습니다.
이 드라이버는 실제 하드웨어 대신 **커널 타이머로 가짜 거리값을 주기적으로
생성**하여, 마치 진짜 레이더가 붙어있는 것처럼 `/dev/radar_dev`를 통해
`presence_event`를 내보냅니다.

`pir_dev.c`의 인터럽트 핸들러(`pirIntHandler`) 자리를 커널 타이머 콜백
(`radarSampleTimerFn`)으로 대체했을 뿐, FIFO 관리·`read()`/`poll()`/`ioctl()`
로직은 PIR과 동일한 구조를 그대로 재사용합니다.

## 동작 방식

1. `radar_period_ms`(기본 300ms)마다 타이머가 깨어나 가짜 거리값(cm)을 생성
   - 이전 값에서 ±20cm 범위로 랜덤워크하여 자연스러운 접근/이탈 패턴 흉내
2. 거리값이 `radar_threshold_cm`(기본 150cm)보다 가까우면 "감지됨"으로 판단
3. 감지 상태가 바뀔 때만(edge-triggered) `presence_event`를 생성해 FIFO에 저장
4. `wake_up_interruptible()`로 대기 중인 `read()`/`poll()` 깨움

## 빌드

Ubuntu 크로스컴파일 환경(NFS 공유 폴더)에서:

```bash
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
```

## 실행 (Pi에서)

```bash
sudo insmod radar_drv.ko
sudo mknod /dev/radar_dev c 231 0
sudo chmod 666 /dev/radar_dev
sudo dmesg | tail -5
```

모듈 파라미터로 시뮬레이션 값 조정 가능:

```bash
sudo insmod radar_drv.ko radar_period_ms=500 radar_threshold_cm=200
```

## 알려진 이슈 / TODO

- [ ] 설계 문서상 FIFO 크기는 64로 명시되어 있으나 `pir_dev.c`는 32로 구현되어
      있어 두 드라이버 간 불일치가 있음 (문서 또는 PIR 코드 중 하나로 통일 필요)
- [ ] `device_create()`/`class_create()`가 없어 `/dev/radar_dev` 노드를
      수동으로 `mknod` 해줘야 함 — udev 자동 생성으로 개선 예정
- [ ] 실제 TI IWR6843 연동 시 `radarSampleTimerFn()`을 SPI/UART 수신
      콜백으로 교체 필요 (FIFO/read/poll/ioctl 로직은 재사용)

## 참고

- 공통 인터페이스 정의: `common/include/presence_uapi.h`
- 자매 드라이버(PIR): `drivers/pir/src/pir_dev.c`
