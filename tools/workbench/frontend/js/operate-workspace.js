import { deriveOperateAvailability, normalizeProviderHealthQuality } from "./operate-state.js";
import {
  extractAutomationStatus,
  extractAutomationTree,
  extractCapabilities,
  extractDeviceStateValues,
  extractDevices,
  extractMode,
  extractParameters,
  extractProvidersHealth,
  extractRuntimeStatus,
  normalizeParameterType,
  parseSafeInt64,
} from "./operate/contracts.js";
import {
  appendEventTrace,
  buildTraceEvent,
  createOperateEventStreamManager,
} from "./operate/events.js";

const INT64_MIN = -9223372036854775808n;
const INT64_MAX = 9223372036854775807n;
const UINT64_MAX = 18446744073709551615n;
const JS_SAFE_MIN = BigInt(Number.MIN_SAFE_INTEGER);
const JS_SAFE_MAX = BigInt(Number.MAX_SAFE_INTEGER);

const EVENT_TRACE_LIMIT = 100;
const TELEMETRY_URL = "http://localhost:3001";

const state = {
  active: false,
  projectName: null,
  system: null,
  runningProject: null,
  pollTimer: null,
  streamManager: null,
  streamStatus: { state: "disconnected", attempts: 0 },
  devices: [],
  selectedKey: "",
  deviceStates: {},
  capabilities: {},
  runtimeStatus: null,
  automationStatus: null,
  behaviorTree: "",
  parameters: [],
  eventTrace: [],
  modeSelectorDirty: false,
  telemetryLoaded: false,
};

let elements = null;
let listenersBound = false;
let parametersGrid = null;
const parameterRows = new Map();

export function setProjectContext(projectName, system) {
  state.projectName = projectName;
  state.system = system;
  state.runningProject = null;
  state.devices = [];
  state.selectedKey = "";
  state.deviceStates = {};
  state.capabilities = {};
  state.runtimeStatus = null;
  state.automationStatus = null;
  state.behaviorTree = "";
  state.parameters = [];
  state.eventTrace = [];
  state.modeSelectorDirty = false;
  parametersGrid = null;
  parameterRows.clear();
}

export function activate() {
  if (state.active) {
    return;
  }

  state.active = true;
  _cacheElements();
  _bindListenersOnce();
  _ensureTelemetryLoaded();
  _renderRuntimeStatusUnavailable();
  _renderStreamStatus({ state: "disconnected", attempts: 0 });
  _renderEventTrace();
  void _refreshOperate();

  state.pollTimer = window.setInterval(() => {
    void _refreshOperate();
  }, 5000);
}

export function deactivate() {
  state.active = false;
  state.modeSelectorDirty = false;

  if (state.pollTimer !== null) {
    window.clearInterval(state.pollTimer);
    state.pollTimer = null;
  }

  _closeEventStream();
}

async function _refreshOperate() {
  if (!state.active || !state.projectName) {
    return;
  }

  let status;
  try {
    status = await _fetchJson("/api/status");
  } catch (err) {
    _showBanner(`Failed to load runtime status: ${_message(err)}`);
    _setModeBadge("--", "unknown");
    _clearOperateData();
    return;
  }

  const availability = deriveOperateAvailability(status, state.projectName);
  state.runningProject = availability.runningProject;

  if (!availability.available) {
    _showBanner(availability.message);
    _setModeBadge("--", "unknown");
    _clearOperateData();
    return;
  }

  _hideBanner();
  _ensureEventStream();
  _ensureTelemetryLoaded();

  const [
    modeResult,
    providerHealthResult,
    devicesResult,
    runtimeStatusResult,
    parametersResult,
    automationStatusResult,
    automationTreeResult,
  ] = await Promise.allSettled([
    _fetchJson("/v0/mode"),
    _fetchJson("/v0/providers/health"),
    _fetchJson("/v0/devices"),
    _fetchJson("/v0/runtime/status"),
    _fetchJson("/v0/parameters"),
    _fetchJson("/v0/automation/status"),
    _fetchJson("/v0/automation/tree"),
  ]);

  if (modeResult.status === "fulfilled") {
    const mode = extractMode(modeResult.value) ?? "UNKNOWN";
    _setModeBadge(mode, "ok");
    if (!state.modeSelectorDirty && elements?.modeSelect) {
      const knownOption = [...elements.modeSelect.options].some((option) => option.value === mode);
      if (knownOption) {
        elements.modeSelect.value = mode;
      }
    }
  } else {
    _setModeBadge("UNKNOWN", "unavailable");
  }

  if (providerHealthResult.status === "fulfilled") {
    _renderProviderHealth(providerHealthResult.value);
  } else {
    _renderProviderHealth({ providers: [] });
  }

  if (devicesResult.status === "fulfilled") {
    state.devices = extractDevices(devicesResult.value);
    _renderDeviceList();
    await _ensureSelectedDeviceLoaded();
  } else {
    state.devices = [];
    _renderDeviceList();
    _renderNoDeviceSelected("Unable to fetch device list.");
  }

  if (runtimeStatusResult.status === "fulfilled") {
    state.runtimeStatus = extractRuntimeStatus(runtimeStatusResult.value);
    _renderRuntimeStatus();
  } else {
    state.runtimeStatus = null;
    _renderRuntimeStatusUnavailable();
  }

  if (parametersResult.status === "fulfilled") {
    state.parameters = extractParameters(parametersResult.value);
    _renderParameters();
  } else {
    state.parameters = [];
    _renderParametersUnavailable("Parameters unavailable.");
  }

  if (automationStatusResult.status === "fulfilled") {
    state.automationStatus = extractAutomationStatus(automationStatusResult.value);
    _renderAutomationStatus();
  } else {
    state.automationStatus = null;
    _renderAutomationStatusUnavailable();
  }

  if (automationTreeResult.status === "fulfilled") {
    state.behaviorTree = extractAutomationTree(automationTreeResult.value);
    _renderBehaviorTree();
  } else {
    state.behaviorTree = "";
    _renderBehaviorTree();
  }
}

