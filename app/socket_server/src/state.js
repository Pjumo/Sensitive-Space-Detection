'use strict';

const { EventEmitter } = require('events');

const MAX_EVENT_LOG = 500;

/**
 * 디바이스별 최신 상태 + 최근 이벤트 로그를 들고 있는 인메모리 저장소.
 * 대시보드가 새로고침해도 "지금 상태"를 바로 알 수 있도록,
 * occupied 이벤트(엣지)만 오더라도 여기서 현재 상태를 유지합니다.
 */
class SensorStateStore extends EventEmitter {
  constructor() {
    super();
    this._devices = new Map(); // id -> { id, occupied, lastEventAt, lastSeenAt }
    this._events = []; // ring buffer, 최신이 뒤쪽
  }

  applyEvent({ id, occupied }) {
    const now = Date.now();

    const prev = this._devices.get(id);
    const device = {
      id,
      occupied: !!occupied,
      lastEventAt: now,
      lastSeenAt: now,
      connected: true,
    };
    this._devices.set(id, device);

    const event = { id, occupied: !!occupied, ts: now };
    this._events.push(event);
    if (this._events.length > MAX_EVENT_LOG) {
      this._events.shift();
    }

    this.emit('event', event);
    this.emit('device-update', device);

    return { device, prev, event };
  }

  markConnected(id, connected) {
    const device = this._devices.get(id);
    if (!device) return;
    device.connected = connected;
    device.lastSeenAt = Date.now();
    this.emit('device-update', device);
  }

  /**
   * heartbeat 등 "살아있다"는 신호만 받았을 때 occupied/lastEventAt은 건드리지 않고
   * lastSeenAt만 갱신합니다. 아직 한 번도 occupied 이벤트를 보낸 적 없는 id면
   * 조용히 무시합니다(대시보드에 반쪽 데이터로 나타나지 않도록).
   */
  touch(id) {
    const device = this._devices.get(id);
    if (!device) return;
    device.lastSeenAt = Date.now();
    this.emit('device-update', device);
  }

  getDevices() {
    return Array.from(this._devices.values());
  }

  getEvents(limit) {
    if (!limit || limit >= this._events.length) return this._events.slice();
    return this._events.slice(this._events.length - limit);
  }
}

module.exports = { SensorStateStore, MAX_EVENT_LOG };
