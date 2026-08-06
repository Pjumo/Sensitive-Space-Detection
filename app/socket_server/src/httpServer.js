'use strict';

const fs = require('fs');
const path = require('path');
const http = require('http');
const { WebSocketServer } = require('ws');

const PUBLIC_DIR = path.join(__dirname, '..', 'public');

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
};

function sendJson(res, statusCode, data) {
  const body = JSON.stringify(data);
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
  });
  res.end(body);
}

/**
 * public/ 아래 정적 파일을 서빙합니다 (express.static 대체, 의존성 최소화 목적).
 * 디렉터리 탈출(../) 방지를 위해 resolve 후 PUBLIC_DIR 하위인지 검증합니다.
 */
function serveStatic(req, res) {
  const urlPath = decodeURIComponent(req.url.split('?')[0]);
  const relative = urlPath === '/' ? 'index.html' : urlPath.replace(/^\/+/, '');
  const filePath = path.resolve(PUBLIC_DIR, relative);

  if (!filePath.startsWith(PUBLIC_DIR)) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }

  fs.readFile(filePath, (err, content) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
      res.end('Not Found');
      return;
    }

    const ext = path.extname(filePath);
    res.writeHead(200, {
      'Content-Type': MIME_TYPES[ext] || 'application/octet-stream',
    });
    res.end(content);
  });
}

/**
 * REST API + WebSocket + 정적 대시보드 파일 서빙 (의존성: ws 만 사용).
 *
 * REST:
 *   GET /api/devices        -> 디바이스별 현재 상태 목록
 *   GET /api/events?limit=n -> 최근 이벤트 로그 (기본 100개)
 *
 * WebSocket (/ws):
 *   접속 직후: { type: "snapshot", devices: [...] }
 *   상태 변화마다: { type: "event", event: {...}, device: {...} }
 */
function createHttpServer({ stateStore, logger = console }) {
  const server = http.createServer((req, res) => {
    const [pathname, queryString] = req.url.split('?');

    if (req.method === 'GET' && pathname === '/api/devices') {
      sendJson(res, 200, stateStore.getDevices());
      return;
    }

    if (req.method === 'GET' && pathname === '/api/events') {
      const params = new URLSearchParams(queryString || '');
      const limit = Number.parseInt(params.get('limit'), 10) || 100;
      sendJson(res, 200, stateStore.getEvents(limit));
      return;
    }

    if (req.method === 'GET') {
      serveStatic(req, res);
      return;
    }

    res.writeHead(404);
    res.end();
  });

  const wss = new WebSocketServer({ server, path: '/ws' });

  function broadcast(payload) {
    const data = JSON.stringify(payload);
    for (const client of wss.clients) {
      if (client.readyState === client.OPEN) {
        client.send(data);
      }
    }
  }

  wss.on('connection', (ws) => {
    logger.log('[ws] dashboard client connected');

    ws.send(
      JSON.stringify({
        type: 'snapshot',
        devices: stateStore.getDevices(),
      })
    );

    ws.on('close', () => logger.log('[ws] dashboard client disconnected'));
  });

  stateStore.on('event', (event) => {
    const device = stateStore.getDevices().find((d) => d.id === event.id);
    broadcast({ type: 'event', event, device });
  });

  stateStore.on('device-update', (device) => {
    broadcast({ type: 'device-update', device });
  });

  return server;
}

module.exports = { createHttpServer };
