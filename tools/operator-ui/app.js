/**
 * Anolis Operator UI
 *
 * Minimal dev/operator tool for validating the HTTP API.
 * This is a MIRROR of the API - no new semantics.
 *
 * Capability-driven only - no device-type assumptions.
 */

// Configuration
const API_BASE = "http://localhost:8080";
const POLL_INTERVAL_MS = 500;

// State
let selectedDevice = null;
let pollInterval = null;
let isPaused = false;
let lastCapabilities = null;

// DOM Elements
const elements = {
  runtimeBadge: document.getElementById("runtime-badge"),
  deviceList: document.getElementById("device-list"),
  noSelection: document.getElementById("no-selection"),
  deviceDetail: document.getElementById("device-detail"),
  deviceTitle: document.getElementById("device-title"),
  stateBody: document.getElementById("state-body"),
  functionsContainer: document.getElementById("functions-container"),
  capabilitiesRaw: document.getElementById("capabilities-raw"),
  pollingStatus: document.getElementById("polling-status"),
  togglePolling: document.getElementById("toggle-polling"),
  lastUpdate: document.getElementById("last-update"),
  lastError: document.getElementById("last-error"),
};

// ============================================================================
// API Functions
// ============================================================================

async function fetchApi(endpoint, options = {}) {
  try {
    const response = await fetch(`${API_BASE}${endpoint}`, {
      ...options,
      headers: {
        "Content-Type": "application/json",
        ...options.headers,
      },
    });
    const data = await response.json();

    // Check for HTTP-level errors (non-2xx status)
    if (!response.ok) {
      const errorMsg = data.status?.message || `HTTP ${response.status}`;
      showError(`API Error: ${errorMsg}`);
      // Still return data so caller can inspect error details
      return data;
    }

    clearError();
    return data;
  } catch (err) {
    showError(`API Error: ${err.message}`);
    throw err;
  }
}

async function fetchRuntimeStatus() {
  try {
    const data = await fetchApi("/v0/runtime/status");
    updateRuntimeBadge(data.status?.code === "OK" ? "ok" : "unavailable");
    return data;
  } catch {
    updateRuntimeBadge("unavailable");
    return null;
  }
}

async function fetchDevices() {
  const data = await fetchApi("/v0/devices");
  return data.devices || [];
}

async function fetchCapabilities(providerId, deviceId) {
  const data = await fetchApi(
    `/v0/devices/${providerId}/${deviceId}/capabilities`,
  );
  return data.capabilities || {};
}

async function fetchState(providerId, deviceId) {
  const data = await fetchApi(`/v0/state/${providerId}/${deviceId}`);
  // Single-device endpoint returns device data at top level, not in devices array
  if (data.values) {
    return data;
  }
  return data.devices?.[0] || null;
}

async function executeCall(providerId, deviceId, functionId, args) {
  const data = await fetchApi("/v0/call", {
    method: "POST",
    body: JSON.stringify({
      provider_id: providerId,
      device_id: deviceId,
      function_id: functionId,
      args: args,
    }),
  });
  return data;
}

// ============================================================================
// UI Update Functions
// ============================================================================

function updateRuntimeBadge(status) {
  elements.runtimeBadge.textContent = status.toUpperCase();
  elements.runtimeBadge.className = `badge ${status}`;
}

function renderDeviceList(devices) {
  // Group devices by provider
  const providers = {};
  for (const device of devices) {
    if (!providers[device.provider_id]) {
      providers[device.provider_id] = [];
    }
    providers[device.provider_id].push(device);
  }

  if (Object.keys(providers).length === 0) {
    elements.deviceList.innerHTML =
      '<p class="placeholder">No devices found</p>';
    return;
  }

  let html = "";
  for (const [providerId, providerDevices] of Object.entries(providers)) {
    html += `<div class="provider-group">`;
    html += `<div class="provider-name">&gt; ${escapeHtml(providerId)}</div>`;
    for (const device of providerDevices) {
      const isSelected =
        selectedDevice &&
        selectedDevice.provider_id === device.provider_id &&
        selectedDevice.device_id === device.device_id;
      html += `
                <div class="device-item ${isSelected ? "selected" : ""}" 
                     data-provider="${escapeHtml(device.provider_id)}" 
                     data-device="${escapeHtml(device.device_id)}">
                    - ${escapeHtml(device.device_id)}
                    <span class="device-type">${escapeHtml(device.type || "")}</span>
                </div>
            `;
    }
    html += `</div>`;
  }
  elements.deviceList.innerHTML = html;

  // Add click handlers
  for (const item of elements.deviceList.querySelectorAll(".device-item")) {
    item.addEventListener("click", () => {
      selectDevice(item.dataset.provider, item.dataset.device);
    });
  }
}

