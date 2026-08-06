'use strict';

/**
 * heartbeat 프레임 처리 + 90초(BRIDGE_HEARTBEAT_TIMEOUT_MS) 타임아웃 강제종료를
 * 검증하기 위한 모의 센서 클라이언트.
 *
 * 사용법:
 *   node test/heartbeat_client.js <id> <보낼 heartbeat 횟수> <heartbeat 간격(ms)>
 *
 * 예시:
 *   # heartbeat 계속 보내서 연결 유지되는지 확인
 *   node test/heartbeat_client.js 1 5 25000
 *
 *   # heartbeat 아예 안 보내고 침묵 -> 서버가 90초 뒤 강제 종료하는지 확인
 *   node test/heartbeat_client.js 2 0
 *
 * 90초를 실제로 기다리기 번거로우면, 서버를 짧은 타임아웃으로 띄워서 테스트하면 됩니다:
 *   BRIDGE_HEARTBEAT_TIMEOUT_MS=2000 BRIDGE_HEARTBEAT_CHECK_INTERVAL_MS=500 npm start
 */

const net = require('net');

const HOST = process.env.HOST || '127.0.0.1';
const PORT = Number.parseInt(process.env.PORT, 10) || 9000;
const ID = process.argv[2] || '1';
const HEARTBEAT_COUNT = Number.parseInt(process.argv[3], 10) || 0;
const HEARTBEAT_INTERVAL_MS = Number.parseInt(process.argv[4], 10) || 30000;

function writeFrame(socket, text) {
  const payload = Buffer.from(text, 'utf8');
  const header = Buffer.alloc(4);
  header.writeUInt32BE(payload.length, 0);
  socket.write(Buffer.concat([header, payload]));
}

function readFrame(socket) {
  return new Promise((resolve) => {
    let buf = Buffer.alloc(0);
    function onData(chunk) {
      buf = Buffer.concat([buf, chunk]);
      if (buf.length < 4) return;
      const len = buf.readUInt32BE(0);
      if (buf.length < 4 + len) return;
      socket.removeListener('data', onData);
      resolve(buf.subarray(4, 4 + len).toString('utf8'));
    }
    socket.on('data', onData);
  });
}

async function main() {
  const socket = net.createConnection({ host: HOST, port: PORT }, async () => {
    console.log(`[heartbeat_client] connected as id=${ID}`);
    writeFrame(socket, `${ID} PASSWD`);

    const authResp = await readFrame(socket);
    console.log(`[heartbeat_client] auth response: ${authResp}`);

    if (authResp !== 'AUTH_OK') {
      socket.end();
      return;
    }

    // 대시보드에 디바이스가 먼저 등록되도록 occupied 이벤트 한 번 전송
    writeFrame(socket, JSON.stringify({ id: ID, occupied: true }));
    console.log('[heartbeat_client] sent occupied=true');

    for (let i = 1; i <= HEARTBEAT_COUNT; i++) {
      await new Promise((r) => setTimeout(r, HEARTBEAT_INTERVAL_MS));
      writeFrame(socket, JSON.stringify({ type: 'heartbeat', id: ID }));
      console.log(`[heartbeat_client] sent heartbeat #${i}`);
    }

    if (HEARTBEAT_COUNT === 0) {
      console.log('[heartbeat_client] heartbeat 없이 침묵 - 서버 타임아웃 강제종료 대기 중...');
    }
  });

  socket.on('error', (err) =>
    console.error('[heartbeat_client] error:', err.message)
  );
  socket.on('close', () => console.log('[heartbeat_client] connection closed'));
}

main();
