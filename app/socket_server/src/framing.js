'use strict';

/**
 * socket_server.c 와 동일한 프레이밍 프로토콜.
 * [4바이트 big-endian 길이][본문 바이트열(길이만큼, null 종단 없음)]
 */

const MAX_MSG_SIZE = 4096;

/**
 * 소켓으로 프레임 하나를 씁니다 (문자열 -> UTF-8 바이트).
 */
function writeFrame(socket, text) {
  const payload = Buffer.from(text, 'utf8');
  const header = Buffer.alloc(4);
  header.writeUInt32BE(payload.length, 0);
  socket.write(Buffer.concat([header, payload]));
}

/**
 * 스트림에서 들어오는 바이트를 누적하고, 완성된 프레임이 생길 때마다
 * onFrame(text)를 호출하는 누적기.
 * 비정상 길이(0 또는 MAX_MSG_SIZE 초과)를 만나면 onError(reason)을 호출합니다.
 */
class FrameAccumulator {
  constructor({ onFrame, onError }) {
    this._buf = Buffer.alloc(0);
    this._onFrame = onFrame;
    this._onError = onError;
  }

  push(chunk) {
    this._buf = this._buf.length ? Buffer.concat([this._buf, chunk]) : chunk;

    for (;;) {
      if (this._buf.length < 4) return;

      const len = this._buf.readUInt32BE(0);

      if (len === 0 || len > MAX_MSG_SIZE) {
        this._onError(`invalid frame length: ${len}`);
        return;
      }

      if (this._buf.length < 4 + len) return; // 본문이 아직 다 안 옴

      const payload = this._buf.subarray(4, 4 + len).toString('utf8');
      this._buf = this._buf.subarray(4 + len);

      this._onFrame(payload);
    }
  }
}

module.exports = { MAX_MSG_SIZE, writeFrame, FrameAccumulator };
