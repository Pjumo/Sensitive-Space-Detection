# drivers/radar/mock

실물 IWR6843 없이 통합 테스트하기 위한 RADAR mock 드라이버.
- 담당 칸반: RADAR mock 드라이버 제작 / Radar mock 통합 데모
- 실제 /dev/radar와 동일한 read/poll/ioctl 인터페이스를 가짜 데이터로 제공
