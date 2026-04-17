import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

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
  parseSafeInt64,
  parseSseFrame,
  parseSseJsonData,
} from "../../frontend/js/operate/contracts.js";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, "../../../..");
const fixtureRoot = path.join(repoRoot, "tests/contracts/runtime-http/examples");

function loadJsonFixture(name) {
  return JSON.parse(fs.readFileSync(path.join(fixtureRoot, name), "utf8"));
}

function loadTextFixture(name) {
  return fs.readFileSync(path.join(fixtureRoot, name), "utf8");
}

test("extractDevices returns normalized array from fixture payload", () => {
  const payload = loadJsonFixture("devices.200.json");
  const devices = extractDevices(payload);
  assert.ok(Array.isArray(devices));
  assert.ok(devices.length > 0);
  assert.equal(typeof devices[0].provider_id, "string");
});

test("extractCapabilities normalizes function metadata and arg specs", () => {
  const payload = loadJsonFixture("device-capabilities.200.json");
  const capabilities = extractCapabilities(payload);

  assert.ok(Array.isArray(capabilities.signals));
  assert.ok(Array.isArray(capabilities.functions));
  assert.ok(capabilities.functions.length > 0);

  const firstFn = capabilities.functions[0];
  assert.equal(typeof firstFn.function_id, "number");
  assert.equal(typeof firstFn.display_name, "string");
  assert.ok(Array.isArray(firstFn.args));
});

test("extractDeviceStateValues normalizes timestamp_epoch_ms to timestamp_ms", () => {
  const payload = loadJsonFixture("device-state.200.json");
  const values = extractDeviceStateValues(payload);
  assert.ok(Array.isArray(values));
  assert.ok(values.length > 0);
  assert.equal(values[0].timestamp_ms, payload.values[0].timestamp_epoch_ms);
});

test("automation and runtime status extractors preserve key fields", () => {
  const runtimePayload = loadJsonFixture("runtime-status.200.json");
  const automationPayload = loadJsonFixture("automation-status.200.json");

  const runtime = extractRuntimeStatus(runtimePayload);
  const automation = extractAutomationStatus(automationPayload);

  assert.equal(runtime.status.code, "OK");
  assert.equal(typeof runtime.mode, "string");
  assert.equal(typeof runtime.device_count, "number");

  assert.equal(automation.status.code, "OK");
  assert.equal(typeof automation.bt_status, "string");
  assert.equal(typeof automation.total_ticks, "number");
});

test("mode, parameters, provider health, and tree fixtures parse correctly", () => {
  const modePayload = loadJsonFixture("mode.get.200.json");
  const parameterPayload = loadJsonFixture("parameters.get.200.json");
  const providerPayload = loadJsonFixture("providers-health.200.json");
  const treePayload = loadJsonFixture("automation-tree.200.json");

  const mode = extractMode(modePayload);
  const parameters = extractParameters(parameterPayload);
  const providers = extractProvidersHealth(providerPayload);
  const tree = extractAutomationTree(treePayload);

  assert.equal(mode, "MANUAL");
  assert.ok(Array.isArray(parameters));
  assert.ok(parameters.length > 0);
  assert.ok(Array.isArray(providers));
  assert.ok(providers.length > 0);
  assert.ok(typeof tree === "string");
  assert.ok(tree.length > 0);
});

test("SSE frame parser and JSON parser decode canonical fixture", () => {
  const frameText = loadTextFixture("events.200.txt");
  const frame = parseSseFrame(frameText);
  const payload = parseSseJsonData(frame.data);

  assert.equal(frame.event, "state_update");
  assert.equal(frame.id, "123");
  assert.equal(payload.provider_id, "sim0");
  assert.equal(payload.device_id, "tempctl0");
});

test("parseSafeInt64 enforces int64 and browser-safe bounds", () => {
  assert.equal(parseSafeInt64("42"), 42);
  assert.throws(() => parseSafeInt64("9223372036854775807"), /browser-safe/i);
  assert.throws(() => parseSafeInt64("not-a-number"), /invalid integer/i);
});
