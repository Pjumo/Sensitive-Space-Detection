# common/sensor_backend

sensor_backend_t 추상화 라이브러리.
- 담당 칸반: UserSpace 이론 및 방향성 설정, 사용자 앱 단 방향성
- /dev/pir, /dev/radar(or mock)를 동일한 open/read/poll/ioctl 인터페이스로 감싸는 공용 라이브러리
- app/sensor_agent, drivers/*/user 테스트 유틸 모두 이 라이브러리를 링크
