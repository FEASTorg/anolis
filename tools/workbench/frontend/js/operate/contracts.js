function asObject(value) {
  return value && typeof value === "object" && !Array.isArray(value) ? value : {};
}

function asArray(value) {
  return Array.isArray(value) ? value : [];
}

function toFiniteNumber(value) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

const PARAMETER_TYPES = new Set(["double", "int64", "bool", "string"]);
const INT64_MIN = -9223372036854775808n;
const INT64_MAX = 9223372036854775807n;
const JS_SAFE_MIN = BigInt(Number.MIN_SAFE_INTEGER);
const JS_SAFE_MAX = BigInt(Number.MAX_SAFE_INTEGER);

export function extractDevices(payload) {
  const root = asObject(payload);
  return asArray(root.devices);
}

export function extractProvidersHealth(payload) {
  const root = asObject(payload);
  return asArray(root.providers);
}

export function extractCapabilities(payload) {
  const root = asObject(payload);
  const capabilities = asObject(root.capabilities);
  return {
    ...capabilities,
    signals: asArray(capabilities.signals),
    functions: normalizeFunctionSpecs(capabilities.functions),
  };
}

export function normalizeFunctionSpecs(functions) {
  if (!Array.isArray(functions)) {
    return [];
  }

  const normalized = functions.map((func, index) => {
    const functionId = Number(func?.function_id);
    const fallbackId = Number.isFinite(functionId) ? functionId : index + 1;

    const name =
      typeof func?.name === "string" && func.name.trim() !== ""
        ? func.name.trim()
        : typeof func?.function_name === "string" && func.function_name.trim() !== ""
          ? func.function_name.trim()
          : `Function ${fallbackId}`;

    const description =
      typeof func?.label === "string" && func.label.trim() !== ""
        ? func.label.trim()
        : typeof func?.description === "string" && func.description.trim() !== ""
          ? func.description.trim()
          : "";

    return {
      ...func,
      function_id: fallbackId,
      name,
      function_name: name,
      label: description,
      description,
      display_name: name,
      args: normalizeFunctionArgs(func?.args),
    };
  });

  normalized.sort((a, b) => {
    if (a.function_id !== b.function_id) {
      return a.function_id - b.function_id;
    }
    return String(a.display_name).localeCompare(String(b.display_name));
  });

  return normalized;
}

export function normalizeFunctionArgs(args) {
  if (Array.isArray(args)) {
    return args
      .map((arg, index) => normalizeFunctionArgSpec(arg, `arg_${index + 1}`))
      .filter((arg) => arg.name !== "");
  }

  if (args && typeof args === "object") {
    return Object.entries(args)
      .map(([name, spec]) => normalizeFunctionArgSpec(spec, name))
      .filter((arg) => arg.name !== "")
      .sort((a, b) => a.name.localeCompare(b.name));
  }

  return [];
}

export function normalizeFunctionArgSpec(arg, fallbackName = "") {
  const resolvedName =
    typeof arg?.name === "string" && arg.name.trim() !== "" ? arg.name.trim() : fallbackName;

  if (typeof resolvedName !== "string" || resolvedName.trim() === "") {
    return { name: "", type: "string", required: true };
  }

  return {
    name: resolvedName.trim(),
    type: typeof arg?.type === "string" && arg.type.trim() !== "" ? arg.type.trim() : "string",
    required: arg?.required !== false,
    min: arg?.min,
    max: arg?.max,
  };
}

export function normalizeStateSignal(signal) {
  const source = asObject(signal);
  const tsMs =
    toFiniteNumber(source.timestamp_ms) ?? toFiniteNumber(source.timestamp_epoch_ms) ?? 0;
  return {
    ...source,
    timestamp_ms: tsMs,
  };
}

export function extractDeviceStateValues(payload) {
  const root = asObject(payload);
  return asArray(root.values).map((entry) => normalizeStateSignal(entry));
}

export function extractMode(payload) {
  const root = asObject(payload);
  return typeof root.mode === "string" ? root.mode : null;
}

export function extractRuntimeStatus(payload) {
  const root = asObject(payload);
  return {
    status: asObject(root.status),
    mode: typeof root.mode === "string" ? root.mode : "UNKNOWN",
    uptime_seconds: toFiniteNumber(root.uptime_seconds) ?? 0,
    polling_interval_ms: toFiniteNumber(root.polling_interval_ms) ?? 0,
    device_count: toFiniteNumber(root.device_count) ?? 0,
    providers: asArray(root.providers),
  };
}

