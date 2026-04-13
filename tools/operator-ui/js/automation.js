/**
 * Automation Module - Mode, parameters, BT, events
 */

import * as API from "./api.js";
import * as SSE from "./sse.js";
import * as UI from "./ui.js";
import { CONFIG } from "./config.js";

let elements = {};
let currentMode = null;
let eventBuffer = [];
let modeSelectorDirty = false; // Track if user has manually changed dropdown
let lastParametersSnapshot = "";
let parametersGrid = null;
const parameterRows = new Map();
const PARAMETER_TYPES = new Set(["double", "int64", "bool", "string"]);
const INT64_MIN = -9223372036854775808n;
const INT64_MAX = 9223372036854775807n;
const JS_SAFE_MIN = BigInt(Number.MIN_SAFE_INTEGER);
const JS_SAFE_MAX = BigInt(Number.MAX_SAFE_INTEGER);

/**
 * Initialize automation module
 */
export function init(elementIds) {
  elements = {
    automationSection: document.getElementById(elementIds.automationSection),
    modeDisplay: document.getElementById(elementIds.modeDisplay),
    modeSelector: document.getElementById(elementIds.modeSelector),
    setModeButton: document.getElementById(elementIds.setModeButton),
    modeFeedback: document.getElementById(elementIds.modeFeedback),
    parametersContainer: document.getElementById(
      elementIds.parametersContainer,
    ),
    btViewer: document.getElementById(elementIds.btViewer),
    eventList: document.getElementById(elementIds.eventList),
    // Health display elements
    btStatus: document.getElementById("bt-status"),
    btTotalTicks: document.getElementById("bt-total-ticks"),
    btTicksSinceProgress: document.getElementById("bt-ticks-since-progress"),
    btErrorCount: document.getElementById("bt-error-count"),
    btLastError: document.getElementById("bt-last-error"),
  };

  // Event listeners
  elements.setModeButton.addEventListener("click", handleSetMode);

  // Track when user manually changes dropdown (dirty state)
  elements.modeSelector.addEventListener("change", () => {
    modeSelectorDirty = true;
  });

  // SSE event handlers
  SSE.on("mode_change", handleModeChange);
  SSE.on("parameter_change", handleParameterChange);
  SSE.on("bt_error", handleBTError);

  // Initial load
  refreshAll();

  // Periodic refresh
  setInterval(refreshAll, 5000);
}

/**
 * Refresh all automation UI
 */
async function refreshAll() {
  await Promise.all([
    refreshMode(),
    refreshParameters(),
    refreshAutomationHealth(),
    loadBehaviorTree(),
  ]);
}

/**
 * Refresh mode display
 */
async function refreshMode() {
  try {
    const data = await API.fetchMode();
    if (data.status?.code === "OK") {
      currentMode = data.mode;
      elements.modeDisplay.textContent = currentMode;
      elements.modeDisplay.className = `badge ${currentMode.toLowerCase()}`;

      // Only update dropdown if user hasn't manually changed it
      if (!modeSelectorDirty) {
        elements.modeSelector.value = currentMode;
      }

      UI.show(elements.automationSection);
    }
  } catch (err) {
    console.error("Failed to fetch mode:", err);
  }
}

/**
 * Handle mode change from SSE
 */
function handleModeChange(data) {
  currentMode = data.new_mode;
  elements.modeDisplay.textContent = currentMode;
  elements.modeDisplay.className = `badge ${currentMode.toLowerCase()}`;

  // Only update dropdown if user hasn't manually changed it
  if (!modeSelectorDirty) {
    elements.modeSelector.value = currentMode;
  }

  addEvent(
    "mode_change",
    `${data.previous_mode} -> ${data.new_mode}`,
    data.timestamp_ms,
  );
}

/**
 * Refresh automation health display
 */
async function refreshAutomationHealth() {
  try {
    const data = await API.fetchAutomationStatus();

    if (data.status?.code === "OK") {
      // Update BT status badge
      const btStatus = data.bt_status || "UNKNOWN";
      elements.btStatus.textContent = btStatus;
      elements.btStatus.className = `badge bt-${btStatus.toLowerCase()}`;

      // Update metrics
      elements.btTotalTicks.textContent = data.total_ticks || 0;
      elements.btTicksSinceProgress.textContent =
        data.ticks_since_progress || 0;
      elements.btErrorCount.textContent = data.error_count || 0;
      elements.btLastError.textContent = data.last_error || "--";

      // Apply semantic style to last error based on BT health
      if (btStatus === "STALLED") {
        elements.btLastError.className = "error-text warning";
      } else if (btStatus === "ERROR") {
        elements.btLastError.className = "error-text alarm";
      } else {
        elements.btLastError.className = "error-text";
      }
    }
  } catch (err) {
    // Automation might not be enabled, fail silently
    console.debug("Automation health not available:", err);
  }
}

