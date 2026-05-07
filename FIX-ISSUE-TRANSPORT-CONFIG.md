# Plan: Inconsistent Transport Configuration Refactoring

## Objective
Resolve the inconsistency where manifest fetching uses a hardcoded 5000ms timeout while firmware download uses the configured `http_timeout_ms`. This plan introduces explicit, separate transport settings in `OtaConfig` to provide a consistent and flexible API.

## Proposed Changes

### 1. Update Configuration Model (`include/ota_types.hpp`)
- Introduce `TransportConfig` struct to group transport-related settings.
- Update `OtaConfig` to use `TransportConfig` instead of the single `http_timeout_ms`.

```cpp
struct TransportConfig {
    uint32_t manifest_timeout_ms; /**< Timeout for manifest fetching (ms) */
    uint32_t firmware_timeout_ms; /**< Timeout for firmware download (ms) */
};

struct OtaConfig {
    // ... other fields ...
    TransportConfig transport;    /**< Transport-specific settings */
    // ... other fields ...
};
```

### 2. Update HTTP Client Interface (`include/interfaces/i_http_client.hpp`)
- Update `IHttpClient::fetch` to accept `timeout_ms`.

```cpp
virtual esp_err_t fetch(const std::string& url, std::string& output_content, uint32_t timeout_ms) = 0;
```

### 3. Update HTTP Client Implementation (`src/http_client.cpp`)
- Update `HttpClient::fetch` to use the provided `timeout_ms`.

### 4. Update OTA Manager Orchestration (`src/ota_manager.cpp`)
- In `handle_manifest_state`: pass `config_.transport.manifest_timeout_ms` to `http_client.fetch()`.
- In `handle_download_state`: use `config_.transport.firmware_timeout_ms` when initializing the OTA session.

### 5. Update Documentation and Examples
- **`API.md`**: Update `OtaConfig` table and descriptions.
- **`README.md`**: Update usage examples.
- **Examples**: Update `examples/scenarios/` to initialize the new config structure.
- **Tests**: Update host-based tests and on-target test apps to use the new configuration.

## Verification Plan

### Automated Tests
- **Host-based Unit Tests**:
  - Update `test_ota_manager.cpp` to verify that `OtaManager` passes the correct timeout values to the `HttpClient` and `OtaSession` mocks.
  - Run `idf.py build` in `host_test/test_ota_manager`.
- **On-Target Build**:
  - Run `idf.py build` in `test_apps/test_build` to ensure compilation across different configurations.

### Manual Verification (Optional)
- Set a very low `manifest_timeout_ms` (e.g., 10ms) and verify that `OtaManager` fails with a timeout error during the `MANIFEST_FETCH` state.
- Set a normal timeout and verify successful operation.
