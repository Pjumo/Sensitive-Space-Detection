'use strict';

const path = require('path');
const { loadUsers } = require('./auth');
const { SensorStateStore } = require('./state');
const { createTcpServer } = require('./tcpServer');
const { createHttpServer } = require('./httpServer');

const TCP_PORT = Number.parseInt(process.env.BRIDGE_TCP_PORT, 10) || 9000;
const HTTP_PORT = Number.parseInt(process.env.BRIDGE_HTTP_PORT, 10) || 8080;
const USERS_FILE =
  process.env.BRIDGE_USERS_FILE ||
  path.join(__dirname, '..', 'idpasswd.txt');

function main() {
  const users = loadUsers(USERS_FILE);
  console.log(`[bridge] ${users.size}명의 센서 계정 로드 완료 (${USERS_FILE})`);

  const stateStore = new SensorStateStore();

  const tcpServer = createTcpServer({ users, stateStore, logger: console });
  tcpServer.listen(TCP_PORT, () => {
    console.log(
      `[bridge] TCP 서버 대기 중 (port=${TCP_PORT}) - 센서 클라이언트 접속용`
    );
  });

  const httpServer = createHttpServer({ stateStore, logger: console });
  httpServer.listen(HTTP_PORT, () => {
    console.log(
      `[bridge] HTTP/WS 서버 대기 중 (port=${HTTP_PORT}) - 대시보드용`
    );
    console.log(`[bridge]   REST: http://localhost:${HTTP_PORT}/api/devices`);
    console.log(`[bridge]   WS  : ws://localhost:${HTTP_PORT}/ws`);
  });

  process.on('SIGINT', () => {
    console.log('\n[bridge] 종료 중...');
    tcpServer.close();
    httpServer.close();
    process.exit(0);
  });
}

main();