function _cacheElements() {
  if (elements) {
    return;
  }

  elements = {
    banner: document.getElementById("operate-banner"),

    runtimeCurrent: document.getElementById("operate-runtime-current"),
    runtimeMeta: document.getElementById("operate-runtime-meta"),
    streamCurrent: document.getElementById("operate-stream-current"),
    streamMeta: document.getElementById("operate-stream-meta"),

    modeCurrent: document.getElementById("operate-mode-current"),
    modeSelect: document.getElementById("operate-mode-select"),
    modeSetButton: document.getElementById("operate-set-mode"),
    modeFeedback: document.getElementById("operate-mode-feedback"),

    automationStatus: document.getElementById("operate-bt-status"),
    automationEnabled: document.getElementById("operate-automation-enabled"),
    automationActive: document.getElementById("operate-automation-active"),
    automationTotalTicks: document.getElementById("operate-bt-total-ticks"),
    automationStalledTicks: document.getElementById("operate-bt-ticks-since-progress"),
    automationErrorCount: document.getElementById("operate-bt-error-count"),
    automationLastError: document.getElementById("operate-bt-last-error"),
    automationCurrentTree: document.getElementById("operate-bt-current-tree"),

    parametersContainer: document.getElementById("operate-parameters"),
    behaviorTreeViewer: document.getElementById("operate-bt-viewer"),
    eventList: document.getElementById("operate-event-list"),

    providerHealthList: document.getElementById("operate-provider-health"),
    deviceList: document.getElementById("operate-device-list"),
    deviceTitle: document.getElementById("operate-device-title"),
    deviceDetail: document.getElementById("operate-device-detail"),

    telemetryFrame: document.getElementById("operate-grafana-iframe"),
    telemetryLink: document.getElementById("operate-grafana-link"),
  };
}

function _bindListenersOnce() {
  if (listenersBound || !elements) {
    return;
  }

  listenersBound = true;

  elements.modeSetButton.addEventListener("click", async () => {
    if (!state.projectName || state.runningProject !== state.projectName) {
      return;
    }

    const mode = elements.modeSelect.value;
    elements.modeSetButton.disabled = true;
    elements.modeFeedback.textContent = "Setting mode...";
    elements.modeFeedback.className = "field-note";

    try {
      await _fetchJson("/v0/mode", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ mode }),
      });
      state.modeSelectorDirty = false;
      elements.modeFeedback.textContent = `Mode set to ${mode}`;
      elements.modeFeedback.className = "field-note";
      void _refreshModeOnly();
    } catch (err) {
      elements.modeFeedback.textContent = `Failed to set mode: ${_message(err)}`;
      elements.modeFeedback.className = "field-error";
    } finally {
      elements.modeSetButton.disabled = false;
    }
  });

  elements.modeSelect.addEventListener("change", () => {
    state.modeSelectorDirty = true;
  });
}

function _showBanner(message) {
  if (!elements) {
    return;
  }
  elements.banner.textContent = message;
  elements.banner.classList.remove("hidden");
}

function _hideBanner() {
  if (!elements) {
    return;
  }
  elements.banner.classList.add("hidden");
  elements.banner.textContent = "";
}

function _setModeBadge(text, className) {
  if (!elements) {
    return;
  }
  elements.modeCurrent.textContent = text;
  elements.modeCurrent.className = `badge ${className}`;
}

function _renderRuntimeStatus() {
  if (!elements) {
    return;
  }

  const runtime = state.runtimeStatus;
  if (!runtime) {
    _renderRuntimeStatusUnavailable();
    return;
  }

  const statusCode = String(runtime.status?.code || "").toUpperCase();
  const statusClass = statusCode === "OK" ? "ok" : "unavailable";
  elements.runtimeCurrent.textContent = statusCode === "OK" ? "OK" : "UNAVAILABLE";
  elements.runtimeCurrent.className = `badge ${statusClass}`;

  const polling = runtime.polling_interval_ms > 0 ? `${runtime.polling_interval_ms}ms` : "--";
  const uptime = runtime.uptime_seconds >= 0 ? `${runtime.uptime_seconds}s` : "--";
  elements.runtimeMeta.textContent = `Mode ${runtime.mode} | Devices ${runtime.device_count} | Poll ${polling} | Uptime ${uptime}`;
}

function _renderRuntimeStatusUnavailable(message = "Runtime status unavailable.") {
  if (!elements) {
    return;
  }

  elements.runtimeCurrent.textContent = "UNAVAILABLE";
  elements.runtimeCurrent.className = "badge unavailable";
  elements.runtimeMeta.textContent = message;
}

