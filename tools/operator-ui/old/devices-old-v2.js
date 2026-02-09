/**
 * Devices Module - Device status overview
 */

import * as API from './api.js';
import * as SSE from './sse.js';
import * as UI from './ui.js';

let elements = {};
let deviceStates = {}; // Map of device_id -> state

/**
 * Initialize devices module
 */
export function init(elementIds) {
  elements = {
    devicesContainer: document.getElementById(elementIds.devicesContainer),
  };

  // SSE event handlers for real-time updates
  SSE.on('state_update', handleStateUpdate);
  SSE.on('quality_change', handleQualityChange);

  // Initial load
  loadDevices();

  // Periodic refresh (every 10 seconds for overview)
  setInterval(loadDevices, 10000);
}

/**
 * Load all devices and their status
 */
async function loadDevices() {
  try {
    const devices = await API.fetchDevices();
    
    // Fetch state for each device
    for (const device of devices) {
      try {
        const state = await API.fetchDeviceState(device.provider_id, device.device_id);
        deviceStates[`${device.provider_id}/${device.device_id}`] = {
          device,
          signals: state,
        };
      } catch (err) {
        console.error(`Failed to fetch state for ${device.provider_id}/${device.device_id}:`, err);
      }
    }

    renderDevices();
  } catch (err) {
    console.error('Failed to load devices:', err);
    elements.devicesContainer.innerHTML = '<p class="placeholder">Failed to load devices</p>';
  }
}

/**
 * Handle state update from SSE
 */
function handleStateUpdate(data) {
  const key = `${data.provider_id}/${data.device_id}`;
  if (!deviceStates[key]) return;

  // Update signal in device state
  const signals = deviceStates[key].signals;
  const signalIndex = signals.findIndex(s => s.signal_id === data.signal_id);
  
  if (signalIndex >= 0) {
    signals[signalIndex] = {
      signal_id: data.signal_id,
      value: data.value,
      quality: data.quality,
      timestamp_ms: data.timestamp_ms,
    };
  } else {
    signals.push({
      signal_id: data.signal_id,
      value: data.value,
      quality: data.quality,
      timestamp_ms: data.timestamp_ms,
    });
  }

  renderDevices();
}

/**
 * Handle quality change from SSE
 */
function handleQualityChange(data) {
  const key = `${data.provider_id}/${data.device_id}`;
  if (!deviceStates[key]) return;

  const signals = deviceStates[key].signals;
  const signal = signals.find(s => s.signal_id === data.signal_id);
  if (signal) {
    signal.quality = data.new_quality;
    renderDevices();
  }
}

/**
 * Render all devices as cards
 */
function renderDevices() {
  const deviceEntries = Object.entries(deviceStates);
  
  if (deviceEntries.length === 0) {
    elements.devicesContainer.innerHTML = '<p class="placeholder">No devices available</p>';
    return;
  }

  let html = '<div class="devices-grid">';
  
  for (const [key, { device, signals }] of deviceEntries) {
    // Get key signals for overview (limit to 4-5 most important)
    const keySignals = signals.slice(0, 5);

    html += `
      <div class="device-card">
        <div class="device-card-header">
          <h3>${UI.escapeHtml(device.device_id)}</h3>
          <span class="device-provider">${UI.escapeHtml(device.provider_id)}</span>
        </div>
        <div class="device-signals">
    `;

    if (keySignals.length === 0) {
      html += '<p class="placeholder">No signals</p>';
    } else {
      for (const signal of keySignals) {
        const quality = UI.normalizeQuality(signal.quality);
        const value = UI.formatValue(signal.value);
        html += `
          <div class="signal-row">
            <span class="signal-name">${UI.escapeHtml(signal.signal_id)}</span>
            <span class="signal-value">${UI.escapeHtml(value)}</span>
            <span class="badge ${quality.toLowerCase()}">${quality}</span>
          </div>
        `;
      }

      if (signals.length > 5) {
        html += `<p class="more-signals">+${signals.length - 5} more signals</p>`;
      }
    }

    html += `
        </div>
      </div>
    `;
  }

  html += '</div>';
  elements.devicesContainer.innerHTML = html;
}