export function extractAutomationStatus(payload) {
  const root = asObject(payload);
  return {
    status: asObject(root.status),
    enabled: Boolean(root.enabled),
    active: Boolean(root.active),
    bt_status: typeof root.bt_status === "string" ? root.bt_status : "UNKNOWN",
    last_tick_ms: toFiniteNumber(root.last_tick_ms) ?? 0,
    ticks_since_progress: toFiniteNumber(root.ticks_since_progress) ?? 0,
    total_ticks: toFiniteNumber(root.total_ticks) ?? 0,
    last_error: typeof root.last_error === "string" && root.last_error.trim() !== "" ? root.last_error : null,
    error_count: toFiniteNumber(root.error_count) ?? 0,
    current_tree: typeof root.current_tree === "string" ? root.current_tree : "",
  };
}

export function extractAutomationTree(payload) {
  const root = asObject(payload);
  return typeof root.tree === "string" ? root.tree : "";
}

export function normalizeParameterType(typeToken) {
  const type = String(typeToken ?? "").trim();
  return PARAMETER_TYPES.has(type) ? type : null;
}

export function normalizeParameterList(parameters) {
  if (!Array.isArray(parameters)) {
    return [];
  }

  return parameters
    .filter((param) => param && typeof param.name === "string")
    .map((param) => {
      const name = String(param.name).trim();
      const type = normalizeParameterType(param.type);
      return {
        ...param,
        name,
        type: type ?? String(param.type ?? ""),
      };
    })
    .filter((param) => param.name !== "")
    .sort((a, b) => String(a.name).localeCompare(String(b.name)));
}

export function extractParameters(payload) {
  const root = asObject(payload);
  return normalizeParameterList(root.parameters);
}

export function parseSseJsonData(data) {
  if (typeof data !== "string") {
    throw new Error("SSE data payload must be a string");
  }
  return JSON.parse(data);
}

export function parseSseFrame(frameText) {
  const record = { event: "", id: "", data: "" };
  const lines = String(frameText).split(/\r?\n/);
  for (const line of lines) {
    if (line.startsWith("event:")) {
      record.event = line.slice("event:".length).trim();
      continue;
    }
    if (line.startsWith("id:")) {
      record.id = line.slice("id:".length).trim();
      continue;
    }
    if (line.startsWith("data:")) {
      const value = line.slice("data:".length).trim();
      record.data = record.data ? `${record.data}\n${value}` : value;
    }
  }
  return record;
}

export function parseSafeInt64(rawValue) {
  const source = String(rawValue).trim();
  if (!/^-?\d+$/.test(source)) {
    throw new Error("Invalid integer");
  }

  let parsed;
  try {
    parsed = BigInt(source);
  } catch {
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

export function coerceParameterValue({ type, rawValue, min, max, allowedValues }) {
  const normalizedType = normalizeParameterType(type);
  if (!normalizedType) {
    throw new Error("Unsupported parameter type");
  }

  const source = String(rawValue ?? "").trim();

  let value;
  if (normalizedType === "int64") {
    value = parseSafeInt64(source);
  } else if (normalizedType === "double") {
    value = Number(source);
    if (Number.isNaN(value)) {
      throw new Error("Invalid number");
    }
  } else if (normalizedType === "bool") {
    value = source.toLowerCase() === "true";
  } else {
    value = source;
  }

  if (normalizedType === "int64" || normalizedType === "double") {
    const minNumber = min !== undefined ? Number(min) : null;
    const maxNumber = max !== undefined ? Number(max) : null;

    if (Number.isFinite(minNumber) && value < minNumber) {
      throw new Error(`Value below minimum (${minNumber})`);
    }
    if (Number.isFinite(maxNumber) && value > maxNumber) {
      throw new Error(`Value above maximum (${maxNumber})`);
    }
  }

  if (normalizedType === "string" && Array.isArray(allowedValues) && allowedValues.length > 0) {
    const allowed = allowedValues.map((entry) => String(entry));
    if (!allowed.includes(String(value))) {
      throw new Error(`Value must be one of: ${allowed.join(", ")}`);
    }
  }

  return value;
}
