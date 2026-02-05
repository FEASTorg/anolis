# Phase 9: C++ Unit Testing Foundation

**Goal**: Add C++ unit testing infrastructure with Google Test and implement unit tests for critical components with complex logic.

**Status**: 🔲 **NOT STARTED**

**Estimated Duration**: 4-5 days

**Approach**: Balanced MVP - Solid foundation without over-engineering

---

## Current Project Tree

```
anolis/
├── .github/
│   └── workflows/
│       └── ci.yml                    # Linux + Windows CI
├── behaviors/
│   └── demo.btproj                   # Behavior tree definitions
├── core/
│   ├── automation/
│   │   ├── bt_nodes.cpp
│   │   ├── bt_nodes.hpp
│   │   ├── bt_runtime.cpp
│   │   ├── bt_runtime.hpp
│   │   ├── mode_manager.cpp          # ⭐ Priority 3 unit test target
│   │   ├── mode_manager.hpp
│   │   └── parameter_manager.cpp
│   ├── control/
│   │   ├── call_router.cpp           # ⭐ Priority 1 unit test target
│   │   └── call_router.hpp
│   ├── events/
│   │   └── event_emitter.hpp
│   ├── http/
│   │   └── http_server.cpp
│   ├── provider/
│   │   ├── framed_stdio_client.cpp
│   │   ├── provider_handle.cpp
│   │   └── provider_process.cpp
│   ├── registry/
│   │   ├── device_registry.cpp
│   │   └── device_registry.hpp
│   ├── runtime/
│   │   ├── config.cpp
│   │   ├── config.hpp
│   │   ├── runtime.cpp
│   │   └── runtime.hpp
│   ├── state/
│   │   ├── state_cache.cpp           # ⭐ Priority 2 unit test target
│   │   └── state_cache.hpp
│   ├── telemetry/
│   │   └── influx_sink.cpp
│   └── CMakeLists.txt
├── docs/
│   ├── architecture.md
│   ├── getting-started.md
│   ├── http-api.md
│   └── providers.md
├── scenarios/                         # ✅ 11 E2E validation scenarios
│   ├── base.py
│   ├── happy_path_end_to_end.py
│   ├── fault_to_manual_recovery.py
│   ├── mode_blocking_policy.py
│   ├── ... (8 more)
│   └── README.md
├── scripts/                           # ✅ Python integration tests + utilities
│   ├── test_all.py                   # Master test orchestrator
│   ├── test_core.py                  # Runtime integration tests
│   ├── test_http.py                  # HTTP API tests
│   ├── test_automation.py            # Automation layer tests
│   ├── test_simulation_devices.py    # Device validation
│   ├── run_scenarios.py              # Scenario runner
│   ├── build.sh / build.ps1          # Build scripts
│   └── run.sh / run.ps1              # Run scripts
├── sdk/                               # Language SDKs (future)
├── spec/
│   └── device-provider/
│       └── protocol.proto            # ADPP protocol definition
├── tools/
│   ├── docker/                       # Telemetry stack
│   ├── grafana/                      # Dashboards
│   └── operator-ui/                  # Web UI
├── working/                           # Planning documents
│   └── plans-to-validation/
├── anolis-runtime.yaml               # Production config
├── anolis-runtime-test.yaml          # Test config (no automation)
├── anolis-runtime-telemetry.yaml     # Telemetry config
├── CMakeLists.txt
├── CONTRIBUTING.md
├── README.md
├── vcpkg.json                        # Dependencies (no gtest yet)
└── requirements.txt                  # Python dependencies
```

**Key Observations**:

- ✅ Good Python integration test structure (scripts/)
- ✅ Well-organized E2E scenarios (scenarios/)
- ✅ CI pipeline working (Linux + Windows)
- ❌ **No C++ unit tests** - no tests/ directory
- ❌ **No Google Test** - not in vcpkg.json
- ❌ **CONTRIBUTING.md** lacks unit test instructions

---

## Motivation

After Phase 8 (provider-sim expansion with fault injection), we have good integration tests (Python) and end-to-end scenarios, but **zero C++ unit tests**. This means:

- **Slow feedback**: Testing C++ logic requires full runtime startup (~30s per test run)
- **Limited edge case coverage**: Integration tests are too coarse to test all conditional branches
- **Refactoring risk**: No safety net for changing core C++ components
- **Debugging difficulty**: Test failures don't pinpoint exact problem

