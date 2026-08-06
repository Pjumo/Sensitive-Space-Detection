'use strict';

const net = require('net');
const { writeFrame, FrameAccumulator } = require('./framing');
const { checkAuth } = require('./auth');

const STATE_AUTH = 'AUTH';
const STATE_READY = 'READY';

/*
 * app/sensor_agent/sensor_agent_client.c 와 옛 socket_server.c 기준.
 * 클라이언트는 상태 변화와 무관하게 30초마다 heartbeat를 보내고,
 * 서버는 90초 동안 아무 프레임도 못 받으면 죽은 연결로 보고 강제 종료합니다.
 * (원래 C 서버의 check_heartbeat_timeouts()와 동일한 동작)
 */
const HEARTBEAT_TIMEOUT_MS =
  Number.parseInt(process.env.BRIDGE_HEARTBEAT_TIMEOUT_MS, 10) || 90 * 1000;
const HEARTBEAT_CHECK_INTERVAL_MS =
  Number.parseInt(process.env.BRIDGE_HEARTBEAT_CHECK_INTERVAL_MS, 10) || 5 * 1000;

/**
 * app/socket_server/socket_server.c 와 동일한 와이어 프로토콜을 구현하는
 * TCP 서버입니다. 센서 클라이언트(sensor_agent_client.c 등)가 그대로
 * 이 서버에 접속할 수 있습니다.
 *
 * 1) client -> server: "<id> <passwd>" (프레임)
 * 2) server -> client: "AUTH_OK" | "AUTH_FAIL" (프레임)
 * 3) client -> server: {"id":"...","occupied":true|false} (프레임, 상태 변화 시)
 * 4) client -> server: {"type":"heartbeat","id":"..."} (프레임, 30초 주기)
 *    - 서버는 여기에 응답을 보내지 않습니다 (원래 프로토콜도 응답 없음).
 *      단지 살아있다는 신호로만 쓰이며, lastSeenAt 갱신 + 90초 타임아웃
 *      판단에만 사용됩니다.
 */
function createTcpServer({ users, stateStore, logger = console }) {
  // 현재 연결된 소켓들을 추적해서 주기적으로 heartbeat 타임아웃을 검사합니다.
  const activeConnections = new Set();

  const server = net.createServer((socket) => {
    const remote = `${socket.remoteAddress}:${socket.remotePort}`;
    let clientState = STATE_AUTH;
    let clientId = null;

    const conn = { socket, remote, lastSeen: Date.now(), get clientId() { return clientId; } };
    activeConnections.add(conn);

    logger.log(`[tcp] client connected: ${remote}`);

    const framer = new FrameAccumulator({
      onFrame: (text) => handleFrame(text),
      onError: (reason) => {
        logger.warn(`[tcp] ${remote} 프레임 오류(${reason}), 연결 종료`);
        socket.destroy();
      },
    });

    function handleFrame(text) {
      if (clientState === STATE_AUTH) {
        const parts = text.trim().split(/\s+/);

        if (parts.length !== 2) {
          logger.warn(`[tcp] ${remote} 인증 메시지 형식 오류, 연결 종료`);
          socket.destroy();
          return;
        }

        const [id, passwd] = parts;

        if (!checkAuth(users, id, passwd)) {
          logger.warn(`[tcp] 인증 실패 (id=${id}), 연결 종료`);
          writeFrame(socket, 'AUTH_FAIL');
          socket.end();
          return;
        }

        clientId = id;
        clientState = STATE_READY;

        logger.log(`[tcp] 인증 성공 (id=${clientId}, ${remote})`);
        writeFrame(socket, 'AUTH_OK');
        stateStore.markConnected(clientId, true);
        return;
      }

      // STATE_READY: JSON 페이로드 처리
      let msg;
      try {
        msg = JSON.parse(text);
      } catch (err) {
        logger.warn(`[tcp] ${remote} JSON 파싱 실패: ${text}`);
        return;
      }

      // heartbeat: 응답 없음, lastSeenAt만 갱신. occupied 이벤트로 취급하지 않음.
      if (msg.type === 'heartbeat') {
        if (typeof msg.id === 'string') {
          stateStore.touch(msg.id);
        }
        return;
      }

      if (typeof msg.id !== 'string' || typeof msg.occupied !== 'boolean') {
        logger.warn(`[tcp] ${remote} 페이로드 형식 오류: ${text}`);
        return;
      }

      logger.log(
        `[tcp] 수신 (id=${msg.id}): occupied=${msg.occupied}`
      );

      stateStore.applyEvent({ id: msg.id, occupied: msg.occupied });
    }

    socket.on('data', (chunk) => {
      conn.lastSeen = Date.now(); // 원래 C 서버처럼 프레임 종류와 무관하게 수신 시마다 갱신
      framer.push(chunk);
    });

    socket.on('close', () => {
      activeConnections.delete(conn);
      logger.log(`[tcp] client disconnected: ${remote} (id=${clientId ?? '-'})`);
      if (clientId) stateStore.markConnected(clientId, false);
    });

    socket.on('error', (err) => {
      logger.warn(`[tcp] socket error (${remote}): ${err.message}`);
    });
  });

  const heartbeatTimer = setInterval(() => {
    const now = Date.now();

    for (const conn of activeConnections) {
      if (now - conn.lastSeen >= HEARTBEAT_TIMEOUT_MS) {
        logger.warn(
          `[tcp] ${conn.remote} (id=${conn.clientId ?? '-'}) heartbeat 타임아웃, 연결 강제 종료`
        );
        conn.socket.destroy();
      }
    }
  }, HEARTBEAT_CHECK_INTERVAL_MS);

  heartbeatTimer.unref(); // 이 타이머 때문에 프로세스 종료가 막히지 않도록

  server.on('close', () => clearInterval(heartbeatTimer));

  return server;
}

module.exports = { createTcpServer };
