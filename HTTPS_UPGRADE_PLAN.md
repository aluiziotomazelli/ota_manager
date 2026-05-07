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
- **Action**: Update `HttpClient::fetch` to populate `esp_http_client_config_t::cert_pem` from `OtaConfig`.
- **File**: `src/http_client.cpp`
- **Impact**: Enables SSL validation for manifest fetching.
- **Verification**: Update `MockHttpClient` to verify the certificate is passed correctly.

### Step 3: Download HAL Refactoring (OtaSession)
- **Action**: Update `OtaSession::begin` to pass the certificate through to the `esp_https_ota_config_t`.
- **File**: `src/ota_session.cpp`
- **Impact**: Enables SSL validation for the firmware download stream.
- **Verification**: Update `MockOtaSession`.

### Step 4: Security Policy Enforcement (OtaManager)
- **Action**: Implement logic in `OtaManager` to validate the security context (e.g., warning if HTTPS is used without a certificate when insecure mode is disabled via SDKConfig).
- **File**: `src/ota_manager.cpp`
- **Impact**: Centralizes security business rules in the orchestrator.

### Step 5: Resource Management Documentation
- **Action**: Add a guide on embedding certificates using `COMPONENT_EMBED_TXTFILES` in `CMakeLists.txt`.
- **File**: `README.md`

## 3. Security Considerations

- **Insecure Bypass**: Support for `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP` will be maintained for local development, but a prominent log warning will be issued by the orchestrator if utilized.
- **Certificate Lifecycle**: The component assumes the certificate string remains valid in memory for the duration of the OTA task.

---
*Status: Approved for Incremental Implementation*
*Date: May 2026*