function _renderStreamStatus(status) {
  if (!elements) {
    return;
  }

  const stateName = String(status?.state || "disconnected");
  if (stateName === "connected") {
    elements.streamCurrent.textContent = "CONNECTED";
    elements.streamCurrent.className = "badge ok";
    elements.streamMeta.textContent = "SSE stream active.";
    return;
  }

  if (stateName === "reconnecting") {
    elements.streamCurrent.textContent = "RECONNECTING";
    elements.streamCurrent.className = "badge unavailable";
    const delaySec = Math.max(1, Math.ceil(Number(status?.delay_ms || 0) / 1000));
    const attempt = Number(status?.attempts || 0);
    elements.streamMeta.textContent = `Attempt ${attempt}, retry in ${delaySec}s.`;
    return;
  }

  if (stateName === "stale") {
    elements.streamCurrent.textContent = "STALE";
    elements.streamCurrent.className = "badge stale";
    const idleSec = Math.floor(Number(status?.idle_ms || 0) / 1000);
    elements.streamMeta.textContent = `No events for ${idleSec}s.`;
    return;
  }

  elements.streamCurrent.textContent = "DISCONNECTED";
  elements.streamCurrent.className = "badge unavailable";
  elements.streamMeta.textContent = "Stream disconnected.";
}

function _renderProviderHealth(payload) {
  if (!elements) {
    return;
  }

  const list = elements.providerHealthList;
  list.innerHTML = "";

  const providers = extractProvidersHealth(payload);
  if (providers.length === 0) {
    list.innerHTML = '<li class="placeholder">No provider health available.</li>';
    return;
  }

  for (const entry of providers) {
    const providerId = typeof entry?.provider_id === "string" ? entry.provider_id : "unknown";
    const quality = normalizeProviderHealthQuality(entry?.health?.quality || entry?.state || "UNKNOWN");
    const lifecycle = typeof entry?.lifecycle_state === "string" ? entry.lifecycle_state : "--";
    const deviceCount = Number.isFinite(Number(entry?.device_count)) ? Number(entry.device_count) : 0;

    const li = document.createElement("li");
    li.innerHTML =
      `${_esc(providerId)} ` +
      `<span class="badge ${quality.toLowerCase()}">${_esc(quality)}</span>` +
      `<div class="provider-row-meta">${_esc(lifecycle)} | ${deviceCount} device(s)</div>`;
    list.appendChild(li);
  }
}

function _renderDeviceList() {
  if (!elements) {
    return;
  }

  const list = elements.deviceList;
  list.innerHTML = "";

  if (!Array.isArray(state.devices) || state.devices.length === 0) {
    list.innerHTML = '<li class="placeholder">No devices found.</li>';
    return;
  }

  for (const device of state.devices) {
    const key = `${device.provider_id}/${device.device_id}`;
    const li = document.createElement("li");
    const button = document.createElement("button");
    button.type = "button";
    button.className = "operate-device-select";
    const title = device.display_name || device.device_id || key;
    button.textContent = `${title} (${key})`;
    button.addEventListener("click", () => {
      state.selectedKey = key;
      _renderDeviceList();
      void _ensureSelectedDeviceLoaded();
    });
    li.appendChild(button);

    if (key === state.selectedKey) {
      li.classList.add("selected");
    }

    list.appendChild(li);
  }
}

async function _ensureSelectedDeviceLoaded() {
  if (state.devices.length === 0) {
    _renderNoDeviceSelected();
    return;
  }

  if (!state.selectedKey) {
    const first = state.devices[0];
    state.selectedKey = `${first.provider_id}/${first.device_id}`;
    _renderDeviceList();
  }

  const selected = _getSelectedDevice();
  if (!selected) {
    _renderNoDeviceSelected();
    return;
  }

  const key = state.selectedKey;

  try {
    const valuesPayload = await _fetchJson(
      `/v0/state/${encodeURIComponent(selected.provider_id)}/${encodeURIComponent(selected.device_id)}`,
    );
    state.deviceStates[key] = extractDeviceStateValues(valuesPayload);
  } catch {
    state.deviceStates[key] = [];
  }

  if (!state.capabilities[key]) {
    try {
      const capsPayload = await _fetchJson(
        `/v0/devices/${encodeURIComponent(selected.provider_id)}/${encodeURIComponent(selected.device_id)}/capabilities`,
      );
      state.capabilities[key] = extractCapabilities(capsPayload);
    } catch {
      state.capabilities[key] = { functions: [] };
    }
  }

  _renderSelectedDevice();
}

function _renderSelectedDevice() {
  if (!elements) {
    return;
  }

  const selected = _getSelectedDevice();
  if (!selected) {
    _renderNoDeviceSelected();
    return;
  }

  const key = state.selectedKey;
  const values = Array.isArray(state.deviceStates[key]) ? state.deviceStates[key] : [];
  const caps = state.capabilities[key] || { functions: [] };

  elements.deviceTitle.textContent = `${selected.display_name || selected.device_id} (${key})`;

  let html = "";
  if (values.length === 0) {
    html += '<p class="placeholder">No signals available.</p>';
  } else {
    html += '<table class="signal-table"><thead><tr><th>Signal</th><th>Value</th><th>Quality</th><th>Timestamp</th></tr></thead><tbody>';
    for (const signal of values) {
      const quality = _normalizeQuality(signal.quality);
      const timestamp = signal.timestamp_ms > 0 ? new Date(signal.timestamp_ms).toLocaleTimeString() : "--";
      html += `<tr><td>${_esc(signal.signal_id || "-")}</td><td>${_esc(_formatValue(signal.value))}</td><td>${_esc(quality)}</td><td>${_esc(timestamp)}</td></tr>`;
    }
    html += "</tbody></table>";
  }

  const functions = Array.isArray(caps.functions) ? caps.functions : [];
  if (functions.length === 0) {
    html += '<p class="placeholder">No callable functions declared.</p>';
  } else {
    for (const func of functions) {
      html += _renderFunctionForm(key, func);
    }
  }

  elements.deviceDetail.innerHTML = html;
  for (const form of elements.deviceDetail.querySelectorAll(".function-form")) {
    form.addEventListener("submit", _handleFunctionSubmit);
  }
}

