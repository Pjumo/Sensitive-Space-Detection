'use strict';

const net = require('net');
const { writeFrame, FrameAccumulator } = require('./framing');
const { checkAuth } = require('./auth');

const STATE_AUTH = 'AUTH';
const STATE_READY = 'READY';

/**
 * app/socket_server/socket_server.c 와 동일한 와이어 프로토콜을 구현하는
 * TCP 서버입니다. 센서 클라이언트(sensor_agent_client.c 등)가 그대로
 * 이 서버에 접속할 수 있습니다.
 *
 * 1) client -> server: "<id> <passwd>" (프레임)
 * 2) server -> client: "AUTH_OK" | "AUTH_FAIL" (프레임)
 * 3) client -> server: {"id":"...","occupied":true|false} (프레임, 반복)
 */
function createTcpServer({ users, stateStore, logger = console }) {
  const server = net.createServer((socket) => {
    const remote = `${socket.remoteAddress}:${socket.remotePort}`;
    let clientState = STATE_AUTH;
    let clientId = null;

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

      if (typeof msg.id !== 'string' || typeof msg.occupied !== 'boolean') {
        logger.warn(`[tcp] ${remote} 페이로드 형식 오류: ${text}`);
        return;
      }

      logger.log(
        `[tcp] 수신 (id=${msg.id}): occupied=${msg.occupied}`
      );

      stateStore.applyEvent({ id: msg.id, occupied: msg.occupied });
    }

    socket.on('data', (chunk) => framer.push(chunk));

    socket.on('close', () => {
      logger.log(`[tcp] client disconnected: ${remote} (id=${clientId ?? '-'})`);
      if (clientId) stateStore.markConnected(clientId, false);
    });

    socket.on('error', (err) => {
      logger.warn(`[tcp] socket error (${remote}): ${err.message}`);
    });
  });

  return server;
}

module.exports = { createTcpServer };
