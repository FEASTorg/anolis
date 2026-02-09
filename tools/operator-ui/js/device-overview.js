/**
 * Device Overview Module - Compact device cards for Dashboard
 * Shows first 5 signals per device with click-to-navigate
 */

import * as UI from './ui.js';

let elements = {};
let deviceStates = {}; // Map of device_id -> state

/**
 * Initialize device overview module
 */
export function init(elementIds) {
  elements = {
    container: document.getElementById(elementIds.container),
  };
  
  console.log('[DeviceOverview] Module initialized');
}

/**
 * Update device states and render
 */
export function render(states) {
  deviceStates = states;
  renderGrid();
}

/**
 * Update a single device from SSE event
 */
export function updateDevice(providerDevice, signals) {
  deviceStates[providerDevice] = signals;
  renderGrid();
}

/**
 * Render compact device cards grid
 */
function renderGrid() {
  const deviceEntries = Object.entries(deviceStates);
  
  if (deviceEntries.length === 0) {
    elements.container.innerHTML = '<p class="placeholder">No devices available</p>';
    return;
  }

  let html = '<div class="devices-grid">';
  
  for (const [key, { device, signals }] of deviceEntries) {
    // Get first 5 signals for overview
    const keySignals = signals.slice(0, 5);
    const [provider, deviceId] = key.split('/');
    
    html += `
      <div class="device-card clickable" 
           data-provider="${UI.escapeHtml(provider)}" 
           data-device="${UI.escapeHtml(deviceId)}"
           title="Click to view full details">
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
  elements.container.innerHTML = html;
  
  // Attach click handlers for navigation
  attachCardHandlers();
}

/**
 * Attach click handlers to device cards
 */
function attachCardHandlers() {
  const cards = elements.container.querySelectorAll('.device-card.clickable');
  cards.forEach(card => {
    card.addEventListener('click', () => {
      const provider = card.dataset.provider;
      const device = card.dataset.device;
      window.location.hash = `#devices/${provider}/${device}`;
    });
  });
}