function _renderNoDeviceSelected(message = "Select a device to inspect signals and execute functions.") {
  if (!elements) {
    return;
  }

  elements.deviceTitle.textContent = "Device Detail";
  elements.deviceDetail.innerHTML = `<p class="placeholder">${_esc(message)}</p>`;
}

function _renderFunctionForm(deviceKey, func) {
  const formId = `operate-fn-${deviceKey.replaceAll("/", "-")}-${func.function_id}`;
  const functionName = func.display_name || func.name || func.function_name || `Function ${func.function_id}`;
  const description = func.description || func.label || "";

  let html = `<div class="function-card"><form class="function-form" id="${_esc(formId)}" data-device-key="${_esc(deviceKey)}" data-function-id="${func.function_id}"><h4>${_esc(functionName)}</h4>`;
  if (description) {
    html += `<p class="function-description">${_esc(description)}</p>`;
  }

  const args = Array.isArray(func.args) ? func.args : [];
  for (const [argIndex, arg] of args.entries()) {
    html += _renderArgInput(arg, argIndex);
  }

  html += "<div class=\"function-actions\"><button type=\"submit\" class=\"btn-secondary btn-sm\">Execute</button><span class=\"function-feedback\"></span></div></form></div>";
  return html;
}

function _renderArgInput(arg, argIndex) {
  const inputName = `arg_${argIndex}`;
  const argId = `arg-${argIndex}-${Math.random().toString(16).slice(2, 8)}`;
  const required = arg.required !== false;
  const requiredLabel = required ? ' <span class="required">*</span>' : "";

  let input = "";
  const type = arg.type || "string";
  if (type === "double") {
    input = `<input type="number" id="${argId}" name="${inputName}" step="any" ${required ? "required" : ""}>`;
  } else if (type === "int64" || type === "uint64") {
    input = `<input type="text" id="${argId}" name="${inputName}" inputmode="numeric" ${required ? "required" : ""}>`;
  } else if (type === "bool") {
    input = `<input type="checkbox" id="${argId}" name="${inputName}">`;
  } else if (type === "bytes") {
    input = `<input type="text" id="${argId}" name="${inputName}" placeholder="Base64 encoded" ${required ? "required" : ""}>`;
  } else {
    input = `<input type="text" id="${argId}" name="${inputName}" ${required ? "required" : ""}>`;
  }

  let hint = "";
  if (arg.min !== undefined && arg.max !== undefined) {
    hint = ` <span class="constraint-hint">[${_esc(String(arg.min))} - ${_esc(String(arg.max))}]</span>`;
  }

  return `<div class="arg-row"><label for="${argId}">${_esc(arg.name)}${requiredLabel}${hint}</label>${input}</div>`;
}

async function _handleFunctionSubmit(event) {
  event.preventDefault();

  const form = event.target;
  const deviceKey = form.dataset.deviceKey || "";
  const functionId = Number.parseInt(form.dataset.functionId || "", 10);
  const feedback = form.querySelector(".function-feedback");
  const button = form.querySelector('button[type="submit"]');

  if (!deviceKey || Number.isNaN(functionId) || !feedback || !button) {
    return;
  }

  const [providerId, deviceId] = deviceKey.split("/");
  const func = (state.capabilities[deviceKey]?.functions || []).find((entry) => entry.function_id === functionId);
  if (!providerId || !deviceId || !func) {
    feedback.textContent = "Function definition not found.";
    feedback.className = "function-feedback error";
    return;
  }

  button.disabled = true;
  feedback.textContent = "Executing...";
  feedback.className = "function-feedback";

  try {
    const argsPayload = _buildArgsPayload(form, func);
    await _fetchJson("/v0/call", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        provider_id: providerId,
        device_id: deviceId,
        function_id: functionId,
        args: argsPayload,
      }),
    });

    feedback.textContent = "Success";
    feedback.className = "function-feedback success";
    window.setTimeout(() => {
      feedback.textContent = "";
      feedback.className = "function-feedback";
    }, 2500);
  } catch (err) {
    feedback.textContent = `Error: ${_message(err)}`;
    feedback.className = "function-feedback error";
  } finally {
    button.disabled = false;
  }
}

function _buildArgsPayload(form, func) {
  const args = {};
  const formData = new FormData(form);

  for (const [argIndex, argDef] of (func.args || []).entries()) {
    const inputName = `arg_${argIndex}`;
    const inputElement = form.elements.namedItem(inputName);
    const argType = argDef.type || "string";

    if (!inputElement) {
      if (argDef.required !== false) {
        throw new Error(`Missing required argument: ${argDef.name}`);
      }
      continue;
    }

    if (argType === "bool") {
      args[argDef.name] = { type: "bool", bool: Boolean(inputElement.checked) };
      continue;
    }

    const rawValue = formData.get(inputName);
    const value = typeof rawValue === "string" ? rawValue.trim() : "";
    if (value === "") {
      if (argDef.required !== false) {
        throw new Error(`Missing required argument: ${argDef.name}`);
      }
      continue;
    }

    if (argType === "double") {
      const parsed = Number(value);
      if (Number.isNaN(parsed)) {
        throw new Error(`Invalid double argument: ${argDef.name}`);
      }
      args[argDef.name] = { type: "double", double: parsed };
      continue;
    }

    if (argType === "int64") {
      const parsed = _parseIntegerArgument(value, argDef, "int64");
      args[argDef.name] = { type: "int64", int64: parsed };
      continue;
    }

    if (argType === "uint64") {
      const parsed = _parseIntegerArgument(value, argDef, "uint64");
      args[argDef.name] = { type: "uint64", uint64: parsed };
      continue;
    }

    if (argType === "bytes") {
      args[argDef.name] = { type: "bytes", base64: value };
      continue;
    }

    args[argDef.name] = { type: "string", string: value };
  }

  return args;
}

