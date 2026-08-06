# app/socket_server

sensor_agent_client로부터 재실(occupancy) 상태를 받는 중앙 수신 서버.
ID/비밀번호 인증과 epoll 기반 멀티클라이언트 처리, heartbeat 타임아웃 감지를 지원한다.

## 파일

| 파일 | 역할 |
|---|---|
| `socket_server.c` | 서버 본체 |
| `idpasswd.txt` | 인증용 ID/비밀번호 목록 (한 줄에 "ID 비밀번호") |
| `Makefile` | 빌드 |

## 빌드 및 실행

```
make
./socket_server <포트>
```

예: `./socket_server 5000`

## 프로토콜

모든 메시지는 길이-프리픽스 프레이밍(4바이트 빅엔디언 길이 + 본문)으로 전송된다.

```
클라이언트 접속
→ "<id> <password>" 전송 (인증)
→ 서버가 idpasswd.txt와 대조
→ 성공: "AUTH_OK" 응답, 이후 일반 메시지 루프 진입
→ 실패: "AUTH_FAIL" 응답, 연결 종료

인증 이후 클라이언트가 보내는 메시지:
{"id":"<id>","occupied":true} - 재실 있음 전환
{"id":"<id>","occupied":false} - 재실 없음 전환
{"type":"heartbeat","id":"<id>"} - 30초 주기 생존 신호
```

## 멀티클라이언트 구조 (epoll)

여러 방 노드(sensor_agent_client)가 동시에 접속할 수 있도록 epoll 기반으로 구현됨.

```
listen_fd, 각 client_fd를 모두 epoll에 등록
epoll_wait(..., 5000) → 최대 5초 주기로 항상 깨어남 (이벤트 없어도)
→ check_heartbeat_timeouts() 매 루프 실행
→ 이벤트가 있으면 accept() 또는 handle_client_readable() 처리
```

각 클라이언트는 `client_t` 구조체로 독립적인 상태(인증 여부, 수신 버퍼, 마지막 수신 시각)를
관리하므로, 한 클라이언트가 논블로킹 소켓에서 부분적으로만 데이터를 받아도
다른 클라이언트 처리에 영향을 주지 않는다.

## Heartbeat 타임아웃 감지

클라이언트로부터 어떤 메시지든(heartbeat 포함) 수신할 때마다 `last_seen`을 갱신한다.
90초(`HEARTBEAT_TIMEOUT_SEC`) 이상 아무 메시지도 없으면, TCP 연결이 살아있는 것처럼
보여도 죽은 클라이언트로 간주하고 강제로 연결을 끊는다.

이는 클라이언트 프로세스가 멈추거나(hang) 응답 불능 상태가 되었을 때,
TCP 자체 메커니즘만으로는 감지되지 않는 상황을 잡아내기 위함이다.

## 인증 파일 (idpasswd.txt) 형식

```
1 PASSWD
2 PASSWD
...
30 PASSWD
```

한 줄에 "ID 비밀번호"를 공백으로 구분. 서버 시작 시 한 번 읽어서 메모리에 로드한다.

## 설정값

| 매크로 | 기본값 | 설명 |
|---|---|---|
| `MAX_CLIENTS` | 64 | 동시 접속 가능한 최대 클라이언트 수 |
| `HEARTBEAT_TIMEOUT_SEC` | 90 | 마지막 메시지로부터 이 시간 넘으면 강제 종료 |
| `MAX_MSG_SIZE` | 4096 | 메시지 본문 최대 크기 |