function renderState(deviceState) {
  if (!deviceState || !deviceState.values) {
    elements.stateBody.innerHTML =
      '<tr><td colspan="5" class="placeholder">No state available</td></tr>';
    return;
  }

  let html = "";
  for (const signal of deviceState.values) {
    const value = formatValue(signal.value);
    const rawQuality = signal.quality || "UNKNOWN";
    // Validate quality is one of the expected values to prevent class injection
    const validQualities = ["OK", "STALE", "UNAVAILABLE", "FAULT", "UNKNOWN"];
    const quality = validQualities.includes(rawQuality)
      ? rawQuality
      : "UNKNOWN";
    const age = signal.age_ms !== undefined ? `${signal.age_ms}ms` : "--";

    html += `
            <tr>
                <td>${escapeHtml(signal.signal_id)}</td>
                <td class="value-cell">${escapeHtml(value)}</td>
                <td class="type-cell">${escapeHtml(signal.value?.type || "--")}</td>
                <td><span class="badge ${quality.toLowerCase()}">${escapeHtml(quality)}</span></td>
                <td class="age-cell">${age}</td>
            </tr>
        `;
  }
  elements.stateBody.innerHTML = html;
  updateLastUpdateTime();
}

function renderFunctions(capabilities) {
  const functions = capabilities.functions || [];

  if (functions.length === 0) {
    elements.functionsContainer.innerHTML =
      '<p class="placeholder">No functions available</p>';
    return;
  }

  let html = "";
  for (const func of functions) {
    html += `
            <div class="function-card" data-function-id="${func.function_id}">
                <h4>${escapeHtml(func.name)} (id: ${func.function_id})</h4>
                <div class="function-args">
                    ${renderFunctionArgs(func.args || {})}
                </div>
                <button onclick="handleExecute(${func.function_id})">Execute</button>
                <div class="function-result hidden" id="result-${func.function_id}"></div>
            </div>
        `;
  }
  elements.functionsContainer.innerHTML = html;
}

function renderFunctionArgs(args) {
  if (Object.keys(args).length === 0) {
    return '<p class="placeholder">No arguments</p>';
  }

  let html = "";
  for (const [argName, argSpec] of Object.entries(args)) {
    // value_type may not be present - provide a selector if missing
    const valueType = argSpec.value_type || null;
    const defaultValue = valueType ? getDefaultForType(valueType) : "0";

    if (valueType) {
      // Type is known - show fixed type
      html += `
                <div class="arg-row">
                    <label>${escapeHtml(argName)}:</label>
                    <input type="text" 
                           name="${escapeHtml(argName)}" 
                           data-type="${escapeHtml(valueType)}"
                           value="${defaultValue}"
                           placeholder="${escapeHtml(valueType)}">
                    <span class="arg-type">(${escapeHtml(valueType)})</span>
                </div>
            `;
    } else {
      // Type unknown - provide selector
      html += `
                <div class="arg-row">
                    <label>${escapeHtml(argName)}:</label>
                    <select name="${escapeHtml(argName)}_type" class="type-select">
                        <option value="double">double</option>
                        <option value="int64">int64</option>
                        <option value="uint64">uint64</option>
                        <option value="bool">bool</option>
                        <option value="string">string</option>
                    </select>
                    <input type="text" 
                           name="${escapeHtml(argName)}" 
                           data-type-from-select="true"
                           value="${defaultValue}"
                           placeholder="value">
                </div>
            `;
    }
  }
  return html;
}

function renderCapabilities(capabilities) {
  elements.capabilitiesRaw.textContent = JSON.stringify(capabilities, null, 2);
}

// ============================================================================
// Event Handlers
// ============================================================================

async function selectDevice(providerId, deviceId) {
  // Stop any existing polling
  stopPolling();
  isPaused = false;
  elements.togglePolling.textContent = "Pause";

  selectedDevice = { provider_id: providerId, device_id: deviceId };

  // Update UI
  elements.noSelection.classList.add("hidden");
  elements.deviceDetail.classList.remove("hidden");
  elements.deviceTitle.textContent = `${providerId}/${deviceId}`;

  // Update device list selection
  for (const item of elements.deviceList.querySelectorAll(".device-item")) {
    item.classList.remove("selected");
    if (
      item.dataset.provider === providerId &&
      item.dataset.device === deviceId
    ) {
      item.classList.add("selected");
    }
  }

  // Fetch and render capabilities
  try {
    lastCapabilities = await fetchCapabilities(providerId, deviceId);
    renderCapabilities(lastCapabilities);
    renderFunctions(lastCapabilities);
  } catch (err) {
    console.error("Failed to fetch capabilities:", err);
  }

  // Start polling state
  startPolling();
}

