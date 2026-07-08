# Plan: Policy Enforcement Refactoring

## Objective
Address the design gap where the `firmware_url` from the manifest is trusted without validation. This plan introduces explicit policy gates for both `manifest_url` and `firmware_url`, ensuring they align with configured security and domain rules.

## Proposed Changes

### 1. Define Policy in Configuration (`include/ota_types.hpp`)
- Introduce a `PolicyConfig` structure in `OtaConfig`.
- Add fields for scheme policy (e.g., `allow_http`) and potentially domain/prefix restrictions.

```cpp
struct PolicyConfig {
    bool allow_http;           /**< Whether to allow insecure HTTP connections */
    // Future: std::vector<std::string> allowed_domains;
};

struct OtaConfig {
    // ...
    PolicyConfig policy;
    // ...
};
```

### 2. Implement URL Validator (`src/url_validator.hpp/cpp` or internal to `OtaManager`)
- Create a utility to validate URLs against the `PolicyConfig`.
- Check for supported schemes (HTTP/HTTPS) and enforce `allow_http` if set to `false`.

### 3. Apply Validation in `OtaManager` (`src/ota_manager.cpp`)
- **Manifest Fetch Gate**: Validate `config_.manifest_url` in `handle_manifest_state()` before calling `http_client.fetch()`.
- **Firmware Download Gate**: Validate `manifest_.firmware_url` after successful manifest parsing (either at the end of `handle_manifest_state()` or at the start of `handle_download_state()`).

### 4. Update Documentation and Examples
- **`API.md`**: Document the new `PolicyConfig` and how it affects OTA operations.
- **`README.md`**: Update usage examples to include policy configuration.
- **Examples/Tests**: Update all call sites to include a default policy (e.g., `allow_http = true` for current compatibility).

## Verification Plan

### Automated Tests
- **Unit Tests**: Add tests to `test_ota_manager.cpp` to verify:
    - OTA fails if `manifest_url` uses HTTP and `allow_http` is `false`.
    - OTA fails if `firmware_url` uses HTTP and `allow_http` is `false`.
    - OTA succeeds if schemes match the policy.
- **On-Target Build**: Ensure `test_apps/test_build` compiles.

### Manual Verification
- Configure a manifest with an HTTP `firmware_url`.
- Run OTA with `allow_http = false` and verify it fails with a security violation error before attempting the download.
