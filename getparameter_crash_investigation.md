# GetParameterNode Registration Crash Investigation

**Date:** 2026-02-04  
**Status:** RESOLVED  
**Impact:** Fixed - parameter access in behavior trees restored

## Problem Summary

The runtime crashed with exit code `0xC0000409` (STATUS_STACK_BUFFER_OVERRUN) during BehaviorTree.CPP node registration when attempting to register `GetParameterNode`. The crash occurred specifically during the `factory_->registerNodeType<GetParameterNode>("GetParameter")` call.

## Observed Behavior

### Console Output Before Crash

```
[Runtime] Creating BT runtime
[BTRuntime] Initialized
[BTRuntime] About to register ReadSignal
[BTRuntime] About to register CallDevice
[BTRuntime] About to register CheckQuality
[BTRuntime] About to register GetParameter
anolis-provider-sim: EOF on stdin; exiting cleanly
```

The runtime exits immediately after "About to register GetParameter" without any error message or exception. The provider process exits because the runtime crash closes all file handles.

### Exit Code

- **Windows:** `0xC0000409` (STATUS_STACK_BUFFER_OVERRUN)
- Indicates memory corruption or stack buffer overflow
- This is an SEH (Structured Exception Handling) fault, not a C++ exception

## What We Tried

### 1. Simplified GetParameterNode Implementation

Reduced the class to absolute minimum (empty tick(), no private methods):

```cpp
class GetParameterNode : public BT::SyncActionNode {
public:
    GetParameterNode(const std::string& name, const BT::NodeConfig& config)
        : BT::SyncActionNode(name, config) {}

    BT::NodeStatus tick() override { return BT::NodeStatus::SUCCESS; }

    static BT::PortsList providedPorts() {
        return { BT::InputPort<std::string>("name") };
    }
};
```

**Result:** Still crashes

### 2. Removed Forward Declaration Issues

Changed `automation::ParameterManager*` to just `ParameterManager*` inside the namespace.
**Result:** Still crashes

### 3. Compared to Working Nodes

ReadSignalNode, CallDeviceNode, and CheckQualityNode all register successfully using identical patterns.
**Result:** No structural difference found

### 4. Registration Order Test

Commenting out GetParameterNode registration allows other nodes to register and runtime to start successfully.
**Result:** Issue is specific to GetParameterNode

### 5. Checked CMakeLists.txt

Verified bt_nodes.cpp is properly included in build.
**Result:** Build configuration is correct

## Root Cause

BehaviorTree.CPP reserves the port name `name`. Our `GetParameterNode::providedPorts()` used an input port called `name`, which is explicitly disallowed by BT.CPP. This triggers an internal error during `registerNodeType()` that manifests as a crash in MSVC Release builds.

Evidence: BT.CPP tests include `IllegalPortNameNode` that throws on registration when the port name is `name`.

## Fix Applied

1. Rename the input port from `name` → `param`.
2. Update XML usage to `param="temp_setpoint"`.
3. Re-enable `GetParameterNode` registration.

After this change, registration succeeds and the runtime starts normally with BT parameter access restored.

## Current Status

GetParameterNode registration is **enabled** in bt_runtime.cpp, and the demo behavior tree uses the GetParameter node with the updated `param` port.

## Code Structure

### Header (bt_nodes.hpp)

```cpp
class GetParameterNode : public BT::SyncActionNode {
public:
    GetParameterNode(const std::string& name, const BT::NodeConfig& config);
    BT::NodeStatus tick() override;
    static BT::PortsList providedPorts();
private:
    automation::ParameterManager* get_parameter_manager();
};
```

### Implementation (bt_nodes.cpp)

```cpp
GetParameterNode::GetParameterNode(const std::string& name, const BT::NodeConfig& config)
    : BT::SyncActionNode(name, config) {}

BT::PortsList GetParameterNode::providedPorts() {
    return {
        BT::InputPort<std::string>("param", "Parameter name"),
        BT::OutputPort<double>("value", "Parameter value")
    };
}

BT::NodeStatus GetParameterNode::tick() {
    // Implementation...
}
```

## Environment

- **OS:** Windows 11
- **Compiler:** MSVC 17.12.12
- **Build:** Release
- **BehaviorTree.CPP:** Version from vcpkg
- **Architecture:** x64

## Theories

### 1. BehaviorTree.CPP Template Instantiation Issue

The crash happens during template instantiation of `registerNodeType<GetParameterNode>`. Possible causes:

- Name collision with internal BT.CPP types
- Template parameter deduction issue
- ABI mismatch between BT.CPP library and our code

### 2. Static Initialization Order

GetParameterNode is the 4th node registered. Perhaps there's a cumulative memory issue or static initialization order problem.

### 3. Symbol Collision

The name "GetParameter" or "GetParameterNode" might conflict with something in BT.CPP or Windows headers.

### 4. Memory Corruption Elsewhere

