# drivers/radar/src

Radar 커널 캐릭터 디바이스(/dev/radar) 드라이버 소스.
- 담당 칸반: Radar 드라이버 구현 / Radar 드라이버 완성 / Radar 실물 연동 / Device Driver 로깅·디버깅
- 브릿지 데몬의 write()를 트리거로 wake_up_interruptible(), read/poll/ioctl 제공