// Make handleExecute globally accessible for onclick handlers
window.handleExecute = async function (functionId) {
  if (!selectedDevice) return;

  const card = document.querySelector(
    `.function-card[data-function-id="${functionId}"]`,
  );
  const resultDiv = document.getElementById(`result-${functionId}`);
  const button = card.querySelector("button");

  // Gather arguments
  const args = {};
  for (const input of card.querySelectorAll("input")) {
    const argName = input.name;
    let valueType = input.dataset.type;

    // If type comes from select, get it from the corresponding select element
    if (input.dataset.typeFromSelect === "true") {
      const typeSelect = card.querySelector(`select[name="${argName}_type"]`);
      valueType = typeSelect ? typeSelect.value : "double";
    }

    const rawValue = input.value;
    args[argName] = parseTypedValue(valueType, rawValue);
  }

  // Execute call
  button.disabled = true;
  try {
    const result = await executeCall(
      selectedDevice.provider_id,
      selectedDevice.device_id,
      functionId,
      args,
    );

    // Check for API-level errors
    if (result.status?.code && result.status.code !== "OK") {
      resultDiv.className = "function-result error";
      resultDiv.textContent = `[ERROR] ${result.status.code}: ${result.status.message}`;
      resultDiv.classList.remove("hidden");
      return;
    }

    resultDiv.className = "function-result success";
    resultDiv.textContent = `[OK] ${result.status?.code || "OK"}: ${result.status?.message || "Success"}`;
    resultDiv.classList.remove("hidden");

    // Trigger an immediate state refresh
    await refreshState();
  } catch (err) {
    resultDiv.className = "function-result error";
    resultDiv.textContent = `[ERROR] ${err.message}`;
    resultDiv.classList.remove("hidden");
  } finally {
    button.disabled = false;
  }
};

function togglePolling() {
  if (isPaused) {
    startPolling();
    elements.togglePolling.textContent = "Pause";
    isPaused = false;
  } else {
    stopPolling();
    elements.togglePolling.textContent = "Resume";
    isPaused = true;
  }
}

// ============================================================================
// Polling
// ============================================================================

function startPolling() {
  if (pollInterval) clearInterval(pollInterval);

  // Immediate first fetch
  refreshState();

  // Start polling
  pollInterval = setInterval(refreshState, POLL_INTERVAL_MS);
  elements.pollingStatus.textContent = `Polling: ${POLL_INTERVAL_MS}ms`;
}

function stopPolling() {
  if (pollInterval) {
    clearInterval(pollInterval);
    pollInterval = null;
  }
}

async function refreshState() {
  if (!selectedDevice) return;

  try {
    const state = await fetchState(
      selectedDevice.provider_id,
      selectedDevice.device_id,
    );
    renderState(state);
  } catch (err) {
    console.error("Failed to fetch state:", err);
  }
}

// ============================================================================
// Utility Functions
// ============================================================================

function formatValue(value) {
  if (!value) return "--";

  switch (value.type) {
    case "double":
      return value.double?.toFixed(4) ?? "--";
    case "int64":
      return String(value.int64 ?? "--");
    case "uint64":
      return String(value.uint64 ?? "--");
    case "bool":
      return value.bool ? "true" : "false";
    case "string":
      return value.string ?? "--";
    case "bytes":
      return `[base64: ${value.base64?.substring(0, 20) ?? ""}...]`;
    default:
      return JSON.stringify(value);
  }
}

function parseTypedValue(type, rawValue) {
  switch (type) {
    case "double":
      return { type: "double", double: parseFloat(rawValue) };
    case "int64":
      return { type: "int64", int64: parseInt(rawValue, 10) };
    case "uint64":
      return { type: "uint64", uint64: parseInt(rawValue, 10) };
    case "bool":
      return {
        type: "bool",
        bool: rawValue.toLowerCase() === "true" || rawValue === "1",
      };
    case "string":
      return { type: "string", string: rawValue };
    case "bytes":
      return { type: "bytes", base64: rawValue };
    default:
      return { type: "string", string: rawValue };
  }
}

function getDefaultForType(type) {
  switch (type) {
    case "double":
      return "0.0";
    case "int64":
      return "0";
    case "uint64":
      return "0";
    case "bool":
      return "false";
    case "string":
      return "";
    case "bytes":
      return "";
    default:
      return "";
  }
}

function escapeHtml(text) {
  if (text == null) return "";
  const div = document.createElement("div");
  div.textContent = String(text);
  return div.innerHTML;
}

function updateLastUpdateTime() {
  const now = new Date();
  elements.lastUpdate.textContent = `Last update: ${now.toLocaleTimeString()}`;
}

function showError(message) {
  elements.lastError.textContent = message;
  elements.lastError.classList.remove("hidden");
}

function clearError() {
  elements.lastError.classList.add("hidden");
}

// ============================================================================
// Initialization
// ============================================================================

async function init() {
  console.log("Anolis Operator UI starting...");

  // Set up event listeners
  elements.togglePolling.addEventListener("click", togglePolling);

  // Check runtime status
  await fetchRuntimeStatus();

  // Load devices
  try {
    const devices = await fetchDevices();
    renderDeviceList(devices);
  } catch (err) {
    console.error("Failed to load devices:", err);
    elements.deviceList.innerHTML =
      '<p class="placeholder">Failed to connect to runtime</p>';
  }

  // Periodically check runtime status
  setInterval(fetchRuntimeStatus, 5000);
}

// Start the app
init();
