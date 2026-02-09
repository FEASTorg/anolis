/**
 * API module - HTTP requests to Anolis runtime
 */

import { CONFIG } from './config.js';

/**
 * Generic API fetch wrapper with error handling
 */
async function fetchApi(endpoint, options = {}) {
  try {
    const response = await fetch(`${CONFIG.API_BASE}${endpoint}`, {
      ...options,
      headers: {
        'Content-Type': 'application/json',
        ...options.headers,
      },
    });
    const data = await response.json();

    if (!response.ok) {
      const errorMsg = data.status?.message || `HTTP ${response.status}`;
      throw new Error(errorMsg);
    }

    return data;
  } catch (err) {
    console.error(`API Error (${endpoint}):`, err);
    throw err;
  }
}

// Runtime Status
export async function fetchRuntimeStatus() {
  return fetchApi('/v0/runtime/status');
}

// Devices
export async function fetchDevices() {
  const data = await fetchApi('/v0/devices');
  return data.devices || [];
}

export async function fetchDeviceCapabilities(providerId, deviceId) {
  const data = await fetchApi(`/v0/devices/${providerId}/${deviceId}/capabilities`);
  return data.capabilities || {};
}

export async function fetchDeviceState(providerId, deviceId) {
  const data = await fetchApi(`/v0/state/${providerId}/${deviceId}`);
  return data.values || [];
}

// Function Execution
export async function executeFunction(providerId, deviceId, functionId, args) {
  return fetchApi('/v0/call', {
    method: 'POST',
    body: JSON.stringify({
      provider_id: providerId,
      device_id: deviceId,
      function_id: functionId,
      args: args,
    }),
  });
}

// Automation
export async function fetchMode() {
  return fetchApi('/v0/mode');
}

export async function setMode(mode) {
  return fetchApi('/v0/mode', {
    method: 'POST',
    body: JSON.stringify({ mode }),
  });
}

export async function fetchParameters() {
  const data = await fetchApi('/v0/parameters');
  return data.parameters || [];
}

export async function updateParameter(name, value) {
  return fetchApi('/v0/parameters', {
    method: 'POST',
    body: JSON.stringify({ name, value }),
  });
}

export async function fetchBehaviorTree() {
  const data = await fetchApi('/v0/automation/tree');
  return data.tree || '';
}
