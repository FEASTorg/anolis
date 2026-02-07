# Clang-Tidy First-Pass Checklist (High-Value Issues)

This checklist focuses on `bugprone-*` and `performance-*` issues only.
Readability checks are intentionally de-prioritized for now.

**Scope**

- Codebase: `core/` and `tests/` (per current `HeaderFilterRegex`).
- Target checks: `bugprone-*`, `performance-*`.
- Excluded checks: `readability-identifier-length`, `readability-magic-numbers`, `readability-identifier-naming`.

**Phase 1: Highest ROI Header Fixes**

- [ ] Fix `bugprone-macro-parentheses` in `core/logging/logger.hpp` (macro argument parentheses).
- [ ] Fix `bugprone-branch-clone` in `core/http/errors.hpp`.
- [ ] Fix `bugprone-branch-clone` in `core/telemetry/influx_sink.hpp`.

**Phase 2: Performance Fixes in Hot Headers**

- [ ] `core/events/event_emitter.hpp`: resolve `performance-unnecessary-value-param` (queue/unsubscribe_fn).
- [ ] `core/telemetry/influx_sink.hpp`: resolve `performance-unnecessary-value-param` (emitter).

**Phase 3: Performance Fixes in Source Files**

- [ ] `core/control/call_router.cpp`: fix `performance-inefficient-string-concatenation`.
- [ ] `core/http/json.cpp`: fix `performance-inefficient-string-concatenation`.
- [ ] `core/state/state_cache.cpp`: fix `performance-unnecessary-copy-initialization`.
- [ ] `core/automation/mode_manager.cpp`: fix `performance-unnecessary-value-param`.
- [ ] `core/automation/parameter_manager.cpp`: fix `performance-unnecessary-value-param`.
- [ ] `core/http/server.cpp`: fix `performance-unnecessary-value-param`.

**Phase 4: Swappable Parameter Triage**

- [ ] `core/provider/framed_stdio_client.cpp`: address `bugprone-easily-swappable-parameters`.
- [ ] `core/provider/provider_process.cpp`: address `bugprone-easily-swappable-parameters`.
- [ ] `core/control/call_router.cpp`: address `bugprone-easily-swappable-parameters`.
- [ ] `core/events/event_emitter.hpp`: address `bugprone-easily-swappable-parameters`.
- [ ] `core/state/state_cache.cpp`: address `bugprone-easily-swappable-parameters`.
- [ ] `core/http/handlers/utils.hpp`: address `bugprone-easily-swappable-parameters`.
- [ ] `core/http/json.cpp`: address `bugprone-easily-swappable-parameters`.
- [ ] `tests/unit/config_test.cpp`: address `bugprone-easily-swappable-parameters` (optional).

**Validation**

- [ ] Re-run clang-tidy on the same build/compile_commands.
- [ ] Re-run `scripts/summarize_clang_tidy.py` and verify bugprone/performance counts drop.
- [ ] Confirm no API breaks (build + unit tests if available).