**What Phase 9 adds:**

- Google Test infrastructure on Windows + Linux
- 15-20 unit tests for 2-3 critical components
- Clear test directory structure (tests/unit/)
- Updated CONTRIBUTING.md with testing workflow
- CI integration for automated unit testing

**What Phase 9 explicitly defers** (future phases when value is clearer):

- Coverage measurement (premature with only 15-20 tests)
- Performance benchmarks (no performance issues identified)
- Pre-built test fixtures (let patterns emerge from actual use)
- Moving Python tests (they work fine in scripts/)
- Config file reorganization (3 files is fine)

---

## Target Project Tree (After Phase 9)

```
anolis/
├── .github/
│   └── workflows/
│       └── ci.yml                    # UPDATED: Adds unit test step
├── core/
│   ├── ... (unchanged)
│   └── CMakeLists.txt                # UPDATED: option(BUILD_TESTING)
├── tests/                             # ⭐ NEW DIRECTORY
│   └── unit/
│       ├── CMakeLists.txt            # Unit test CMake config
│       ├── control/
│       │   └── call_router_test.cpp  # 8-10 tests
│       ├── state/
│       │   └── state_cache_test.cpp  # 6-8 tests
│       └── automation/
│           └── mode_manager_test.cpp # 4-6 tests (stretch goal)
├── scripts/                           # UNCHANGED (Python tests stay here)
├── scenarios/                         # UNCHANGED (E2E stays here)
├── vcpkg.json                        # UPDATED: Adds "gtest" dependency
├── CONTRIBUTING.md                   # UPDATED: Unit test instructions
└── ... (rest unchanged)
```

**Changes Summary**:
| Item | Change |
|------|--------|
| `tests/unit/` | NEW - C++ unit test directory |
| `vcpkg.json` | ADD `gtest` dependency |
| `core/CMakeLists.txt` | ADD `option(BUILD_TESTING)` and `add_subdirectory` |
| `.github/workflows/ci.yml` | ADD unit test step (ctest) |
| `CONTRIBUTING.md` | ADD unit test section |

**Unchanged** (explicitly not touched):

- `scripts/` - Python integration tests stay here
- `scenarios/` - E2E scenarios stay here
- Config files - 3 YAML files at root stay as-is
- No new documentation files (just update existing)

---

## Architecture Constraints