The crash manifests during GetParameterNode registration but the root cause is earlier memory corruption.

## Next Steps

1. **Research BehaviorTree.CPP Issues**
   - Check GitHub issues for similar registration crashes
   - Review BT.CPP documentation for node registration requirements
   - Check if there are known MSVC compatibility issues

2. **Try Alternative Approaches**
   - Rename to completely different name (e.g., "ParamReader", "FetchParam")
   - Try registering with a lambda factory function instead of template
   - Build in Debug mode to get better error information

3. **Binary Analysis**
   - Run under WinDbg to get detailed crash dump
   - Check if there's a stack overflow in Debug builds
   - Examine assembly around registration call

4. **Minimal Reproduction**
   - Create standalone test case with just BT.CPP and GetParameterNode
   - Test on Linux to see if issue is Windows/MSVC specific

## Impact on Phase 7C

- ✅ Parameter Manager: Working
- ✅ HTTP API: Working (GET/POST /v0/parameters)
- ✅ Telemetry Events: Working (ParameterChangeEvent)
- ✅ YAML Configuration: Working
- ✅ BT Parameter Access: **RESTORED**

## Files Affected

- `core/automation/bt_nodes.hpp` - GetParameterNode declaration
- `core/automation/bt_nodes.cpp` - GetParameterNode implementation
- `core/automation/bt_runtime.cpp` - Registration enabled
- `behaviors/demo.xml` - GetParameter node uses `param` port
- `scripts/test_automation.py` - Tests pass with GetParameter enabled

## References

- BehaviorTree.CPP docs: https://www.behaviortree.dev/
- Windows error codes: https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes
- STATUS_STACK_BUFFER_OVERRUN: Security check failure, usually indicates buffer overflow
- BT.CPP GitHub: https://github.com/BehaviorTree/BehaviorTree.CPP

## Research Findings

Searched BehaviorTree.CPP repository for similar issues:

- No reported crashes during `registerNodeType()` on Windows/MSVC
- All example nodes follow same pattern as GetParameterNode
- Issue #959 mentions "Version conflict with embedded nlohmann::json can cause undefined behavior" - could be related?
- Node registration pattern is used extensively in tests without issues

Key observations from BT.CPP source:

1. `registerNodeType<T>()` calls `CreateManifest<T>()` which calls `getProvidedPorts<T>()`
2. `getProvidedPorts<T>()` internally calls `T::providedPorts()` at compile time
3. The crash happens during template instantiation, not during runtime node creation
4. All examples use identical constructor signature: `(const std::string&, const NodeConfig&)`

## Possible Root Causes

### 1. **MSVC Template Instantiation Bug**

The crash happens during `registerNodeType<GetParameterNode>()` which involves heavy template metaprogramming. MSVC might have an issue with the specific combination of templates used.

### 2. **Name Collision**

"GetParameter" might collide with a Windows API or MSVC intrinsic. Try renaming to something unique like "FetchParameter" or "RetrieveParameter".

### 3. **Static Initialization Order Fiasco**

If GetParameterNode uses any static members (even transitively through included headers), initialization order could cause the crash.

### 4. **Stack Size Issue**

Windows default stack size is 1MB. Deep template instantiation might overflow. Try:

```cmake
set_target_properties(anolis-runtime PROPERTIES LINK_FLAGS "/STACK:2097152")  # 2MB stack
```

### 5. **Compiler Optimization Bug**

Release mode might trigger an MSVC optimizer bug. Test in Debug mode to verify.

## Action Plan

**Priority 1: Quick Wins**

1. ✅ Rename input port from `name` to `param`
2. ✅ Re-enable GetParameterNode registration
3. ⏭️ Build in Debug mode to see if crash persists
4. ⏭️ Increase stack size via linker flags

**Priority 2: Deeper Investigation** 5. ⏭️ Run under WinDbg to get detailed crash dump and stack trace 6. ⏭️ Create minimal reproduction with standalone BT.CPP test 7. ⏭️ Test on Linux to confirm Windows/MSVC specific 8. ⏭️ Check if vcpkg BT.CPP version has known issues

**Priority 3: Alternative Approaches** 9. ⏭️ Use `registerSimpleAction()` with lambda instead of `registerNodeType<T>()` 10. ⏭️ Create GetParameter as a composite of existing nodes 11. ⏭️ Access parameters via blackboard directly instead of dedicated node

## Verification Status

**Current State:**

- GetParameterNode registration is **enabled** in bt_runtime.cpp
- Demo behavior tree uses GetParameter with `param=` input port
- All Phase 7C features functional

**What Works:**

- ✅ HTTP GET /v0/parameters - List all parameters with values
- ✅ HTTP POST /v0/parameters - Update parameter with validation
- ✅ Parameter change events to telemetry
- ✅ YAML parameter configuration
- ✅ Parameter manager initialization
- ✅ Accessing parameters from behavior trees