/**
 * Handle BT error event from SSE
 */
function handleBTError(data) {
  // Update error display
  elements.btLastError.textContent = data.error;
  elements.btLastError.className = "error-text alarm";

  // Increment error count
  const currentCount = parseInt(elements.btErrorCount.textContent) || 0;
  elements.btErrorCount.textContent = currentCount + 1;

  // Update status badge to ERROR
  elements.btStatus.textContent = "ERROR";
  elements.btStatus.className = "badge bt-error";

  // Add to event trace
  const errorMsg = data.node ? `${data.node}: ${data.error}` : data.error;
  addEvent("bt_error", errorMsg, data.timestamp_ms);
}

/**
 * Handle set mode button click
 */
async function handleSetMode() {
  const newMode = elements.modeSelector.value;

  elements.modeFeedback.className = "feedback pending";
  elements.modeFeedback.textContent = "Setting...";

  try {
    const result = await API.setMode(newMode);

    if (result.status?.code === "OK") {
      elements.modeFeedback.textContent = "Mode set";
      elements.modeFeedback.className = "feedback success";

      // Reset dirty flag - mode change successful
      modeSelectorDirty = false;

      setTimeout(() => {
        elements.modeFeedback.textContent = "";
        elements.modeFeedback.className = "feedback";
      }, 2000);
    } else {
      throw new Error(result.status?.message || "Failed to set mode");
    }
  } catch (err) {
    elements.modeFeedback.textContent = `Error: ${err.message}`;
    elements.modeFeedback.className = "feedback error";
    console.error("Failed to set mode:", err);
  }
}

/**
 * Refresh parameters display
 */
async function refreshParameters() {
  try {
    const parameters = await API.fetchParameters();
    const normalized = normalizeParameterList(parameters);
    const snapshot = JSON.stringify(
      normalized.map((param) => ({
        name: param.name,
        type: param.type,
        value: param.value,
        min: param.min ?? null,
        max: param.max ?? null,
        allowed_values: param.allowed_values ?? null,
      })),
    );
    if (snapshot === lastParametersSnapshot) {
      return;
    }
    lastParametersSnapshot = snapshot;

    if (normalized.length === 0) {
      elements.parametersContainer.innerHTML =
        '<p class="placeholder">No parameters available</p>';
      parametersGrid = null;
      parameterRows.clear();
      return;
    }
    reconcileParameterRows(normalized);
  } catch (err) {
    console.error("Failed to refresh parameters:", err);
  }
}

/**
 * Update parameter value
 */
export async function updateParameter(name, type, inputElement, feedbackElement) {
  if (!inputElement || !feedbackElement) {
    return;
  }
  const rawValue = String(inputElement.value).trim();
  const normalizedType = normalizeParameterType(type);

  try {
    if (!normalizedType) {
      throw new Error("Unsupported parameter type");
    }

    // Parse value based on type
    let value;
    if (normalizedType === "int64") {
      value = parseInt64(rawValue);
    } else if (normalizedType === "double") {
      value = parseFloat(rawValue);
      if (isNaN(value)) throw new Error("Invalid number");
    } else if (normalizedType === "bool") {
      value = rawValue.toLowerCase() === "true";
    } else {
      value = rawValue;
    }

    const minAttr = inputElement.dataset.paramMin;
    const maxAttr = inputElement.dataset.paramMax;
    if (
      (normalizedType === "int64" || normalizedType === "double") &&
      minAttr !== undefined &&
      minAttr !== "" &&
      value < Number(minAttr)
    ) {
      throw new Error(`Value below minimum (${minAttr})`);
    }
    if (
      (normalizedType === "int64" || normalizedType === "double") &&
      maxAttr !== undefined &&
      maxAttr !== "" &&
      value > Number(maxAttr)
    ) {
      throw new Error(`Value above maximum (${maxAttr})`);
    }

    const result = await API.updateParameter(name, value);

    if (result.status?.code === "OK") {
      feedbackElement.textContent = "Updated";
      feedbackElement.className = "feedback success";
      lastParametersSnapshot = "";
      setTimeout(() => {
        feedbackElement.textContent = "";
        feedbackElement.className = "feedback";
      }, 2000);
      await refreshParameters();
    } else {
      throw new Error(result.status?.message || "Update failed");
    }
  } catch (err) {
    feedbackElement.textContent = `Error: ${err.message}`;
    feedbackElement.className = "feedback error";
  }
}

