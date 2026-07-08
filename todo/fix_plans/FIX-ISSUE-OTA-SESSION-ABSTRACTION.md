# Plan: IOtaSession Transport-Agnostic Refactoring

## Objective
Resolve the interface leakage where `IOtaSession` depends on `esp_http_client_config_t`. This refactoring will introduce a domain-level request structure to decouple the orchestration layer from ESP-IDF transport details, fulfilling the promise of a transport-agnostic abstraction.

## Proposed Changes

### 1. Define Domain Request Structure (`include/ota_types.hpp`)
- Introduce `OtaDownloadRequest` to encapsulate all data needed to start a download session without leaking implementation details.

```cpp
struct OtaDownloadRequest {
    std::string url;
    uint32_t timeout_ms;
    // Future: OtaSecurityConfig security; or certificate pointers
};
```

### 2. Update `IOtaSession` Interface (`include/interfaces/i_ota_session.hpp`)
- Change the `begin()` method to accept `OtaDownloadRequest` instead of `esp_http_client_config_t*`.

```cpp
virtual esp_err_t begin(const OtaDownloadRequest& request) = 0;
```

### 3. Update `OtaSession` Implementation (`src/ota_session.cpp`)
- Update `OtaSession::begin()` to map the `OtaDownloadRequest` fields into the internal `esp_http_client_config_t` required by `esp_https_ota_begin()`.
- This localizes the dependency on ESP-IDF types within the concrete implementation.

### 4. Update `OtaManager` Orchestration (`src/ota_manager.cpp`)
- Modify `handle_download_state()` to construct and pass an `OtaDownloadRequest`.
- Note: `OtaManager::get_http_config` might be refactored or replaced by a helper that creates this new request structure.

### 5. Update Mocks and Tests
- **`MockOtaSession`**: Update the `begin` mock signature.
- **Tests**: Update `test_ota_manager.cpp` and `test_ota_manager_task.cpp` to align with the new interface.

### 6. Update Documentation
- **`API.md`**: Update the interface descriptions.
- **`COMPONENT_ISSUES_AND_PRE_HTTPS_FIXES.md`**: Mark this issue as resolved.

## Verification Plan

### Automated Tests
- **Unit Tests**: Rebuild and run host tests to ensure the new interface is correctly utilized and mocks are satisfied.
- **On-Target Build**: Verify compilation of `test_apps/test_build` to ensure ESP-IDF integration remains functional.

### Architecture Review
- Verify that `i_ota_session.hpp` no longer includes `esp_http_client.h`.
