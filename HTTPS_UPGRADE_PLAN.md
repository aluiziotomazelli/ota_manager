# HTTPS Upgrade Plan for OTA Manager

This document outlines the architectural requirements and incremental steps to enable secure HTTPS communication for both manifest retrieval and firmware download.

## Objective
Transition from plain HTTP to HTTPS to ensure firmware integrity and authenticity, maintaining strict separation of concerns and host-testability.

## 1. Architectural Analysis & Risks

Acting as the Architecture Guardian, the following risks must be mitigated:

- **Domain Coupling**: The orchestrator (`OtaManager`) must remain agnostic of SSL implementation details. It should only manage the "intent" of a secure update.
- **Interface Integrity**: HAL interfaces (`IHttpClient`, `IOtaSession`) must remain generic. We will avoid adding SSL-specific parameters to method calls; instead, the configuration will be injected during HAL instantiation or session initialization.
- **Host-Testability**: Host-side tests (Linux) must continue to function. Mocks must handle certificate pointers safely without attempting real SSL handshakes.

## 2. Refined Incremental Plan

### Step 1: Data Contract Update
- **Action**: Add `const char* server_cert_pem` to the `OtaConfig` struct.
- **File**: `include/ota_types.hpp`
- **Impact**: Prepares the data structure. No build breakage.
- **Verification**: Ensure successful compilation of all components.

### Step 2: Manifest HAL Refactoring (HttpClient)
- **Action**: Inject `OtaConfig` (or at minimum `server_cert_pem`) into `HttpClient` via its **constructor**. The `IHttpClient::fetch` signature must remain unchanged — SSL details are internal to the HAL implementation, not part of the interface contract.
- **File**: `include/http_client.hpp`, `src/http_client.cpp`
- **Impact**: Enables SSL validation for manifest fetching without leaking SSL concerns into the interface or the orchestrator.
- **Verification**: Since `MockHttpClient` mocks `IHttpClient` (which does not expose the certificate), SSL wiring is validated via the `test_build` on-target test or an integration test, not via the mock.

### Step 3: Download HAL Refactoring (OtaSession)
- **Action**: Inject `server_cert_pem` into `OtaSession` via its **constructor** and use it to populate `esp_https_ota_config_t::http_config.cert_pem` inside `OtaSession::begin`. The `IOtaSession::begin` signature must remain unchanged.
- **File**: `include/ota_session.hpp`, `src/ota_session.cpp`
- **Impact**: Enables SSL validation for the firmware download stream without leaking SSL concerns into the interface.
- **Verification**: SSL wiring is validated via the `test_build` on-target test. `MockOtaSession` does not change, as the interface contract is unaffected.

### Step 4: Security Policy Enforcement (OtaManager)
- **Action**: Implement purely data-driven validation in `OtaManager` to enforce security policy (e.g., log a warning if `config.server_cert_pem == nullptr` while the manifest URL uses `https://`). **No SSL headers or ESP-IDF SSL APIs may be included in `ota_manager.cpp`** — this logic must rely exclusively on `OtaConfig` fields.
- **File**: `src/ota_manager.cpp`
- **Impact**: Centralizes security business rules in the orchestrator without violating the HAL boundary.

### Step 5: Resource Management Documentation
- **Action**: Add a guide on embedding certificates using `COMPONENT_EMBED_TXTFILES` in `CMakeLists.txt`.
- **File**: `README.md`

## 3. Security Considerations

- **Insecure Bypass**: Support for `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP` will be maintained for local development, but a prominent log warning will be issued by the orchestrator if utilized.
- **Certificate Lifecycle**: The component assumes the certificate string remains valid in memory for the duration of the OTA task.

---
*Status: Approved for Incremental Implementation*
*Date: May 2026*