function _parseIntegerArgument(value, argDef, valueType) {
  const argName = argDef.name;
  let parsedBigInt;
  try {
    parsedBigInt = BigInt(value);
  } catch {
    throw new Error(`Invalid ${valueType} argument: ${argName}`);
  }

  if (valueType === "int64") {
    if (parsedBigInt < INT64_MIN || parsedBigInt > INT64_MAX) {
      throw new Error(`Out-of-range int64 argument: ${argName}`);
    }
    if (parsedBigInt < JS_SAFE_MIN || parsedBigInt > JS_SAFE_MAX) {
      throw new Error(`int64 argument out of browser-safe range: ${argName}`);
    }
  } else {
    if (parsedBigInt < 0n || parsedBigInt > UINT64_MAX) {
      throw new Error(`Out-of-range uint64 argument: ${argName}`);
    }
    if (parsedBigInt > JS_SAFE_MAX) {
      throw new Error(`uint64 argument out of browser-safe range: ${argName}`);
    }
  }

  return Number(parsedBigInt);
}

function _renderParameters() {
  if (!elements) {
    return;
  }

  if (!Array.isArray(state.parameters) || state.parameters.length === 0) {
    _renderParametersUnavailable("No parameters available.");
    return;
  }

  const grid = _ensureParametersGrid();
  const names = new Set(state.parameters.map((param) => param.name));

  for (const [name, row] of parameterRows.entries()) {
    if (!names.has(name)) {
      row.card.remove();
      parameterRows.delete(name);
    }
  }

  for (const parameter of state.parameters) {
    let row = parameterRows.get(parameter.name);
    if (!row) {
      row = _createParameterRow(parameter);
      parameterRows.set(parameter.name, row);
    }

    _updateParameterRow(row, parameter);
    grid.appendChild(row.card);
  }
}

function _renderParametersUnavailable(message) {
  if (!elements) {
    return;
  }

  elements.parametersContainer.innerHTML = `<p class="placeholder">${_esc(message)}</p>`;
  parameterRows.clear();
  parametersGrid = null;
}

function _ensureParametersGrid() {
  if (parametersGrid && elements.parametersContainer.contains(parametersGrid)) {
    return parametersGrid;
  }

  parametersGrid = document.createElement("div");
  parametersGrid.className = "parameters-grid";
  elements.parametersContainer.innerHTML = "";
  elements.parametersContainer.appendChild(parametersGrid);
  return parametersGrid;
}

function _createParameterRow(parameter) {
  const card = document.createElement("div");
  card.className = "parameter-card";
  card.dataset.parameterName = parameter.name;

  const header = document.createElement("div");
  header.className = "parameter-header";

  const nameEl = document.createElement("span");
  nameEl.className = "parameter-name";
  nameEl.textContent = parameter.name;

  const typeEl = document.createElement("span");
  typeEl.className = "parameter-type";

  header.appendChild(nameEl);
  header.appendChild(typeEl);

  const valueWrap = document.createElement("div");
  valueWrap.className = "parameter-value";
  const valueStrong = document.createElement("strong");
  valueWrap.appendChild(valueStrong);

  const controls = document.createElement("div");
  controls.className = "parameter-controls";

  const feedbackEl = document.createElement("span");
  feedbackEl.className = "feedback";

  const button = document.createElement("button");
  button.type = "button";
  button.className = "btn-secondary btn-sm";
  button.textContent = "Set";

  const rangeEl = document.createElement("div");
  rangeEl.className = "parameter-range";

  card.appendChild(header);
  card.appendChild(valueWrap);
  card.appendChild(controls);
  card.appendChild(rangeEl);

  return {
    card,
    name: parameter.name,
    type: "",
    typeEl,
    valueStrong,
    controls,
    feedbackEl,
    button,
    rangeEl,
    inputElement: null,
  };
}

function _updateParameterRow(row, parameter) {
  const type = normalizeParameterType(parameter.type);
  row.typeEl.textContent = type ?? String(parameter.type);
  row.valueStrong.textContent = String(parameter.value);

  if (!type) {
    if (row.type !== "invalid") {
      row.controls.innerHTML = "";
      const errorText = document.createElement("span");
      errorText.className = "feedback error";
      errorText.textContent = `Unsupported parameter type: ${String(parameter.type)}`;
      row.controls.appendChild(errorText);
      row.inputElement = null;
      row.type = "invalid";
    }
    row.rangeEl.textContent = "";
    row.rangeEl.style.display = "none";
    return;
  }

  if (row.type !== type || !row.inputElement) {
    row.controls.innerHTML = "";
    row.inputElement = _createParameterInput(parameter, type);
    if (!row.inputElement) {
      return;
    }

    row.button.onclick = () => {
      void _updateParameter(parameter, type, row.inputElement, row.feedbackEl);
    };

    row.controls.appendChild(row.inputElement);
    row.controls.appendChild(row.button);
    row.controls.appendChild(row.feedbackEl);
    row.type = type;
  } else {
    _setParameterConstraints(row.inputElement, parameter.min, parameter.max);
  }

  if (!_isElementEditing(row.inputElement)) {
    if (type === "bool") {
      row.inputElement.value = String(parameter.value).toLowerCase() === "true" ? "true" : "false";
    } else {
      row.inputElement.value = String(parameter.value);
    }
  }

  if (parameter.min !== undefined || parameter.max !== undefined) {
    row.rangeEl.textContent = `Range: [${parameter.min ?? "-inf"}, ${parameter.max ?? "+inf"}]`;
    row.rangeEl.style.display = "";
  } else {
    row.rangeEl.textContent = "";
    row.rangeEl.style.display = "none";
  }
}

