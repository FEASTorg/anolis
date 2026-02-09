/**
 * SSE (Server-Sent Events) module - Real-time event stream
 */

import { CONFIG } from './config.js';

let eventSource = null;
let sseConnected = false;
let reconnectTimeout = null;
let eventHandlers = {};

/**
 * Register event handler
 * @param {string} eventType - Event type (e.g., 'mode_change', 'parameter_change')
 * @param {Function} handler - Handler function(data)
 */
export function on(eventType, handler) {
  if (!eventHandlers[eventType]) {
    eventHandlers[eventType] = [];
  }
  eventHandlers[eventType].push(handler);
}

/**
 * Emit event to registered handlers
 */
function emit(eventType, data) {
  const handlers = eventHandlers[eventType] || [];
  handlers.forEach(handler => {
    try {
      handler(data);
    } catch (err) {
      console.error(`Error in ${eventType} handler:`, err);
    }
  });
}

/**
 * Connect to SSE stream
 */
export function connect() {
  if (eventSource && eventSource.readyState === EventSource.OPEN) {
    console.log('[SSE] Already connected');
    return;
  }

  disconnect();

  const url = `${CONFIG.API_BASE}/v0/events`;
  console.log('[SSE] Connecting to', url);

  try {
    eventSource = new EventSource(url);

    eventSource.onopen = () => {
      console.log('[SSE] Connected');
      sseConnected = true;
      emit('connection_status', 'connected');
    };

    // State update events
    eventSource.addEventListener('state_update', (event) => {
      try {
        const data = JSON.parse(event.data);
        emit('state_update', data);
      } catch (err) {
        console.error('[SSE] Failed to parse state_update:', err);
      }
    });

    // Quality change events
    eventSource.addEventListener('quality_change', (event) => {
      try {
        const data = JSON.parse(event.data);
        emit('quality_change', data);
      } catch (err) {
        console.error('[SSE] Failed to parse quality_change:', err);
      }
    });

    // Mode change events
    eventSource.addEventListener('mode_change', (event) => {
      try {
        const data = JSON.parse(event.data);
        emit('mode_change', data);
      } catch (err) {
        console.error('[SSE] Failed to parse mode_change:', err);
      }
    });

    // Parameter change events
    eventSource.addEventListener('parameter_change', (event) => {
      try {
        const data = JSON.parse(event.data);
        emit('parameter_change', data);
      } catch (err) {
        console.error('[SSE] Failed to parse parameter_change:', err);
      }
    });

    // Error handling
    eventSource.onerror = (err) => {
      console.error('[SSE] Error:', err);
      if (eventSource.readyState === EventSource.CLOSED) {
        sseConnected = false;
        emit('connection_status', 'disconnected');
        
        // Attempt reconnect
        if (reconnectTimeout) clearTimeout(reconnectTimeout);
        reconnectTimeout = setTimeout(() => {
          console.log('[SSE] Attempting reconnect...');
          connect();
        }, CONFIG.SSE_RECONNECT_DELAY_MS);
      }
    };
  } catch (err) {
    console.error('[SSE] Failed to create EventSource:', err);
    emit('connection_status', 'error');
  }
}

/**
 * Disconnect from SSE stream
 */
export function disconnect() {
  if (reconnectTimeout) {
    clearTimeout(reconnectTimeout);
    reconnectTimeout = null;
  }

  if (eventSource) {
    eventSource.close();
    eventSource = null;
    sseConnected = false;
    console.log('[SSE] Disconnected');
    emit('connection_status', 'disconnected');
  }
}

/**
 * Get connection status
 */
export function isConnected() {
  return sseConnected;
}