- **C++17** - Use Google Test (industry standard, matches project)
- **CMake 3.20+** - Use CTest for test execution
- **vcpkg** - Install Google Test via vcpkg (consistent with project)
- **Cross-platform** - Tests must pass on Windows (MSVC) and Linux (GCC/Clang)
- **Fast** - Unit tests should complete in <100ms total
- **Isolated** - Each test runs independently, no shared state
- **Optional build** - `BUILD_TESTING=OFF` by default (don't burden users)

---

## Target Components for Unit Testing

**Selection Criteria**: Complex logic AND testable in isolation AND high value

### Priority 1: call_router.cpp ⭐ (8-10 tests)

**Location**: `core/control/call_router.cpp` (87 lines header, ~300 lines impl)

**Why test this**:

- Complex validation logic with many edge cases
- Precondition checking with AND/OR boolean logic
- Parameter validation (types, ranges, required/optional)
- Device/function existence validation
- Recent bugs found (Phase 8 signal fault injection)

**Testable methods**:

- `validate_call()` - Main validation entry point
- `validate_device_exists()` - Device lookup
- `validate_function_exists()` - Function lookup
- `validate_arguments()` - Parameter checking
- `parse_device_handle()` - Handle parsing

**Test cases**:

1. Valid call with all required parameters → passes
2. Missing required parameter → fails with error
3. Wrong parameter type (string vs int) → fails
4. Parameter out of range → fails
5. Device not found → fails
6. Function not found → fails
7. Precondition AND (all true) → passes
8. Precondition AND (one false) → fails
9. Precondition OR (one true) → passes
10. Empty preconditions → passes

### Priority 2: state_cache.cpp ⭐ (6-8 tests)

**Location**: `core/state/state_cache.cpp` (146 lines header, ~200 lines impl)

**Why test this**:

- Critical path - every signal read goes through here
- Staleness detection based on timestamps (time logic tricky)
- Edge cases: missing signals, quality handling

**Testable methods**:

- `CachedSignalValue::is_stale()` - Staleness check
- `CachedSignalValue::age()` - Age calculation
- `get_device_state()` - State retrieval
- `get_signal_value()` - Signal lookup

**Test cases**:

1. Insert new signal → retrievable
2. Update existing signal → new value returned
3. Fresh signal (age < threshold) → not stale
4. Stale signal (age > threshold) → is stale
5. Missing signal → returns nullptr
6. Quality GOOD → not stale
7. Quality BAD → is stale (if quality-based staleness enabled)
8. Age calculation → correct milliseconds

### Priority 3: mode_manager.cpp (4-6 tests) - Stretch Goal

**Location**: `core/automation/mode_manager.cpp` (115 lines header, ~150 lines impl)

**Why test this** (if time permits):

- Clean state machine - very testable
- Mode transitions have clear rules
- Foundation for automation tests

**Testable methods**:

- `current_mode()` - Get current mode
- `request_mode()` - Request transition
- `mode_to_string()` / `string_to_mode()` - Conversion

**Test cases**:

1. Initial mode is MANUAL
2. MANUAL → AUTO → succeeds
3. MANUAL → FAULT → succeeds
4. FAULT → MANUAL → succeeds (recovery)
5. FAULT → AUTO → fails (must go through MANUAL)
6. Mode string conversion round-trip

**Total Target**: 18-24 unit tests for 2-3 components

---

## Subtasks

### 9.1 Set Up Google Test Infrastructure (Day 1)

**Objective**: Get Google Test building and running on Windows + Linux

**Tasks**:

1. **Add Google Test to vcpkg.json**:

   ```json
   {
     "dependencies": [
       "gtest"
       // ... existing deps
     ]
   }
   ```

2. **Create tests/unit/ directory structure**:

   ```
   tests/
     unit/
       CMakeLists.txt
       control/
         call_router_test.cpp
       state/
         state_cache_test.cpp
       automation/
         mode_manager_test.cpp
   ```

3. **Update core/CMakeLists.txt** (add at end):

   ```cmake
   # Unit Testing (optional, for developers)
   option(BUILD_TESTING "Build unit tests" OFF)
   if(BUILD_TESTING)
       enable_testing()
       add_subdirectory(../tests/unit tests_unit)
   endif()
   ```

4. **Create tests/unit/CMakeLists.txt**:

   ```cmake
   find_package(GTest CONFIG REQUIRED)
   include(GoogleTest)

   # call_router tests
   add_executable(call_router_test control/call_router_test.cpp)
   target_link_libraries(call_router_test PRIVATE
       GTest::gtest_main
       anolis_control
       anolis_registry
       anolis_state
   )
   gtest_discover_tests(call_router_test)

   # state_cache tests
   add_executable(state_cache_test state/state_cache_test.cpp)
   target_link_libraries(state_cache_test PRIVATE
       GTest::gtest_main
       anolis_state
   )
   gtest_discover_tests(state_cache_test)
   ```

5. **Create skeleton test files** with one passing test each

6. **Verify builds**:

   ```bash
   # Windows
   cmake -B build -DBUILD_TESTING=ON -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
   cmake --build build --config Release
   cd build && ctest -C Release --output-on-failure

   # Linux
   cmake -B build -DBUILD_TESTING=ON -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
   cmake --build build
   cd build && ctest --output-on-failure
   ```

**Exit Criteria**:

- [ ] Google Test installed via vcpkg
- [ ] `tests/unit/` directory exists with CMakeLists.txt
- [ ] Skeleton tests compile and pass
- [ ] `ctest` runs successfully on Windows + Linux
- [ ] Build with `BUILD_TESTING=OFF` still works (no gtest required)

**Time Estimate**: 1 day

---

### 9.2 Implement call_router Tests (Day 2)

**Objective**: Write 8-10 unit tests for call_router validation logic

**File**: `tests/unit/control/call_router_test.cpp`

**Approach**:

- Create minimal test fixtures (mock registry, mock state)
- Test public validation methods
- Focus on edge cases that are hard to test via integration tests

**Example test structure**:

```cpp
#include <gtest/gtest.h>
#include "control/call_router.hpp"
#include "registry/device_registry.hpp"
#include "state/state_cache.hpp"

class CallRouterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create minimal registry with test device
        // Create state cache with test signals
        // Create call router instance
    }

    std::unique_ptr<registry::DeviceRegistry> registry_;
    std::unique_ptr<state::StateCache> state_cache_;
    std::unique_ptr<control::CallRouter> router_;
};

TEST_F(CallRouterTest, ValidateCall_ValidRequest_Succeeds) {
    control::CallRequest request{
        .device_handle = "sim0/tempctl0",
        .function_name = "set_temperature",
        .args = {{"target_temp", MakeValue(25.0)}}
    };

    std::string error;
    EXPECT_TRUE(router_->validate_call(request, error));
    EXPECT_TRUE(error.empty());
}

TEST_F(CallRouterTest, ValidateCall_DeviceNotFound_Fails) {
    control::CallRequest request{
        .device_handle = "sim0/nonexistent",
        .function_name = "set_temperature",
        .args = {}
    };

    std::string error;
    EXPECT_FALSE(router_->validate_call(request, error));
    EXPECT_THAT(error, ::testing::HasSubstr("not found"));
}
```

**Tests to implement**:

1. `ValidateCall_ValidRequest_Succeeds`
2. `ValidateCall_MissingRequiredParam_Fails`
3. `ValidateCall_WrongParamType_Fails`
4. `ValidateCall_ParamOutOfRange_Fails`
5. `ValidateCall_DeviceNotFound_Fails`
6. `ValidateCall_FunctionNotFound_Fails`
7. `ValidateCall_PreconditionAND_AllTrue_Passes`
8. `ValidateCall_PreconditionAND_OneFalse_Fails`
9. `ValidateCall_PreconditionOR_OneTrue_Passes`
10. `ValidateCall_EmptyPreconditions_Passes`

**Exit Criteria**:

- [ ] 8-10 tests written and passing
- [ ] Tests cover key validation paths
- [ ] Tests fail when validation logic is intentionally broken
- [ ] No flaky tests

**Time Estimate**: 1.5 days

---

### 9.3 Implement state_cache Tests (Day 3-4)

**Objective**: Write 6-8 unit tests for state_cache

**File**: `tests/unit/state/state_cache_test.cpp`

**Approach**:

- Test CachedSignalValue methods directly
- Test StateCache lookup methods with mock data
- Focus on staleness and edge cases

**Example test structure**:

```cpp
#include <gtest/gtest.h>
#include "state/state_cache.hpp"
#include <chrono>

class CachedSignalValueTest : public ::testing::Test {
protected:
    anolis::state::CachedSignalValue CreateFreshSignal() {
        return {
            .value = MakeDoubleValue(42.0),
            .timestamp = std::chrono::system_clock::now(),
            .quality = SignalValue_Quality_GOOD
        };
    }

    anolis::state::CachedSignalValue CreateStaleSignal() {
        return {
            .value = MakeDoubleValue(42.0),
            .timestamp = std::chrono::system_clock::now() - std::chrono::seconds(10),
            .quality = SignalValue_Quality_GOOD
        };
    }
};

TEST_F(CachedSignalValueTest, Age_FreshSignal_ReturnsSmallAge) {
    auto signal = CreateFreshSignal();
    auto age = signal.age();
    EXPECT_LT(age.count(), 100); // Less than 100ms
}

TEST_F(CachedSignalValueTest, IsStale_FreshSignal_ReturnsFalse) {
    auto signal = CreateFreshSignal();
    EXPECT_FALSE(signal.is_stale());
}

TEST_F(CachedSignalValueTest, IsStale_StaleSignal_ReturnsTrue) {
    auto signal = CreateStaleSignal();
    EXPECT_TRUE(signal.is_stale());
}
```

**Tests to implement**:

1. `CachedSignalValue_Age_CalculatesCorrectly`
2. `CachedSignalValue_IsStale_FreshSignal_ReturnsFalse`
3. `CachedSignalValue_IsStale_OldSignal_ReturnsTrue`
4. `CachedSignalValue_IsStale_BadQuality_ReturnsTrue`
5. `StateCache_GetSignalValue_ExistingSignal_ReturnsValue`
6. `StateCache_GetSignalValue_MissingSignal_ReturnsNull`
7. `StateCache_GetDeviceState_ExistingDevice_ReturnsState`
8. `StateCache_GetDeviceState_MissingDevice_ReturnsNull`

**Exit Criteria**:

- [ ] 6-8 tests written and passing
- [ ] Staleness logic thoroughly tested
- [ ] Edge cases covered (missing signals, bad quality)
- [ ] No time-dependent flakiness

**Time Estimate**: 1 day

---

### 9.4 CI Integration & Documentation (Day 4-5)

**Objective**: Integrate unit tests into CI and update documentation

**CI Integration** - Update `.github/workflows/ci.yml`:

```yaml
# After the "Build anolis" step, add:
- name: Run unit tests
  run: |
    cd build
    ctest -C Release --output-on-failure --timeout 60
```

Both Linux and Windows jobs need this addition.

**Documentation** - Update `CONTRIBUTING.md`:

Add section after "### Testing":

````markdown
### Unit Tests (C++)

Unit tests validate C++ components in isolation. They require Google Test.

```bash
# Build with tests enabled
cmake -B build -DBUILD_TESTING=ON -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release  # Windows
cmake --build build                    # Linux

# Run unit tests
cd build
ctest -C Release --output-on-failure  # Windows
ctest --output-on-failure              # Linux
```
````

#### Adding New Unit Tests

1. Create test file in `tests/unit/<component>/<name>_test.cpp`
2. Add test executable to `tests/unit/CMakeLists.txt`
3. Use Google Test macros: `TEST`, `TEST_F`, `EXPECT_*`, `ASSERT_*`
4. Run tests locally before pushing

See `tests/unit/control/call_router_test.cpp` for examples.

#### Test Requirements

All PRs should include tests:

- **New C++ logic**: Add unit tests in `tests/unit/`
- **New API endpoints**: Add integration tests in `scripts/`
- **Bug fixes**: Add regression test to prevent recurrence

````

**Exit Criteria**:
- [ ] CI runs unit tests on Linux + Windows
- [ ] CI fails if any unit test fails
- [ ] CONTRIBUTING.md documents unit test workflow
- [ ] All existing tests still pass

**Time Estimate**: 0.5 days

---

### 9.5 (Stretch) mode_manager Tests (Day 5)

**Objective**: If time permits, add 4-6 tests for mode_manager

**File**: `tests/unit/automation/mode_manager_test.cpp`

**Tests to implement**:
1. `ModeManager_InitialMode_IsManual`
2. `ModeManager_TransitionManualToAuto_Succeeds`
3. `ModeManager_TransitionFaultToManual_Succeeds`
4. `ModeManager_TransitionFaultToAuto_Fails`
5. `ModeToString_ValidModes_ReturnsCorrectString`
6. `StringToMode_ValidStrings_ReturnsCorrectMode`

**Exit Criteria**:
- [ ] 4-6 tests written and passing
- [ ] State transition rules validated

**Time Estimate**: 0.5 days (stretch goal)

---

## Dependencies

### New C++ Dependencies (vcpkg)

```json
{
  "dependencies": [
    "gtest"
  ]
}
````

This adds:

- **Google Test**: Unit testing framework
- **Google Mock**: Mocking framework (included automatically)

### Build Tools (Already Have)

- CMake 3.20+ ✅
- CTest (included with CMake) ✅
- C++17 compiler ✅

### No New Python Dependencies

- Existing `requests` for integration tests ✅

---

## Exit Criteria

### Must Have ✅

- [ ] Google Test building on Windows + Linux via vcpkg
- [ ] `tests/unit/` directory with working CMake
- [ ] 14-18 unit tests passing:
  - [ ] 8-10 tests for call_router.cpp
  - [ ] 6-8 tests for state_cache.cpp
- [ ] Unit tests integrated into CI (both platforms)
- [ ] CI fails on unit test failure
- [ ] CONTRIBUTING.md documents unit test workflow
- [ ] All existing Python tests still passing
- [ ] Build with `BUILD_TESTING=OFF` still works

### Should Have

- [ ] 4-6 mode_manager tests (stretch goal)
- [ ] No flaky tests

### Explicitly Deferred (Future Phases)

- ⏸️ Coverage measurement (wait until 50+ tests)
- ⏸️ Pre-built test fixtures (extract when patterns emerge)
- ⏸️ Performance benchmarks (no issues to solve)
- ⏸️ Move Python tests (working fine in scripts/)
- ⏸️ Config reorganization (3 files is fine)

---

## Success Metrics

1. **Infrastructure works**: Can build and run `ctest` on both platforms
2. **Tests useful**: Tests fail when logic is intentionally broken
3. **CI integrated**: Every commit runs unit tests automatically
4. **Documented**: Developer can add new unit test without asking
5. **Foundation solid**: Easy to add more tests in future phases

---

## Timeline

**Total Duration**: 4-5 days

| Day | Task                                                     | Deliverable                  |
| --- | -------------------------------------------------------- | ---------------------------- |
| 1   | 9.1 Google Test setup                                    | Build infrastructure working |
| 2   | 9.2 call_router tests (part 1)                           | 5 tests passing              |
| 3   | 9.2 call_router tests (part 2) + 9.3 state_cache (start) | 10 tests passing             |
| 4   | 9.3 state_cache tests + 9.4 CI/docs                      | 16+ tests, CI running        |
| 5   | 9.5 mode_manager (stretch) + polish                      | Final validation             |

**Buffer**: Day 5 provides buffer for Windows-specific issues or stretch goal.

---

## Risk Mitigation

| Risk                        | Probability | Impact | Mitigation                                         |
| --------------------------- | ----------- | ------ | -------------------------------------------------- |
| Google Test Windows issues  | Medium      | High   | Use vcpkg (well-tested), start with Windows        |
| Tests require heavy mocking | Low         | Medium | Start with pure functions, mock minimally          |
| Circular dependency issues  | Low         | Medium | Test public interfaces only                        |
| Tests flaky on CI           | Low         | Medium | Avoid time-sensitive tests, use deterministic data |

---

## What This Phase Enables

**Immediate Benefits**:

- ✅ Fast feedback: Unit tests run in <1s vs. ~30s integration tests
- ✅ Edge case coverage: Test corner cases easily
- ✅ Refactoring confidence: Change implementation with safety net
- ✅ Debugging efficiency: Failing test pinpoints exact problem

**Future Benefits**:

- Foundation for adding more unit tests (Phase 10+ features)
- Established patterns for C++ testing in this project
- CI catches regressions immediately
- Easier onboarding for new contributors

**What It Doesn't Do** (by design):

- ❌ Replace integration tests (they're still valuable)
- ❌ Test all components (just proving infrastructure works)
- ❌ Measure coverage (not useful with only 20 tests)
- ❌ Reorganize project structure (not broken)

---

## Relation to Other Phases

- **Phase 8** (Provider-sim): Identified bugs that unit tests could have caught
- **Phase 10** (Operator UI): Will benefit from fast backend validation
- **Phase 11** (Acceptance Harness): Unit tests complement E2E tests
- **Future**: Coverage/benchmarks can be added when justified

---

## Notes

### Design Decisions

1. **Why `tests/unit/` not `core/tests/`?**
   - Follows convention: tests separate from product code
   - Easier to exclude from user builds (`BUILD_TESTING=OFF`)
   - Clear separation of concerns

2. **Why `BUILD_TESTING=OFF` by default?**
   - Users don't need gtest dependency
   - Faster builds for production
   - Developers opt-in with `-DBUILD_TESTING=ON`

3. **Why only call_router + state_cache first?**
   - Complex logic = high ROI for unit testing
   - Known bug history = validates testing value
   - Clean interfaces = testable in isolation

4. **Why not move Python tests?**
   - They work fine in scripts/
   - Moving files is risk with no benefit
   - YAGNI: organize when needed

5. **Why defer coverage?**
   - 20 tests = tiny coverage %
   - Coverage tooling has platform pain
   - More valuable when 50+ tests exist

### Future Expansion Path

**Phase 11**: After acceptance harness, consider:

- Add tests for more components (device_registry, telemetry)
- Extract common test helpers if patterns emerge
- Target: 40-50 total unit tests

**Phase 13**: After hardening, consider:

- Add coverage measurement (now tests justify tooling)
- Add benchmarks (if performance issues found)
- Target: Comprehensive test suite

**Philosophy**: Build testing infrastructure incrementally as value is proven.