function _createParameterInput(parameter, type) {
  let inputElement;

  if (type === "bool") {
    inputElement = document.createElement("select");
    const trueOption = document.createElement("option");
    trueOption.value = "true";
    trueOption.textContent = "true";
    const falseOption = document.createElement("option");
    falseOption.value = "false";
    falseOption.textContent = "false";
    inputElement.appendChild(trueOption);
    inputElement.appendChild(falseOption);
  } else if (type === "string" && Array.isArray(parameter.allowed_values) && parameter.allowed_values.length > 0) {
    inputElement = document.createElement("select");
    for (const item of parameter.allowed_values) {
      const option = document.createElement("option");
      option.value = String(item);
      option.textContent = String(item);
      inputElement.appendChild(option);
    }
  } else {
    inputElement = document.createElement("input");
    if (type === "double") {
      inputElement.type = "number";
      inputElement.step = "any";
      if (parameter.min !== undefined) {
        inputElement.min = String(parameter.min);
      }
      if (parameter.max !== undefined) {
        inputElement.max = String(parameter.max);
      }
    } else if (type === "int64") {
      inputElement.type = "text";
      inputElement.inputMode = "numeric";
    } else {
      inputElement.type = "text";
    }
  }

  inputElement.placeholder = "New value";
  _setParameterConstraints(inputElement, parameter.min, parameter.max);

  return inputElement;
}

function _setParameterConstraints(inputElement, min, max) {
  if (min !== undefined) {
    inputElement.dataset.paramMin = String(min);
  } else {
    delete inputElement.dataset.paramMin;
  }

  if (max !== undefined) {
    inputElement.dataset.paramMax = String(max);
  } else {
    delete inputElement.dataset.paramMax;
  }
}

async function _updateParameter(parameter, type, inputElement, feedbackElement) {
  if (!inputElement || !feedbackElement) {
    return;
  }

  const rawValue = String(inputElement.value).trim();

  try {
    let value;
    if (type === "int64") {
      value = parseSafeInt64(rawValue);
    } else if (type === "double") {
      value = Number(rawValue);
      if (Number.isNaN(value)) {
        throw new Error("Invalid number");
      }
    } else if (type === "bool") {
      value = rawValue.toLowerCase() === "true";
    } else {
      value = rawValue;
    }

    const minAttr = inputElement.dataset.paramMin;
    const maxAttr = inputElement.dataset.paramMax;
    if ((type === "int64" || type === "double") && minAttr !== undefined && minAttr !== "" && value < Number(minAttr)) {
      throw new Error(`Value below minimum (${minAttr})`);
    }
    if ((type === "int64" || type === "double") && maxAttr !== undefined && maxAttr !== "" && value > Number(maxAttr)) {
      throw new Error(`Value above maximum (${maxAttr})`);
    }

    if (type === "string" && Array.isArray(parameter.allowed_values) && parameter.allowed_values.length > 0) {
      const allowed = parameter.allowed_values.map((entry) => String(entry));
      if (!allowed.includes(String(value))) {
        throw new Error(`Value must be one of: ${allowed.join(", ")}`);
      }
    }

    await _fetchJson("/v0/parameters", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name: parameter.name, value }),
    });

    feedbackElement.textContent = "Updated";
    feedbackElement.className = "feedback success";
    window.setTimeout(() => {
      feedbackElement.textContent = "";
      feedbackElement.className = "feedback";
    }, 2000);

    await _refreshParametersOnly();
  } catch (err) {
    feedbackElement.textContent = `Error: ${_message(err)}`;
    feedbackElement.className = "feedback error";
  }
}

function _renderAutomationStatus() {
  if (!elements) {
    return;
  }

  const automation = state.automationStatus;
  if (!automation) {
    _renderAutomationStatusUnavailable();
    return;
  }

  const statusText = String(automation.bt_status || "UNKNOWN").toUpperCase();
  elements.automationStatus.textContent = statusText;
  elements.automationStatus.className = `badge bt-${statusText.toLowerCase()}`;

  elements.automationEnabled.textContent = automation.enabled ? "true" : "false";
  elements.automationActive.textContent = automation.active ? "true" : "false";
  elements.automationTotalTicks.textContent = String(automation.total_ticks);
  elements.automationStalledTicks.textContent = String(automation.ticks_since_progress);
  elements.automationErrorCount.textContent = String(automation.error_count);
  elements.automationLastError.textContent = automation.last_error || "--";
  elements.automationCurrentTree.textContent = automation.current_tree || "--";

  if (statusText === "ERROR") {
    elements.automationLastError.className = "error-text alarm";
  } else if (statusText === "STALLED") {
    elements.automationLastError.className = "error-text warning";
  } else {
    elements.automationLastError.className = "error-text";
  }
}

function _renderAutomationStatusUnavailable() {
  if (!elements) {
    return;
  }

  elements.automationStatus.textContent = "--";
  elements.automationStatus.className = "badge unknown";
  elements.automationEnabled.textContent = "--";
  elements.automationActive.textContent = "--";
  elements.automationTotalTicks.textContent = "--";
  elements.automationStalledTicks.textContent = "--";
  elements.automationErrorCount.textContent = "--";
  elements.automationLastError.textContent = "--";
  elements.automationLastError.className = "error-text";
  elements.automationCurrentTree.textContent = "--";
}

