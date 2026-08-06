# app/socket_server

센서 클라이언트(sensor_agent_client.c 등)가 접속하는 TCP 서버 역할 + 웹 대시보드용 REST/WebSocket API를 제공하는 Node.js 브릿지.

원래 이 폴더는 로그만 찍는 C 소켓 서버 프로토타입이었는데, 대시보드용 REST/WebSocket까지 필요해지면서 Node.js 브릿지로 교체. 와이어 프로토콜은 기존 C 서버와 동일하게 유지했기 때문에 `sensor_agent_client` 등 기존 클라이언트 코드는 수정 없이 접속 포트만 그대로 쓰면 됨. 

## 프로토콜 (예전 C 버전과 100% 동일)

- 프레이밍: `[4바이트 big-endian 길이][본문 바이트열]`
- 인증: 클라이언트 → `"<id> <passwd>"` 프레임, 서버 → `"AUTH_OK"` / `"AUTH_FAIL"` 프레임
- 인증 후: 클라이언트 → `{"id":"...","occupied":true|false}` 프레임 (상태 변화 시마다)
- heartbeat: 클라이언트 → `{"type":"heartbeat","id":"..."}` 프레임 (30초 주기, 상태와 무관). 서버는 응답하지 않고 `lastSeenAt`만 갱신. 90초 동안 아무 프레임도 없으면 서버가 연결을 강제 종료 (원래 C 서버의 `check_heartbeat_timeouts()`와 동일)
- 계정 목록: `idpasswd.txt` (기존과 동일 포맷, 1~30 / PASSWD)

## 구조

```
src/
  framing.js    프레이밍 인코딩/디코딩
  auth.js       idpasswd.txt 로드 및 인증 체크
  state.js      디바이스별 현재 상태 + 최근 이벤트 로그(최대 500개, 인메모리)
  tcpServer.js  센서 클라이언트가 접속하는 TCP 서버 (예전 C 서버와 동일 프로토콜 구현)
  httpServer.js REST API + WebSocket + 정적 파일 서빙 (의존성: ws 만 사용, express 미사용)
  server.js     entry point
public/index.html    웹 대시보드 (vanilla HTML/CSS/JS, 빌드 스텝 없음)
test/mock_client.js       프로토콜 검증용 모의 센서 클라이언트
test/heartbeat_client.js  heartbeat/타임아웃 검증용 모의 센서 클라이언트
```

## 실행

```bash
cd app/socket_server
npm install
npm start
```

환경변수 (선택):

- `BRIDGE_TCP_PORT` (기본 9000) - 센서 클라이언트 접속 포트
- `BRIDGE_HTTP_PORT` (기본 8080) - REST/WebSocket/대시보드 포트
- `BRIDGE_USERS_FILE` (기본 `./idpasswd.txt`)
- `BRIDGE_HEARTBEAT_TIMEOUT_MS` (기본 90000) - 이 시간 동안 프레임이 없으면 연결 강제 종료
- `BRIDGE_HEARTBEAT_CHECK_INTERVAL_MS` (기본 5000) - 타임아웃 검사 주기

## REST API

- `GET /api/devices` - 디바이스별 현재 상태 `[{id, occupied, lastEventAt, lastSeenAt, connected}]`
- `GET /api/events?limit=100` - 최근 이벤트 로그 `[{id, occupied, ts}]`

## WebSocket (`/ws`)

접속 직후 스냅샷 1회:

```json
{ "type": "snapshot", "devices": [ ... ] }
```

이후 상태 변화마다:

```json
{ "type": "event", "event": {"id":"1","occupied":true,"ts":...}, "device": {...} }
{ "type": "device-update", "device": {...} }
```

`device-update`는 occupied 값 변화 없이 접속/해제(connected)만 바뀔 때도 옵니다.

## 대시보드 UI

`npm start` 후 브라우저에서 `http://<호스트>:8080` 접속 (별도 빌드 불필요, `public/index.html` 하나로 동작).

PIR / RADAR 두 개 탭으로 구성:

- **PIR 탭**
  - 요약 카드: 전체 디바이스 수, 재실 중인 방 수, 센서 연결됨 비율, 최근 이벤트 경과 시간
  - 디바이스 카드 (방 ID별): 재실 여부, 센서 연결 상태(온라인/오프라인), 마지막 이벤트 시각, 현재 상태 지속 시간 (1초마다 갱신)
  - 최근 이벤트 로그: 전체 방 통합, 최신순, 재실 시작/종료 아이콘 구분 (최대 100개)
  - 상단 우측에 브릿지 자체 WebSocket 연결 상태 표시, 끊기면 자동 재연결
  - 페이지 로드시 `GET /api/devices`, `GET /api/events`로 초기 상태를 채우고, 이후는 `/ws`로 실시간 갱신
- **RADAR 탭**
  - 아직 미구현이라 "준비 중" placeholder만 표시. 레이더 point cloud 3D 시각화(Three.js) 예정 위치

## 테스트

```bash
npm start &                 # 브릿지 실행
node test/mock_client.js 1  # id=1 계정으로 접속 -> occupied=true -> 0.5초 후 occupied=false 전송
curl localhost:8080/api/devices
curl localhost:8080/api/events
```

heartbeat/타임아웃 검증 (짧은 타임아웃으로 브릿지 띄우면 90초 안 기다려도 됨):

```bash
BRIDGE_HEARTBEAT_TIMEOUT_MS=2000 BRIDGE_HEARTBEAT_CHECK_INTERVAL_MS=500 npm start &
node test/heartbeat_client.js 1 3 500   # heartbeat 계속 보내서 연결 유지되는지 확인
node test/heartbeat_client.js 2 0       # heartbeat 없이 침묵 -> 타임아웃 후 강제 종료되는지 확인
```

## 다음 단계

- RADAR 탭에 point cloud 3D 시각화(Three.js) 붙이기 - 레이더 브릿지(`bridge/`) 완성 이후
- 필요 시 이벤트 로그를 SQLite 등으로 영속화 (지금은 프로세스 재시작하면 사라지는 인메모리)
