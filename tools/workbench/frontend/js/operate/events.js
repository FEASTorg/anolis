import { parseSseJsonData } from "./contracts.js";

export const DEFAULT_SSE_EVENT_TYPES = [
  "state_update",
  "quality_change",
  "mode_change",
  "parameter_change",
  "bt_error",
  "provider_health_change",
];

export function appendEventTrace(buffer, event, maxEntries = 100) {
  const items = Array.isArray(buffer) ? buffer : [];
  items.push(event);
  if (items.length > maxEntries) {
    items.splice(0, items.length - maxEntries);
  }
  return items;
}

export function describeEvent(eventType, payload) {
  if (!payload || typeof payload !== "object") {
    return eventType;
  }

  if (eventType === "mode_change") {
    const previous = payload.previous_mode ?? "?";
    const next = payload.new_mode ?? "?";
    return `${previous} -> ${next}`;
  }

  if (eventType === "parameter_change") {
    const name = payload.parameter_name ?? payload.name ?? "parameter";
    const oldValue = payload.old_value ?? "?";
    const newValue = payload.new_value ?? "?";
    return `${name}: ${oldValue} -> ${newValue}`;
  }

  if (eventType === "bt_error") {
    const node = typeof payload.node === "string" && payload.node !== "" ? `${payload.node}: ` : "";
    return `${node}${payload.error ?? "Unknown behavior-tree error"}`;
  }

  if (eventType === "provider_health_change") {
    const provider = payload.provider_id ?? "provider";
    const state = payload.state ?? payload.new_state ?? "UNKNOWN";
    return `${provider}: ${state}`;
  }

  if (eventType === "state_update") {
    const provider = payload.provider_id ?? "provider";
    const device = payload.device_id ?? "device";
    const signal = payload.signal_id ?? "signal";
    return `${provider}/${device} ${signal}`;
  }

  if (eventType === "quality_change") {
    const provider = payload.provider_id ?? "provider";
    const device = payload.device_id ?? "device";
    const signal = payload.signal_id ?? "signal";
    const quality = payload.new_quality ?? payload.quality ?? "UNKNOWN";
    return `${provider}/${device} ${signal} -> ${quality}`;
  }

  return JSON.stringify(payload);
}

export function buildTraceEvent(eventType, payload, nowMs = Date.now()) {
  const timestampRaw = Number(payload?.timestamp_ms);
  const timestampMs = Number.isFinite(timestampRaw) && timestampRaw > 0 ? timestampRaw : nowMs;
  return {
    type: eventType,
    timestamp_ms: timestampMs,
    details: describeEvent(eventType, payload),
    payload,
  };
}

export function createOperateEventStreamManager({
  url = "/v0/events",
  onEvent = () => {},
  onConnectionStatus = () => {},
  onParseError = () => {},
  eventTypes = DEFAULT_SSE_EVENT_TYPES,
  reconnectDelayMs = 3000,
  maxReconnectDelayMs = 10000,
  staleAfterMs = 30000,
  staleCheckIntervalMs = 5000,
  EventSourceImpl = EventSource,
  nowFn = () => Date.now(),
  setTimeoutFn = (callback, delayMs) => window.setTimeout(callback, delayMs),
  clearTimeoutFn = (timerId) => window.clearTimeout(timerId),
  setIntervalFn = (callback, delayMs) => window.setInterval(callback, delayMs),
  clearIntervalFn = (timerId) => window.clearInterval(timerId),
} = {}) {
  let active = false;
  let source = null;
  let reconnectTimer = null;
  let staleTimer = null;
  let reconnectAttempts = 0;
  let lastEventAt = 0;
  let currentState = "disconnected";

  function emitConnectionStatus(status) {
    currentState = status.state;
    onConnectionStatus(status);
  }

  function clearReconnectTimer() {
    if (reconnectTimer !== null) {
      clearTimeoutFn(reconnectTimer);
      reconnectTimer = null;
    }
  }

  function clearStaleTimer() {
    if (staleTimer !== null) {
      clearIntervalFn(staleTimer);
      staleTimer = null;
    }
  }

  function closeSource() {
    if (!source) {
      return;
    }

    source.onopen = null;
    source.onerror = null;
    try {
      source.close();
    } catch {
      // Best-effort cleanup.
    }
    source = null;
  }

  function updateEventTimestamp() {
    lastEventAt = nowFn();
    if (currentState === "stale") {
      emitConnectionStatus({ state: "connected", attempts: reconnectAttempts });
    }
  }

  function startStaleTimer() {
    clearStaleTimer();
    staleTimer = setIntervalFn(() => {
      if (!active || !source || currentState === "disconnected" || currentState === "reconnecting") {
        return;
      }

      const idleMs = nowFn() - lastEventAt;
      if (idleMs > staleAfterMs) {
        emitConnectionStatus({ state: "stale", attempts: reconnectAttempts, idle_ms: idleMs });
      }
    }, staleCheckIntervalMs);
  }

  function scheduleReconnect() {
    if (!active) {
      return;
    }

    reconnectAttempts += 1;
    const delayMs = Math.min(reconnectDelayMs * reconnectAttempts, maxReconnectDelayMs);
    emitConnectionStatus({ state: "disconnected", attempts: reconnectAttempts });
    emitConnectionStatus({ state: "reconnecting", attempts: reconnectAttempts, delay_ms: delayMs });

    clearReconnectTimer();
    reconnectTimer = setTimeoutFn(() => {
      reconnectTimer = null;
      openSource();
    }, delayMs);
  }

  function openSource() {
    if (!active) {
      return;
    }

    clearReconnectTimer();
    clearStaleTimer();
    closeSource();

    try {
      source = new EventSourceImpl(url);
    } catch (err) {
      onParseError(err, "event_source_init");
      scheduleReconnect();
      return;
    }

    source.onopen = () => {
      reconnectAttempts = 0;
      updateEventTimestamp();
      emitConnectionStatus({ state: "connected", attempts: reconnectAttempts });
      startStaleTimer();
    };

    source.onerror = () => {
      clearStaleTimer();
      closeSource();
      scheduleReconnect();
    };

    for (const eventType of eventTypes) {
      source.addEventListener(eventType, (event) => {
        try {
          updateEventTimestamp();
          const payload = parseSseJsonData(event.data);
          onEvent(eventType, payload);
        } catch (err) {
          onParseError(err, eventType);
        }
      });
    }
  }

  return {
    connect() {
      if (active) {
        return;
      }
      active = true;
      reconnectAttempts = 0;
      openSource();
    },
    disconnect() {
      active = false;
      reconnectAttempts = 0;
      clearReconnectTimer();
      clearStaleTimer();
      closeSource();
      emitConnectionStatus({ state: "disconnected", attempts: 0 });
    },
    getState() {
      return currentState;
    },
    getAttempts() {
      return reconnectAttempts;
    },
  };
}
