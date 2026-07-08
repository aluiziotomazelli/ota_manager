# Plan: Transport and TLS Configuration Ownership Refactoring

## Objective
Resolve the ownership mismatch where transport-related settings (timeouts, certificates) are defined in `OtaConfig` but injected into `HttpClient` and `OtaSession` which are instantiated externally. This plan ensures `OtaManager` acts as the single source of truth, pushing configuration to dependencies at call time.

## Proposed Changes

### 1. Simplify Dependency Initialization
- Keep `OtaDependencies` as the structure for dependency injection to maintain testability.
- Dependencies (`HttpClient`, `OtaSession`) will remain stateless regarding OTA-specific configuration until they are called.

### 2. Enhance Transport Interfaces
- **`IHttpClient`**: Already updated to accept `timeout_ms` in `fetch()`. No further changes needed for timeouts.
- **`IOtaSession`**: Ensure `begin()` accepts all necessary configuration. Currently, it takes `esp_http_client_config_t*`.

### 3. Consolidate Configuration in `OtaManager`
- `OtaManager` holds the `OtaConfig`.
- When `OtaManager` calls `deps_.http_client.fetch()` or `deps_.ota_session.begin()`, it constructs the necessary request or configuration objects using its internal `config_`.

### 4. Preparation for HTTPS (Ownership focus)
- Even before adding full HTTPS support, define where certificates and security flags will live.
- They will be added to `OtaSecurityConfig` within `OtaConfig`.
- `OtaManager` will be responsible for passing these to `IHttpClient` and `IOtaSession`.

### 5. Update Documentation
- **`API.md`**: Clarify that `OtaManager` owns the lifecycle and configuration of the OTA process, and dependencies are execution engines.
- **`COMPONENT_ISSUES_AND_PRE_HTTPS_FIXES.md`**: Mark this issue as resolved once implemented.

## Verification Plan

### Automated Tests
- **Unit Tests**: Update `test_ota_manager.cpp` to ensure mocks receive the expected configuration parameters (timeouts, etc.) derived from the `OtaConfig` passed to `init()`.
- **Compilability**: Ensure all examples and test apps compile.

### Architecture Review
- Verify that no transport-specific configuration (like a hardcoded timeout or a separate cert pointer) is required during the *construction* of `HttpClient` or `OtaSession` in `app_main`.
