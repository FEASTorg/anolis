/**
 * Anolis Operator UI
 *
 * Minimal dev/operator tool for validating the HTTP API.
 * This is a MIRROR of the API - no new semantics.
 *
 * Capability-driven only - no device-type assumptions.
 *
 * Currently uses SSE for real-time updates with polling fallback.
 */

// Configuration
const API_BASE = "http://localhost:8080";
const POLL_INTERVAL_MS = 500;
const SSE_RECONNECT_DELAY_MS = 3000;

// State
let selectedDevice = null;
let pollInterval = null;
let isPaused = false;
let lastCapabilities = null;

// SSE State
let eventSource = null;
let sseConnected = false;
let lastEventId = null;
let cachedState = {}; // signal_id -> signal data (for delta updates)

// Automation State
let automationEnabled = false;
let currentMode = null;
let eventBuffer = []; // Ring buffer for event trace (max 100)
const MAX_EVENTS = 100;

// DOM Elements
const elements = {
  runtimeBadge: document.getElementById("runtime-badge"),
  connectionBadge: document.getElementById("connection-badge"),
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
  // Automation elements
  automationStatus: document.getElementById("automation-status"),
  automationMode: document.getElementById("automation-mode"),
  automationPolicy: document.getElementById("automation-policy"),
  automationBtFile: document.getElementById("automation-bt-file"),
  automationLastTick: document.getElementById("automation-last-tick"),
  modeSelector: document.getElementById("mode-selector"),
  setModeButton: document.getElementById("set-mode-button"),
  modeFeedback: document.getElementById("mode-feedback"),
  // Parameters elements
  parametersSection: document.getElementById("parameters-section"),
  parametersContainer: document.getElementById("parameters-container"),
  // BT visualization elements
  btVisualization: document.getElementById("bt-visualization"),
  btTreeView: document.getElementById("bt-tree-view"),
  // Event trace elements
  eventTrace: document.getElementById("event-trace"),
  eventList: document.getElementById("event-list"),
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

async function fetchMode() {
  try {
    const data = await fetchApi("/v0/mode");
    if (data.status?.code === "OK") {
      return data;
    }
  } catch (err) {
    // Automation not enabled
  }
  return null;
}

async function setMode(mode) {
  const data = await fetchApi("/v0/mode", {
    method: "POST",
    body: JSON.stringify({ mode: mode }),
  });
  return data;
}

async function fetchParameters() {
  try {
    const data = await fetchApi("/v0/parameters");
    if (data.status?.code === "OK") {
      return data.parameters || [];
    }
  } catch (err) {
    // Automation not enabled
  }
  return [];
}

async function updateParameter(name, value) {
  const data = await fetchApi("/v0/parameters", {
    method: "POST",
    body: JSON.stringify({ name: name, value: value }),
  });
  return data;
}

async function fetchBehaviorTree() {
  try {
    const data = await fetchApi("/v0/automation/tree");
    if (data.status?.code === "OK") {
      return data.tree || "";
    }
  } catch (err) {
    // Endpoint not available
  }
  return "";
}

// ============================================================================
// SSE (Server-Sent Events) Functions
// ============================================================================

function updateConnectionBadge(status) {
  elements.connectionBadge.textContent = status.toUpperCase();
  elements.connectionBadge.className = `badge ${status.toLowerCase()}`;
}

/**
 * Connect to SSE stream for real-time updates
 * Now connects globally to receive all events (device state + automation)
 * Device-specific events are filtered client-side
 */
async function connectSSE() {
  if (isPaused) return; // Don't connect if paused

  // Guard: Reuse existing healthy connection
  if (eventSource && eventSource.readyState === EventSource.OPEN) {
    console.log("[SSE] Already connected, reusing existing connection");
    return;
  }

  // Close any existing connection
  disconnectSSE();

  // If we have a selected device, seed its state first
  if (selectedDevice) {
    const { provider_id, device_id } = selectedDevice;
    
    // Seed initial state via REST (race-proof pattern)
    updateConnectionBadge("connecting");
    try {
      const state = await fetchState(provider_id, device_id);
      if (state && state.values) {
        // Cache the seeded state
        cachedState = {};
        for (const signal of state.values) {
          cachedState[signal.signal_id] = signal;
        }
        renderStateFromCache();
      }
    } catch (err) {
      console.error("Failed to seed state:", err);
      // Continue anyway to get automation events
    }
  } else {
    updateConnectionBadge("connecting");
  }

  // Open SSE connection for all events (device state + automation)
  const sseUrl = `${API_BASE}/v0/events`;

  try {
    eventSource = new EventSource(sseUrl);

    eventSource.onopen = () => {
      console.log("[SSE] Connected");
      sseConnected = true;
      updateConnectionBadge("connected");
      elements.pollingStatus.textContent = "SSE: Real-time";
      clearError();
    };

    eventSource.addEventListener("state_update", (event) => {
      handleStateUpdateEvent(event);
    });

    eventSource.addEventListener("quality_change", (event) => {
      handleQualityChangeEvent(event);
    });

    eventSource.addEventListener("mode_change", (event) => {
      handleModeChangeEvent(event);
    });

    eventSource.addEventListener("parameter_change", (event) => {
      handleParameterChangeEvent(event);
    });

    eventSource.onerror = (err) => {
      console.error("[SSE] Error:", err);
      if (eventSource.readyState === EventSource.CLOSED) {
        sseConnected = false;
        updateConnectionBadge("disconnected");
        // Attempt reconnect after delay
        setTimeout(() => {
          if (selectedDevice && !isPaused) {
            console.log("[SSE] Attempting reconnect...");
            connectSSE();
          }
        }, SSE_RECONNECT_DELAY_MS);
      }
    };
  } catch (err) {
    console.error("[SSE] Failed to create EventSource:", err);
    fallbackToPolling("SSE unavailable");
  }
}

function disconnectSSE() {
  if (eventSource) {
    eventSource.close();
    eventSource = null;
  }
  sseConnected = false;
  lastEventId = null;
}

/**
 * Handle state_update event from SSE
 */
function handleStateUpdateEvent(event) {
  try {
    const data = JSON.parse(event.data);
    lastEventId = event.lastEventId;

    // Only update if this is for our selected device
    if (!selectedDevice) return;
    if (data.provider_id !== selectedDevice.provider_id) return;
    if (data.device_id !== selectedDevice.device_id) return;

    // Update cached state
    const signalId = data.signal_id;
    cachedState[signalId] = {
      signal_id: signalId,
      value: data.value,
      quality: data.quality,
      timestamp_ms: data.timestamp_ms,
    };

    // Re-render with change highlight
    renderStateFromCache(signalId);
    updateLastUpdateTime();
  } catch (err) {
    console.error("[SSE] Failed to parse state_update:", err);
  }
}

/**
 * Handle quality_change event from SSE
 */
function handleQualityChangeEvent(event) {
  try {
    const data = JSON.parse(event.data);
    lastEventId = event.lastEventId;

    // Only update if this is for our selected device
    if (!selectedDevice) return;
    if (data.provider_id !== selectedDevice.provider_id) return;
    if (data.device_id !== selectedDevice.device_id) return;

    // Update quality in cached state
    const signalId = data.signal_id;
    if (cachedState[signalId]) {
      cachedState[signalId].quality = data.new_quality;
      renderStateFromCache(signalId);
      updateLastUpdateTime();
    }
  } catch (err) {
    console.error("[SSE] Failed to parse quality_change:", err);
  }
}

/**
 * Handle mode_change event from SSE
 */
function handleModeChangeEvent(event) {
  try {
    const data = JSON.parse(event.data);
    lastEventId = event.lastEventId;

    // Update automation status display
    currentMode = data.new_mode;
    updateAutomationDisplay();

    // Add to event trace
    addEventToTrace(
      "mode_change",
      `${data.previous_mode} → ${data.new_mode}`,
      data.timestamp_ms,
    );
  } catch (err) {
    console.error("[SSE] Failed to parse mode_change:", err);
  }
}

/**
 * Handle parameter_change event from SSE
 */
function handleParameterChangeEvent(event) {
  try {
    const data = JSON.parse(event.data);
    lastEventId = event.lastEventId;

    // Refresh parameters display
    refreshParameters();

    // Add to event trace
    addEventToTrace(
      "parameter_change",
      `${data.parameter_name}: ${data.old_value} → ${data.new_value}`,
      data.timestamp_ms,
    );
  } catch (err) {
    console.error("[SSE] Failed to parse parameter_change:", err);
  }
}

/**
 * Fall back to polling when SSE is unavailable
 */
function fallbackToPolling(reason) {
  console.log(`[SSE] Falling back to polling: ${reason}`);
  disconnectSSE();
  updateConnectionBadge("fallback");
  elements.pollingStatus.textContent = `Polling: ${POLL_INTERVAL_MS}ms (SSE: ${reason})`;
  startPolling();
}

// ============================================================================
// UI Update Functions
// ============================================================================

function updateRuntimeBadge(status) {
  elements.runtimeBadge.textContent = status.toUpperCase();
  elements.runtimeBadge.className = `badge ${status}`;
}

function renderDeviceList(devices) {
  // Separate sim_control from other devices
  const simControl = devices.find(d => d.device_id === 'sim_control');
  const otherDevices = devices.filter(d => d.device_id !== 'sim_control');
  
  // Group other devices by provider
  const providers = {};
  for (const device of otherDevices) {
    if (!providers[device.provider_id]) {
      providers[device.provider_id] = [];
    }
    providers[device.provider_id].push(device);
  }

  if (devices.length === 0) {
    elements.deviceList.innerHTML =
      '<p class="placeholder">No devices found</p>';
    return;
  }

  let html = "";
  
  // Render sim_control at root level if it exists
  if (simControl) {
    const isSelected =
      selectedDevice &&
      selectedDevice.provider_id === simControl.provider_id &&
      selectedDevice.device_id === simControl.device_id;
    html += `
      <div class="device-item root-device ${isSelected ? "selected" : ""}" 
           data-provider="${escapeHtml(simControl.provider_id)}" 
           data-device="${escapeHtml(simControl.device_id)}">
        ${escapeHtml(simControl.device_id)}
        <span class="device-type">${escapeHtml(simControl.type || "")}</span>
      </div>
    `;
  }
  
  // Render other devices grouped by provider
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

  // Also update cache when rendering from REST
  for (const signal of deviceState.values) {
    cachedState[signal.signal_id] = signal;
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
            <tr data-signal-id="${escapeHtml(signal.signal_id)}">
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

/**
 * Render state from cache, optionally highlighting a changed signal
 * @param {string} changedSignalId - Signal ID that changed (for pulse animation)
 */
function renderStateFromCache(changedSignalId = null) {
  const signals = Object.values(cachedState);

  if (signals.length === 0) {
    elements.stateBody.innerHTML =
      '<tr><td colspan="5" class="placeholder">No state available</td></tr>';
    return;
  }

  // If we have an existing table and a specific signal changed, update just that row
  if (changedSignalId && elements.stateBody.children.length > 0) {
    const existingRow = elements.stateBody.querySelector(
      `tr[data-signal-id="${changedSignalId}"]`,
    );
    if (existingRow) {
      updateSignalRow(existingRow, cachedState[changedSignalId]);
      return;
    }
  }

  // Full render (initial load or structure changed)
  signals.sort((a, b) => a.signal_id.localeCompare(b.signal_id));

  let html = "";
  for (const signal of signals) {
    html += createSignalRowHtml(signal, signal.signal_id === changedSignalId);
  }
  elements.stateBody.innerHTML = html;
}

/**
 * Create HTML for a single signal row
 */
function createSignalRowHtml(signal, isChanged = false) {
  const value = formatValue(signal.value);
  const rawQuality = signal.quality || "UNKNOWN";
  const validQualities = ["OK", "STALE", "UNAVAILABLE", "FAULT", "UNKNOWN"];
  const quality = validQualities.includes(rawQuality) ? rawQuality : "UNKNOWN";

  let age = "--";
  if (signal.timestamp_ms) {
    const ageMs = Date.now() - signal.timestamp_ms;
    age = `${Math.max(0, ageMs)}ms`;
  } else if (signal.age_ms !== undefined) {
    age = `${signal.age_ms}ms`;
  }

  const changeClass = isChanged ? "value-changed" : "";

  return `
      <tr data-signal-id="${escapeHtml(signal.signal_id)}" class="${changeClass}">
          <td>${escapeHtml(signal.signal_id)}</td>
          <td class="value-cell">${escapeHtml(value)}</td>
          <td class="type-cell">${escapeHtml(signal.value?.type || "--")}</td>
          <td><span class="badge ${quality.toLowerCase()}">${escapeHtml(quality)}</span></td>
          <td class="age-cell">${age}</td>
      </tr>
  `;
}

/**
 * Update a single row in-place with new signal data
 * This preserves other rows' animations
 */
function updateSignalRow(row, signal) {
  const value = formatValue(signal.value);
  const rawQuality = signal.quality || "UNKNOWN";
  const validQualities = ["OK", "STALE", "UNAVAILABLE", "FAULT", "UNKNOWN"];
  const quality = validQualities.includes(rawQuality) ? rawQuality : "UNKNOWN";

  let age = "--";
  if (signal.timestamp_ms) {
    const ageMs = Date.now() - signal.timestamp_ms;
    age = `${Math.max(0, ageMs)}ms`;
  } else if (signal.age_ms !== undefined) {
    age = `${signal.age_ms}ms`;
  }

  // Update cell contents
  const cells = row.cells;
  cells[1].textContent = value; // value-cell
  cells[2].textContent = signal.value?.type || "--"; // type-cell
  cells[3].innerHTML = `<span class="badge ${quality.toLowerCase()}">${escapeHtml(quality)}</span>`;
  cells[4].textContent = age; // age-cell

  // Trigger animation by removing and re-adding class
  row.classList.remove("value-changed");
  // Force reflow to restart animation
  void row.offsetWidth;
  row.classList.add("value-changed");
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
// Automation UI Functions
// ============================================================================

async function updateAutomationDisplay() {
  try {
    const statusData = await fetchRuntimeStatus();
    const modeData = await fetchMode();

    if (!modeData) {
      // Automation not enabled
      automationEnabled = false;
      elements.automationStatus.classList.add("hidden");
      elements.parametersSection.classList.add("hidden");
      elements.btVisualization.classList.add("hidden");
      elements.eventTrace.classList.add("hidden");
      return;
    }

    // Show automation sections
    automationEnabled = true;
    elements.automationStatus.classList.remove("hidden");
    elements.parametersSection.classList.remove("hidden");
    elements.btVisualization.classList.remove("hidden");
    elements.eventTrace.classList.remove("hidden");

    // Update mode display
    currentMode = modeData.mode;
    elements.automationMode.textContent = currentMode;
    elements.automationMode.className = `badge ${currentMode.toLowerCase()}`;

    // Update policy display (if available)
    if (modeData.policy) {
      elements.automationPolicy.textContent = modeData.policy;
    }

    // Update BT file (if available in status)
    if (statusData.automation && statusData.automation.bt_file) {
      elements.automationBtFile.textContent = statusData.automation.bt_file;
    }

    // Update last tick (if available)
    if (statusData.automation && statusData.automation.last_tick_ms) {
      const ageMs = Date.now() - statusData.automation.last_tick_ms;
      elements.automationLastTick.textContent = `${Math.round(ageMs)}ms ago`;
    }

    // Set mode selector to current mode
    elements.modeSelector.value = currentMode;
  } catch (err) {
    console.error("Failed to update automation display:", err);
  }
}

async function refreshParameters() {
  try {
    const parameters = await fetchParameters();

    if (parameters.length === 0) {
      elements.parametersContainer.innerHTML =
        '<p class="placeholder">No parameters available</p>';
      return;
    }

    let html = '<div class="parameters-list">';
    for (const param of parameters) {
      html += `
        <div class="parameter-item">
          <div class="parameter-header">
            <span class="parameter-name">${escapeHtml(param.name)}</span>
            <span class="parameter-type">${escapeHtml(param.type)}</span>
          </div>
          <div class="parameter-value">
            <span>Current: <strong>${escapeHtml(String(param.value))}</strong></span>
          </div>
          <div class="parameter-controls">
            <input type="text" 
                   id="param-${escapeHtml(param.name)}" 
                   value="${escapeHtml(String(param.value))}"
                   placeholder="New value">
            <button onclick="updateParameterValue('${escapeHtml(param.name)}', '${escapeHtml(param.type)}')">Update</button>
            <span id="param-feedback-${escapeHtml(param.name)}" class="param-feedback"></span>
          </div>
          ${param.min !== undefined || param.max !== undefined ? `<div class="parameter-constraints">Range: [${param.min}, ${param.max}]</div>` : ""}
        </div>
      `;
    }
    html += "</div>";
    elements.parametersContainer.innerHTML = html;
  } catch (err) {
    console.error("Failed to refresh parameters:", err);
  }
}

async function updateParameterValue(name, type) {
  const inputElement = document.getElementById(`param-${name}`);
  const feedbackElement = document.getElementById(`param-feedback-${name}`);
  const rawValue = inputElement.value;

  try {
    // Parse value based on type
    let value;
    if (type === "int64" || type === "uint64") {
      value = parseInt(rawValue, 10);
      if (isNaN(value)) {
        throw new Error("Invalid integer");
      }
    } else if (type === "double") {
      value = parseFloat(rawValue);
      if (isNaN(value)) {
        throw new Error("Invalid number");
      }
    } else if (type === "bool") {
      value = rawValue.toLowerCase() === "true";
    } else {
      value = rawValue;
    }

    // Send update request
    const result = await updateParameter(name, value);

    if (result.status?.code === "OK") {
      feedbackElement.textContent = "✓ Updated";
      feedbackElement.className = "param-feedback success";
      setTimeout(() => {
        feedbackElement.textContent = "";
      }, 2000);
      // Refresh parameters to show new value
      await refreshParameters();
    } else {
      throw new Error(result.status?.message || "Update failed");
    }
  } catch (err) {
    feedbackElement.textContent = `✗ ${err.message}`;
    feedbackElement.className = "param-feedback error";
  }
}

async function loadBehaviorTree() {
  try {
    const treeXml = await fetchBehaviorTree();
    if (!treeXml) {
      elements.btTreeView.textContent = "No behavior tree loaded";
      return;
    }

    // Parse XML and render as text outline
    const parser = new DOMParser();
    const xmlDoc = parser.parseFromString(treeXml, "text/xml");
    const outline = renderBTTextOutline(xmlDoc);
    elements.btTreeView.textContent = outline;
  } catch (err) {
    elements.btTreeView.textContent = `Error loading BT: ${err.message}`;
    console.error("Failed to load behavior tree:", err);
  }
}

function renderBTTextOutline(xmlDoc, node = null, indent = 0, isLast = true) {
  if (!node) {
    const root = xmlDoc.querySelector("BehaviorTree");
    if (!root) return "No BehaviorTree found in XML";
    return renderBTTextOutline(xmlDoc, root, 0, true);
  }

  let output = "";
  const prefix =
    indent === 0 ? "" : " ".repeat((indent - 1) * 2) + (isLast ? "└─ " : "├─ ");

  // Node type and name
  const nodeName = node.getAttribute("name") || "";
  output += `${prefix}${node.tagName}${nodeName ? ` "${nodeName}"` : ""}\n`;

  // Render children
  const children = Array.from(node.children);
  for (let i = 0; i < children.length; i++) {
    const child = children[i];
    const childIsLast = i === children.length - 1;
    output += renderBTTextOutline(xmlDoc, child, indent + 1, childIsLast);
  }

  return output;
}

function addEventToTrace(eventType, details, timestampMs) {
  const timestamp = new Date(timestampMs).toLocaleTimeString();
  const event = { type: eventType, details, timestamp };

  // Add to ring buffer
  eventBuffer.push(event);
  if (eventBuffer.length > MAX_EVENTS) {
    eventBuffer.shift();
  }

  // Render event trace
  renderEventTrace();
}

function renderEventTrace() {
  if (eventBuffer.length === 0) {
    elements.eventList.innerHTML = '<p class="placeholder">No events yet</p>';
    return;
  }

  let html = '<div class="event-items">';
  // Show most recent first
  for (let i = eventBuffer.length - 1; i >= 0; i--) {
    const event = eventBuffer[i];
    const typeClass = event.type.replace("_", "-");
    html += `
      <div class="event-item ${typeClass}">
        <span class="event-timestamp">${event.timestamp}</span>
        <span class="event-type">${event.type.toUpperCase()}</span>
        <span class="event-details">${escapeHtml(event.details)}</span>
      </div>
    `;
  }
  html += "</div>";
  elements.eventList.innerHTML = html;

  // Auto-scroll to top (since most recent is first)
  elements.eventList.scrollTop = 0;
}

async function handleSetMode() {
  const selectedMode = elements.modeSelector.value;
  if (!selectedMode) {
    elements.modeFeedback.textContent = "Please select a mode";
    elements.modeFeedback.className = "error";
    return;
  }

  try {
    const result = await setMode(selectedMode);
    if (result.status?.code === "OK") {
      elements.modeFeedback.textContent = `✓ Mode set to ${selectedMode}`;
      elements.modeFeedback.className = "success";
      setTimeout(() => {
        elements.modeFeedback.textContent = "";
      }, 2000);
      // Update display to reflect new mode
      await updateAutomationDisplay();
    } else {
      throw new Error(result.status?.message || "Failed to set mode");
    }
  } catch (err) {
    elements.modeFeedback.textContent = `✗ ${err.message}`;
    elements.modeFeedback.className = "error";
    console.error("Failed to set mode:", err);
  }
}

// ============================================================================
// Event Handlers
// ============================================================================

async function selectDevice(providerId, deviceId) {
  // Stop polling (SSE stays connected globally)
  stopPolling();
  // Note: Don't disconnect SSE - keep one connection for all devices
  
  selectedDevice = { provider_id: providerId, device_id: deviceId };
  
  // Clear state for new device
  cachedState = {};
  renderStateFromCache(); // Immediately clear state table UI
  
  isPaused = false;
  elements.togglePolling.textContent = "Pause";

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

  // Fetch initial state for new device
  try {
    const state = await fetchState(providerId, deviceId);
    if (state && state.values) {
      for (const signal of state.values) {
        cachedState[signal.signal_id] = signal;
      }
      renderStateFromCache();
      updateLastUpdateTime();
    }
  } catch (err) {
    console.error("Failed to fetch initial state:", err);
  }

  // Kick off polling briefly to ensure fresh data after device switch
  // (SSE only sends events when state changes, so static devices would show stale ages)
  // After a few cycles, we rely on SSE for real-time updates
  startPolling();
  setTimeout(() => {
    if (selectedDevice && 
        selectedDevice.provider_id === providerId && 
        selectedDevice.device_id === deviceId) {
      stopPolling();
    }
  }, 3000); // Poll for 3 seconds, then stop

  // Note: SSE stays connected globally - no need to reconnect on device switch
  // Device-specific events are filtered client-side based on selectedDevice
  // Note: Automation panels (mode, parameters, BT, events) are RUNTIME-GLOBAL,
  // not device-specific. They control/display the entire automation system.
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

// Make updateParameterValue globally accessible for onclick handlers
window.updateParameterValue = updateParameterValue;

function togglePolling() {
  if (isPaused) {
    // Resume: try SSE first, fall back to polling
    isPaused = false;
    elements.togglePolling.textContent = "Pause";
    connectSSE();
  } else {
    // Pause: stop both SSE and polling
    isPaused = true;
    elements.togglePolling.textContent = "Resume";
    stopPolling();
    disconnectSSE();
    updateConnectionBadge("disconnected");
    elements.pollingStatus.textContent = "Paused";
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
  elements.setModeButton.addEventListener("click", handleSetMode);

  // Check runtime status
  await fetchRuntimeStatus();

  // Load devices
  let devices = [];
  try {
    devices = await fetchDevices();
    renderDeviceList(devices);
    
    // Auto-select sim_control if available
    const simControl = devices.find(d => d.device_id === 'sim_control');
    if (simControl) {
      await selectDevice(simControl.provider_id, simControl.device_id);
    }
  } catch (err) {
    console.error("Failed to load devices:", err);
    elements.deviceList.innerHTML =
      '<p class="placeholder">Failed to connect to runtime</p>';
  }

  // Periodically check runtime status and automation state
  setInterval(fetchRuntimeStatus, 5000);
  setInterval(updateAutomationDisplay, 5000);

  // Initial automation UI update
  await updateAutomationDisplay();
  if (automationEnabled) {
    await refreshParameters();
    await loadBehaviorTree();
  }

  // Start global SSE connection for automation events
  // (will also handle device events once a device is selected)
  connectSSE();
}

// Start the app
init();