function _renderBehaviorTree() {
  if (!elements) {
    return;
  }

  const tree = state.behaviorTree;
  if (typeof tree !== "string" || tree.trim() === "") {
    elements.behaviorTreeViewer.textContent = "No behavior tree loaded.";
    return;
  }

  try {
    const parser = new DOMParser();
    const xmlDoc = parser.parseFromString(tree, "text/xml");
    const parseError = xmlDoc.querySelector("parsererror");
    if (parseError) {
      elements.behaviorTreeViewer.textContent = tree;
      return;
    }
    elements.behaviorTreeViewer.textContent = _renderBtOutline(xmlDoc);
  } catch {
    elements.behaviorTreeViewer.textContent = tree;
  }
}

function _renderBtOutline(xmlDoc, node = null, indent = 0, isLast = true) {
  if (!node) {
    const root = xmlDoc.querySelector("BehaviorTree");
    if (!root) {
      return "No BehaviorTree found.";
    }
    return _renderBtOutline(xmlDoc, root, 0, true);
  }

  const prefix = indent === 0 ? "" : " ".repeat((indent - 1) * 2) + (isLast ? "\\- " : "|- ");
  const name = node.getAttribute("name") || "";
  let output = `${prefix}${node.tagName}${name ? ` \"${name}\"` : ""}\n`;

  const children = Array.from(node.children);
  for (let index = 0; index < children.length; index += 1) {
    output += _renderBtOutline(xmlDoc, children[index], indent + 1, index === children.length - 1);
  }

  return output;
}

function _renderEventTrace() {
  if (!elements) {
    return;
  }

  if (!Array.isArray(state.eventTrace) || state.eventTrace.length === 0) {
    elements.eventList.innerHTML = '<p class="placeholder">No events yet.</p>';
    return;
  }

  let html = '<div class="event-items">';
  for (let index = state.eventTrace.length - 1; index >= 0; index -= 1) {
    const event = state.eventTrace[index];
    const timestamp = Number.isFinite(Number(event.timestamp_ms))
      ? new Date(Number(event.timestamp_ms)).toLocaleTimeString()
      : "--";
    const typeClass = String(event.type || "event").replaceAll("_", "-");
    html += `<div class="event-item ${_esc(typeClass)}"><span class="event-time">${_esc(timestamp)}</span><span class="event-type">${_esc(event.type)}</span><span class="event-details">${_esc(event.details)}</span></div>`;
  }
  html += "</div>";

  elements.eventList.innerHTML = html;
}

function _appendTraceEvent(eventType, payload, detailsOverride = null) {
  const event = buildTraceEvent(eventType, payload, Date.now());
  if (detailsOverride) {
    event.details = detailsOverride;
  }
  appendEventTrace(state.eventTrace, event, EVENT_TRACE_LIMIT);
  _renderEventTrace();
}

function _ensureEventStream() {
  if (state.streamManager || !state.active) {
    return;
  }

  state.streamManager = createOperateEventStreamManager({
    url: "/v0/events",
    onEvent: (eventType, payload) => {
      _handleSseEvent(eventType, payload);
    },
    onConnectionStatus: (status) => {
      _handleStreamStatus(status);
    },
    onParseError: (err, eventType) => {
      _appendTraceEvent("stream_error", { timestamp_ms: Date.now() }, `${eventType}: ${_message(err)}`);
    },
  });

  state.streamManager.connect();
}

function _closeEventStream() {
  if (!state.streamManager) {
    _renderStreamStatus({ state: "disconnected", attempts: 0 });
    return;
  }

  state.streamManager.disconnect();
  state.streamManager = null;
}

function _handleStreamStatus(status) {
  const previousState = state.streamStatus.state;
  state.streamStatus = status;
  _renderStreamStatus(status);

  if (status.state !== previousState) {
    const details =
      status.state === "reconnecting"
        ? `Reconnecting in ${Math.max(1, Math.ceil(Number(status.delay_ms || 0) / 1000))}s`
        : status.state === "stale"
          ? `Stale for ${Math.floor(Number(status.idle_ms || 0) / 1000)}s`
          : status.state === "connected"
            ? "Connected"
            : "Disconnected";
    _appendTraceEvent("stream_status", { timestamp_ms: Date.now() }, details);
  }
}

function _handleSseEvent(eventType, payload) {
  if (eventType === "state_update") {
    _consumeStateEvent(payload);
  } else if (eventType === "quality_change") {
    _consumeQualityEvent(payload);
  } else if (eventType === "mode_change") {
    _consumeModeChangeEvent(payload);
  } else if (eventType === "parameter_change") {
    void _refreshParametersOnly();
  } else if (eventType === "bt_error") {
    _consumeBtErrorEvent(payload);
  } else if (eventType === "provider_health_change") {
    void _refreshProviderHealthOnly();
  }

  _appendTraceEvent(eventType, payload);
}

function _consumeStateEvent(payload) {
  const key = `${payload.provider_id}/${payload.device_id}`;
  if (!state.deviceStates[key]) {
    state.deviceStates[key] = [];
  }

  const values = state.deviceStates[key];
  const idx = values.findIndex((entry) => entry.signal_id === payload.signal_id);
  const normalized = {
    ...payload,
    timestamp_ms: Number(payload.timestamp_ms) || Number(payload.timestamp_epoch_ms) || 0,
  };

  if (idx >= 0) {
    values[idx] = normalized;
  } else {
    values.push(normalized);
  }

  if (state.selectedKey === key && !_isFunctionInputFocused()) {
    _renderSelectedDevice();
  }
}

