# app/sensor_agent

PIR/Radar 센서 이벤트를 읽어서 재실(occupancy) 여부를 판단하고, 결과를 서버로 전송하는 유저스페이스 에이전트.

common/include/presence_uapi.h를 기준으로, 커널 드라이버(pir_dev.ko 등)가 제공하는
`presence_event`를 읽어 판단 로직을 수행한다.

## 구성 파일

| 파일 | 역할 |
|---|---|
| `sensor_agent.c` | mock backend 기반 로컬 테스트용. 네트워크 전송 없이 콘솔 출력만 함 |
| `sensor_agent_client.c` | 실제 서비스용. 인증 + occupancy 판단 + 서버 전송까지 수행 |
| `mock/` | 하드웨어 없이 가짜 PIR 이벤트를 생성하는 backend |
| `real/` | 실제 `/dev/pir_dev`를 열어 커널 드라이버 이벤트를 받는 backend |
| `Makefile` | sensor_agent, sensor_agent_client 둘 다 빌드 |

## 빌드

```bash
make            # sensor_agent, sensor_agent_client 둘 다 빌드
make clean      # 빌드 결과물 정리
```

크로스컴파일 시:
```bash
make CC=aarch64-linux-gnu-gcc
```

## 실행

### mock 기반 로컬 테스트 (하드웨어/서버 없이)
```bash
./sensor_agent
```

### 실제 서비스 실행
```bash
./sensor_agent_client <서버IP> <포트> <ID>
# 예: ./sensor_agent_client 10.10.16.69 5000 1
```

`<ID>`는 서버의 `idpasswd.txt`에 등록된 ID여야 하며, 비밀번호는 클라이언트 코드에
`CLIENT_PASSWD`로 고정되어 있음.

## Occupancy 판단 알고리즘

HC-SR501은 Repeatable Trigger 모드로 설정되어 있음을 전제로 함
(움직임 감지 시마다 HIGH 유지 시간이 갱신됨).

PIR OUT이 HIGH로 바뀜 (ASSERTED 이벤트)
→ occupied=false였다면 → true로 전환, 서버에 전송

PIR OUT이 LOW로 떨어짐 (DEASSERTED 이벤트)
→ occupied=true였다면 → 그 시점부터 타이머 시작 (OCCUPANCY_TIMEOUT_SEC)

타이머가 도는 중 다시 ASSERTED가 오면 → 타이머 취소
타이머가 OCCUPANCY_TIMEOUT_SEC(기본 5초)를 넘기면
→ occupied=false로 전환, 서버에 전송


`real_backend_wait_read()`는 poll()에 1초 타임아웃을 걸어 반복 호출되며,
이벤트가 없어도 매초 타이머를 확인하므로 사람이 나간 뒤에도 지연 없이
occupancy=false 판정이 이루어진다.

## 서버 전송 메시지 형식

인증 (연결 직후 1회):

"<id> <password>"

응답: `AUTH_OK` 또는 `AUTH_FAIL`

상태 전송 (occupancy 전환 시):
```json
{"id":"<id>","occupied":true}
{"id":"<id>","occupied":false}
```

Heartbeat (30초 주기, 상태와 무관하게 전송):
```json
{"type":"heartbeat","id":"<id>"}
```

모든 메시지는 길이-프리픽스 프레이밍(4바이트 빅엔디언 길이 + 본문)으로 전송된다.

## 안정성 관련 설정값

| 매크로 | 위치 | 기본값 | 설명 |
|---|---|---|---|
| `OCCUPANCY_TIMEOUT_SEC` | sensor_agent_client.c | 5 | LOW 이후 재실 없음 판정까지 대기 시간(초) |
| `MIN_SEND_INTERVAL_SEC` | sensor_agent_client.c | 1 | 상태 메시지 전송 최소 간격(rate limiting) |
| `HEARTBEAT_INTERVAL_SEC` | sensor_agent_client.c | 30 | heartbeat 전송 주기(초) |

## 의존성

- `common/include/presence_uapi.h` (PIR/Radar 공통 UAPI, 컴파일 시 `-I` 경로로 참조)
- real backend 사용 시 `/dev/pir_dev` 장치 파일 (커널 드라이버 insmod 필요)
