# Plan: Version Validation Robustness Refactoring

## Objective
Fix a potential runtime crash in `OtaManager::handle_download_state()` where a `std::optional<OtaVersion>` is accessed via `.value()` without first checking if it contains a value. This ensures robust error handling if the running firmware's version string is malformed.

## Proposed Changes

### 1. Fix Unchecked Optional in `src/ota_manager.cpp`
- Update `OtaManager::handle_download_state()` to explicitly check both `current_v_opt` and `new_v_opt` before comparing them.

```cpp
        auto current_v_opt = VersionHelper::parse(deps_.system.get_running_app_desc()->version);
        auto new_v_opt = VersionHelper::parse(new_app_info.version);
        
        if (!current_v_opt.has_value() || !new_v_opt.has_value() ||
            !is_version_newer(current_v_opt.value(), new_v_opt.value(), config_.allow_same_version)) {
            // ... handle error ...
        }
```

### 2. Update Documentation
- **`COMPONENT_ISSUES_AND_PRE_HTTPS_FIXES.md`**: Mark this issue as resolved.

## Verification Plan

### Automated Tests
- **Unit Tests**: Add a test case to `test_ota_manager.cpp` that simulates a malformed running version string (returning a value that `VersionHelper::parse` cannot handle) and verify that `handle_download_state` returns `OtaStepResult::FAILED` instead of crashing.
- **On-Target Build**: Verify compilation of `test_apps/test_build`.

### Regression Check
- Ensure `handle_version_state()` remains safe as it already implements this check correctly.
