# bridge/src

레이더 브릿지 데몬.
- 담당 칸반: RADAR 브릿지 데몬 초안 / 브릿지 데몬 + 드라이버 통합
- /dev/ttyAMA0 또는 /dev/ttyACM0에서 magic word 동기화 + TLV(Type1/7) 파싱
- sensor_frame_header + point[]로 조립 후 /dev/radar에 write()