function _consumeQualityEvent(payload) {
  const key = `${payload.provider_id}/${payload.device_id}`;
  if (!state.deviceStates[key]) {
    return;
  }

  const signal = state.deviceStates[key].find((entry) => entry.signal_id === payload.signal_id);
  if (signal) {
    signal.quality = payload.new_quality;
  }

  if (state.selectedKey === key && !_isFunctionInputFocused()) {
    _renderSelectedDevice();
  }
}

function _consumeModeChangeEvent(payload) {
  const mode = typeof payload.new_mode === "string" ? payload.new_mode : "UNKNOWN";
  _setModeBadge(mode, "ok");
  if (!state.modeSelectorDirty && elements?.modeSelect) {
    const knownOption = [...elements.modeSelect.options].some((option) => option.value === mode);
    if (knownOption) {
      elements.modeSelect.value = mode;
    }
  }
}

function _consumeBtErrorEvent(payload) {
  if (!state.automationStatus) {
    state.automationStatus = {
      enabled: true,
      active: true,
      bt_status: "ERROR",
      last_tick_ms: Number(payload.timestamp_ms) || 0,
      ticks_since_progress: 0,
      total_ticks: 0,
      last_error: String(payload.error || "Unknown behavior-tree error"),
      error_count: 1,
      current_tree: "",
    };
  } else {
    state.automationStatus.bt_status = "ERROR";
    state.automationStatus.last_error = String(payload.error || "Unknown behavior-tree error");
    state.automationStatus.error_count = Number(state.automationStatus.error_count || 0) + 1;
  }

  _renderAutomationStatus();
}

async function _refreshProviderHealthOnly() {
  if (!state.active) {
    return;
  }

  try {
    const payload = await _fetchJson("/v0/providers/health");
    _renderProviderHealth(payload);
  } catch {
    // Best-effort refresh.
  }
}

async function _refreshParametersOnly() {
  if (!state.active) {
    return;
  }

  try {
    const payload = await _fetchJson("/v0/parameters");
    state.parameters = extractParameters(payload);
    _renderParameters();
  } catch {
    // Best-effort refresh.
  }
}

async function _refreshModeOnly() {
  if (!state.active) {
    return;
  }

  try {
    const payload = await _fetchJson("/v0/mode");
    const mode = extractMode(payload) ?? "UNKNOWN";
    _setModeBadge(mode, "ok");
    if (!state.modeSelectorDirty && elements?.modeSelect) {
      const knownOption = [...elements.modeSelect.options].some((option) => option.value === mode);
      if (knownOption) {
        elements.modeSelect.value = mode;
      }
    }
  } catch {
    _setModeBadge("UNKNOWN", "unavailable");
  }
}

function _clearOperateData() {
  state.devices = [];
  state.selectedKey = "";
  state.deviceStates = {};
  state.capabilities = {};
  state.parameters = [];
  state.runtimeStatus = null;
  state.automationStatus = null;
  state.behaviorTree = "";
  state.modeSelectorDirty = false;
  state.streamStatus = { state: "disconnected", attempts: 0 };

  _renderRuntimeStatusUnavailable();
  _renderStreamStatus(state.streamStatus);
  _renderProviderHealth({ providers: [] });
  _renderDeviceList();
  _renderNoDeviceSelected("Runtime data unavailable.");
  _renderParametersUnavailable("Runtime data unavailable.");
  _renderAutomationStatusUnavailable();
  _renderBehaviorTree();

  _closeEventStream();
}

function _ensureTelemetryLoaded() {
  if (!elements?.telemetryFrame || state.telemetryLoaded) {
    return;
  }

  elements.telemetryFrame.src = TELEMETRY_URL;
  if (elements.telemetryLink) {
    elements.telemetryLink.href = TELEMETRY_URL;
  }
  state.telemetryLoaded = true;
}

function _isElementEditing(element) {
  const active = document.activeElement;
  return active instanceof HTMLElement && active === element;
}

function _isFunctionInputFocused() {
  const active = document.activeElement;
  if (!(active instanceof HTMLElement)) {
    return false;
  }
  return active.closest(".function-form") !== null;
}

function _getSelectedDevice() {
  if (!state.selectedKey) {
    return null;
  }
  return state.devices.find((entry) => `${entry.provider_id}/${entry.device_id}` === state.selectedKey) || null;
}

async function _fetchJson(path, options = {}) {
  const response = await fetch(path, options);
  const text = await response.text();
  let data = {};

  if (text) {
    try {
      data = JSON.parse(text);
    } catch {
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${text}`);
      }
      return {};
    }
  }

  if (!response.ok) {
    const message = data?.status?.message || data?.error || `HTTP ${response.status}`;
    throw new Error(message);
  }

  return data;
}

function _normalizeQuality(quality) {
  return normalizeProviderHealthQuality(quality);
}

function _formatValue(value) {
  if (value === null || value === undefined) {
    return "--";
  }

  if (typeof value === "object") {
    if (typeof value.type === "string") {
      if (value.type === "double" && value.double !== undefined) {
        return String(value.double);
      }
      if (value.type === "int64" && value.int64 !== undefined) {
        return String(value.int64);
      }
      if (value.type === "uint64" && value.uint64 !== undefined) {
        return String(value.uint64);
      }
      if (value.type === "bool" && value.bool !== undefined) {
        return value.bool ? "true" : "false";
      }
      if (value.type === "string" && value.string !== undefined) {
        return String(value.string);
      }
      if (value.type === "bytes" && value.base64 !== undefined) {
        return String(value.base64);
      }
    }
    return JSON.stringify(value);
  }

  return String(value);
}

function _message(err) {
  if (err instanceof Error) {
    return err.message;
  }
  return String(err);
}

function _esc(str) {
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;")
    .replace(/'/g, "&#39;");
}