/**
 * Handle parameter change from SSE
 */
function handleParameterChange(data) {
  addEvent(
    "parameter_change",
    `${data.parameter_name}: ${data.old_value} -> ${data.new_value}`,
    data.timestamp_ms,
  );
  void refreshParameters();
}

function normalizeParameterType(typeToken) {
  const type = String(typeToken ?? "").trim();
  return PARAMETER_TYPES.has(type) ? type : null;
}

function ensureParametersGrid() {
  if (parametersGrid && elements.parametersContainer.contains(parametersGrid)) {
    return parametersGrid;
  }
  parametersGrid = document.createElement("div");
  parametersGrid.className = "parameters-grid";
  elements.parametersContainer.innerHTML = "";
  elements.parametersContainer.appendChild(parametersGrid);
  return parametersGrid;
}

function isElementEditing(element) {
  const active = document.activeElement;
  return active instanceof HTMLElement && active === element;
}

function setConstraints(inputElement, min, max) {
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

function createParameterInput(param) {
  const type = normalizeParameterType(param.type);
  if (!type) {
    return null;
  }

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
  } else {
    inputElement = document.createElement("input");
    if (type === "double") {
      inputElement.type = "number";
      inputElement.step = "any";
      if (param.min !== undefined) inputElement.min = String(param.min);
      if (param.max !== undefined) inputElement.max = String(param.max);
    } else if (type === "int64") {
      inputElement.type = "text";
      inputElement.inputMode = "numeric";
    } else {
      inputElement.type = "text";
    }
  }

  inputElement.placeholder = "New value";
  setConstraints(inputElement, param.min, param.max);
  return inputElement;
}

function createParameterRow(param) {
  const card = document.createElement("div");
  card.className = "parameter-card";
  card.dataset.parameterName = param.name;

  const header = document.createElement("div");
  header.className = "parameter-header";

  const nameEl = document.createElement("span");
  nameEl.className = "parameter-name";
  nameEl.textContent = param.name;
  header.appendChild(nameEl);

  const typeEl = document.createElement("span");
  typeEl.className = "parameter-type";
  typeEl.textContent = param.type;
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
  button.textContent = "Set";

  const rangeEl = document.createElement("div");
  rangeEl.className = "parameter-range";

  card.appendChild(header);
  card.appendChild(valueWrap);
  card.appendChild(controls);
  card.appendChild(rangeEl);

  return {
    card,
    name: param.name,
    type: param.type,
    typeEl,
    valueStrong,
    controls,
    feedbackEl,
    button,
    rangeEl,
    inputElement: null,
  };
}

