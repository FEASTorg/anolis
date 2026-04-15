import test from "node:test";
import assert from "node:assert/strict";

import {
  resolveOperatorUiBase,
  trimTrailingSlash,
} from "../../frontend/js/launch.js";

test("trimTrailingSlash removes only one trailing slash", () => {
  assert.equal(trimTrailingSlash("http://localhost:3000/"), "http://localhost:3000");
  assert.equal(trimTrailingSlash("http://localhost:3000"), "http://localhost:3000");
});

test("resolveOperatorUiBase prefers explicit runtime.operator_ui_base", () => {
  global.window = { __ANOLIS_COMPOSER__: { operatorUiBase: "http://composer.local:3000" } };
  const system = {
    topology: {
      runtime: {
        operator_ui_base: "https://ops.local/ui/",
        cors_origins: ["http://cors.local:3000"],
      },
    },
  };
  assert.equal(resolveOperatorUiBase(system), "https://ops.local/ui");
});

test("resolveOperatorUiBase falls back to first HTTP(S) cors_origin", () => {
  global.window = { __ANOLIS_COMPOSER__: { operatorUiBase: "http://composer.local:3000" } };
  const system = {
    topology: {
      runtime: {
        cors_origins: ["*", "not-a-url", "http://cors.local:3100/"],
      },
    },
  };
  assert.equal(resolveOperatorUiBase(system), "http://cors.local:3100");
});

test("resolveOperatorUiBase then falls back to composer status metadata", () => {
  global.window = { __ANOLIS_COMPOSER__: { operatorUiBase: "http://composer.local:3000/" } };
  const system = { topology: { runtime: {} } };
  assert.equal(resolveOperatorUiBase(system), "http://composer.local:3000");
});

test("resolveOperatorUiBase uses localhost default when no source is available", () => {
  global.window = {};
  const system = { topology: { runtime: {} } };
  assert.equal(resolveOperatorUiBase(system), "http://localhost:3000");
});
