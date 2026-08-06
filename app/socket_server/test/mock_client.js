'use strict';

/**
 * sensor_agent_client.c 가 실제로 하는 것과 동일한 순서로:
 * connect -> "id passwd" 프레임 전송 -> AUTH_OK 대기 -> occupied 이벤트 2회 전송
 * 을 수행하는 테스트용 스크립트. 브릿지가 socket_server.c와 같은 와이어
 * 프로토콜을 제대로 구현했는지 확인하는 용도.
 */

const net = require('net');

const HOST = process.env.HOST || '127.0.0.1';
const PORT = Number.parseInt(process.env.PORT, 10) || 9000;
const ID = process.argv[2] || '1';

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
    console.log(`[mock_client] connected as id=${ID}`);
    writeFrame(socket, `${ID} PASSWD`);

    const authResp = await readFrame(socket);
    console.log(`[mock_client] auth response: ${authResp}`);

    if (authResp !== 'AUTH_OK') {
      socket.end();
      return;
    }

    writeFrame(socket, JSON.stringify({ id: ID, occupied: true }));
    console.log('[mock_client] sent occupied=true');

    await new Promise((r) => setTimeout(r, 500));

    writeFrame(socket, JSON.stringify({ id: ID, occupied: false }));
    console.log('[mock_client] sent occupied=false');

    await new Promise((r) => setTimeout(r, 200));
    socket.end();
  });

  socket.on('error', (err) => console.error('[mock_client] error:', err.message));
  socket.on('close', () => console.log('[mock_client] closed'));
}

main();
