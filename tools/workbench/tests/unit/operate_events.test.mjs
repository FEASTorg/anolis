import test from "node:test";
import assert from "node:assert/strict";

import {
  appendEventTrace,
  buildTraceEvent,
  createOperateEventStreamManager,
} from "../../frontend/js/operate/events.js";

class FakeEventSource {
  static OPEN = 1;
  static CLOSED = 2;
  static instances = [];

  constructor(url) {
    this.url = url;
    this.readyState = 0;
    this.listeners = new Map();
    this.onopen = null;
    this.onerror = null;
    this.closed = false;
    FakeEventSource.instances.push(this);
  }

  addEventListener(type, handler) {
    if (!this.listeners.has(type)) {
      this.listeners.set(type, []);
    }
    this.listeners.get(type).push(handler);
  }

  emit(type, payload) {
    const handlers = this.listeners.get(type) || [];
    const event = {
      data: typeof payload === "string" ? payload : JSON.stringify(payload),
    };
    for (const handler of handlers) {
      handler(event);
    }
  }

  open() {
    this.readyState = FakeEventSource.OPEN;
    if (typeof this.onopen === "function") {
      this.onopen();
    }
  }

  fail() {
    this.readyState = FakeEventSource.CLOSED;
    if (typeof this.onerror === "function") {
      this.onerror(new Error("stream failure"));
    }
  }

  close() {
    this.readyState = FakeEventSource.CLOSED;
    this.closed = true;
  }
}

function createScheduler() {
  let nextId = 1;
  const timeouts = new Map();
  const intervals = new Map();

  return {
    setTimeoutFn(callback, delayMs) {
      const id = nextId++;
      timeouts.set(id, { callback, delayMs });
      return id;
    },
    clearTimeoutFn(id) {
      timeouts.delete(id);
    },
    setIntervalFn(callback, delayMs) {
      const id = nextId++;
      intervals.set(id, { callback, delayMs });
      return id;
    },
    clearIntervalFn(id) {
      intervals.delete(id);
    },
    runIntervalPass() {
      for (const { callback } of intervals.values()) {
        callback();
      }
    },
    runFirstTimeout() {
      const first = timeouts.entries().next();
      if (first.done) {
        return false;
      }
      const [id, timer] = first.value;
      timeouts.delete(id);
      timer.callback();
      return true;
    },
    timeoutCount() {
      return timeouts.size;
    },
  };
}

test("appendEventTrace keeps ring buffer bounded", () => {
  const buffer = [];
  appendEventTrace(buffer, { type: "a" }, 3);
  appendEventTrace(buffer, { type: "b" }, 3);
  appendEventTrace(buffer, { type: "c" }, 3);
  appendEventTrace(buffer, { type: "d" }, 3);

  assert.deepEqual(buffer.map((entry) => entry.type), ["b", "c", "d"]);
});

test("buildTraceEvent creates readable details for known event families", () => {
  const mode = buildTraceEvent("mode_change", {
    previous_mode: "MANUAL",
    new_mode: "AUTO",
    timestamp_ms: 50,
  });
  const provider = buildTraceEvent("provider_health_change", {
    provider_id: "sim0",
    state: "UNAVAILABLE",
    timestamp_ms: 100,
  });

  assert.equal(mode.details, "MANUAL -> AUTO");
  assert.equal(provider.details, "sim0: UNAVAILABLE");
  assert.equal(provider.timestamp_ms, 100);
});

test("event stream manager handles connected, stale, reconnecting, and disconnected states", () => {
  FakeEventSource.instances = [];
  const scheduler = createScheduler();
  const statuses = [];
  const events = [];
  let nowMs = 0;

  const manager = createOperateEventStreamManager({
    EventSourceImpl: FakeEventSource,
    onConnectionStatus: (status) => {
      statuses.push(status);
    },
    onEvent: (eventType, payload) => {
      events.push({ eventType, payload });
    },
    nowFn: () => nowMs,
    staleAfterMs: 1000,
    staleCheckIntervalMs: 100,
    setTimeoutFn: scheduler.setTimeoutFn,
    clearTimeoutFn: scheduler.clearTimeoutFn,
    setIntervalFn: scheduler.setIntervalFn,
    clearIntervalFn: scheduler.clearIntervalFn,
  });

  manager.connect();
  assert.equal(FakeEventSource.instances.length, 1);

  const stream1 = FakeEventSource.instances[0];
  stream1.open();
  assert.equal(statuses.at(-1).state, "connected");

  stream1.emit("state_update", {
    provider_id: "sim0",
    device_id: "dev0",
    signal_id: "temp",
    timestamp_ms: 100,
  });
  assert.equal(events.length, 1);

  nowMs = 1501;
  scheduler.runIntervalPass();
  assert.equal(statuses.at(-1).state, "stale");

  stream1.emit("quality_change", {
    provider_id: "sim0",
    device_id: "dev0",
    signal_id: "temp",
    new_quality: "OK",
    timestamp_ms: 1502,
  });
  assert.equal(statuses.at(-1).state, "connected");

  stream1.fail();
  assert.equal(statuses.at(-1).state, "reconnecting");
  assert.equal(scheduler.timeoutCount(), 1);

  assert.equal(scheduler.runFirstTimeout(), true);
  assert.equal(FakeEventSource.instances.length, 2);

  const stream2 = FakeEventSource.instances[1];
  stream2.open();
  assert.equal(statuses.at(-1).state, "connected");

  manager.disconnect();
  assert.equal(stream2.closed, true);
  assert.equal(statuses.at(-1).state, "disconnected");
});