function updateParameterRow(row, param) {
  const type = normalizeParameterType(param.type);
  row.typeEl.textContent = type ?? String(param.type);
  row.valueStrong.textContent = String(param.value);

  if (!type) {
    if (row.type !== "invalid") {
      row.controls.innerHTML = "";
      const errorText = document.createElement("span");
      errorText.className = "feedback error";
      errorText.textContent = `Unsupported parameter type: ${String(param.type)}`;
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
    row.inputElement = createParameterInput(param);
    if (!row.inputElement) {
      return;
    }
    row.button.onclick = () => {
      void updateParameter(param.name, type, row.inputElement, row.feedbackEl);
    };
    row.controls.appendChild(row.inputElement);
    row.controls.appendChild(row.button);
    row.controls.appendChild(row.feedbackEl);
    row.type = type;
  } else {
    setConstraints(row.inputElement, param.min, param.max);
    if (type === "double") {
      if (param.min !== undefined) {
        row.inputElement.min = String(param.min);
      } else {
        row.inputElement.removeAttribute("min");
      }
      if (param.max !== undefined) {
        row.inputElement.max = String(param.max);
      } else {
        row.inputElement.removeAttribute("max");
      }
    }
  }

  if (!isElementEditing(row.inputElement)) {
    if (type === "bool") {
      row.inputElement.value =
        String(param.value).toLowerCase() === "true" ? "true" : "false";
    } else {
      row.inputElement.value = String(param.value);
    }
  }

  if (param.min !== undefined || param.max !== undefined) {
    row.rangeEl.textContent = `Range: [${param.min}, ${param.max}]`;
    row.rangeEl.style.display = "";
  } else {
    row.rangeEl.textContent = "";
    row.rangeEl.style.display = "none";
  }
}

function reconcileParameterRows(parameters) {
  const grid = ensureParametersGrid();
  const names = new Set(parameters.map((param) => param.name));

  for (const [name, row] of parameterRows.entries()) {
    if (!names.has(name)) {
      row.card.remove();
      parameterRows.delete(name);
    }
  }

  for (const param of parameters) {
    let row = parameterRows.get(param.name);
    if (!row) {
      row = createParameterRow(param);
      parameterRows.set(param.name, row);
    }
    updateParameterRow(row, param);
    grid.appendChild(row.card);
  }
}

function parseInt64(rawValue) {
  if (!/^-?\d+$/.test(rawValue)) {
    throw new Error("Invalid integer");
  }
  let parsed;
  try {
    parsed = BigInt(rawValue);
  } catch (_err) {
    throw new Error("Invalid integer");
  }
  if (parsed < INT64_MIN || parsed > INT64_MAX) {
    throw new Error("Out-of-range int64 value");
  }
  if (parsed < JS_SAFE_MIN || parsed > JS_SAFE_MAX) {
    throw new Error("int64 value exceeds browser-safe range");
  }
  return Number(parsed);
}

function normalizeParameterList(parameters) {
  if (!Array.isArray(parameters)) {
    return [];
  }

  return parameters
    .filter((param) => param && typeof param.name === "string")
    .map((param) => ({
      ...param,
      name: param.name.trim(),
    }))
    .filter((param) => param.name !== "")
    .sort((a, b) => String(a.name).localeCompare(String(b.name)));
}

/**
 * Load and display behavior tree
 */
async function loadBehaviorTree() {
  try {
    const treeXml = await API.fetchBehaviorTree();
    if (!treeXml) {
      elements.btViewer.textContent = "No behavior tree loaded";
      return;
    }

    // Parse and render as text outline
    const parser = new DOMParser();
    const xmlDoc = parser.parseFromString(treeXml, "text/xml");
    const outline = renderBTOutline(xmlDoc);
    elements.btViewer.textContent = outline;
  } catch (err) {
    elements.btViewer.textContent = `Error: ${err.message}`;
    console.error("Failed to load BT:", err);
  }
}

/**
 * Render BT as text outline
 */
function renderBTOutline(xmlDoc, node = null, indent = 0, isLast = true) {
  if (!node) {
    const root = xmlDoc.querySelector("BehaviorTree");
    if (!root) return "No BehaviorTree found";
    return renderBTOutline(xmlDoc, root, 0, true);
  }

  let output = "";
  const prefix =
    indent === 0
      ? ""
      : " ".repeat((indent - 1) * 2) + (isLast ? "\\- " : "|- ");
  const nodeName = node.getAttribute("name") || "";
  output += `${prefix}${node.tagName}${nodeName ? ` "${nodeName}"` : ""}\n`;

  const children = Array.from(node.children);
  for (let i = 0; i < children.length; i++) {
    output += renderBTOutline(
      xmlDoc,
      children[i],
      indent + 1,
      i === children.length - 1,
    );
  }

  return output;
}

/**
 * Add event to trace
 */
function addEvent(eventType, details, timestampMs) {
  const timestampValue = Number(timestampMs);
  const timestamp =
    Number.isFinite(timestampValue) && timestampValue > 0
      ? new Date(timestampValue).toLocaleTimeString()
      : new Date().toLocaleTimeString();
  eventBuffer.push({ type: eventType, details, timestamp });

  if (eventBuffer.length > CONFIG.MAX_EVENTS) {
    eventBuffer.shift();
  }

  renderEvents();
}

/**
 * Render event trace
 */
function renderEvents() {
  if (eventBuffer.length === 0) {
    elements.eventList.innerHTML = '<p class="placeholder">No events yet</p>';
    return;
  }

  let html = '<div class="event-items">';
  // Show most recent first
  for (let i = eventBuffer.length - 1; i >= 0; i--) {
    const event = eventBuffer[i];
    html += `
      <div class="event-item ${event.type.replace("_", "-")}">
        <span class="event-time">${event.timestamp}</span>
        <span class="event-type">${event.type}</span>
        <span class="event-details">${UI.escapeHtml(event.details)}</span>
      </div>
    `;
  }
  html += "</div>";
  elements.eventList.innerHTML = html;
}
