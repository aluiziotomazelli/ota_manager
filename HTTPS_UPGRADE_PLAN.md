# HTTPS Upgrade Plan for OTA Manager

This document outlines the steps required to enable secure HTTPS communication for both manifest retrieval and firmware download in the `ota_manager` component.

## Objective
Transition from plain HTTP to HTTPS to ensure firmware integrity and authenticity during the update process, while maintaining backward compatibility for local development.

## 1. Architectural Changes

### 1.1 Configuration Update (`OtaConfig`)
Add an optional field for the Server Root CA certificate.
- **File:** `include/ota_types.hpp`
- **Change:** Add `const char* server_cert_pem;` to `OtaConfig` struct.
- **Impact:** Allows the application to provide the certificate required for SSL validation.

### 1.2 HTTP Client Enhancement (`HttpClient`)
Update the manifest fetcher to handle secure connections.
- **File:** `src/http_client.cpp`
- **Change:** Pass `config.server_cert_pem` to the `esp_http_client_config_t` structure.
- **Logic:** If `server_cert_pem` is provided, the client will perform certificate validation. If NULL, it stays in insecure mode (if allowed by sdkconfig).

### 1.3 OTA Session Enhancement (`OtaSession`)
Update the firmware downloader to utilize the certificate.
- **File:** `src/ota_session.cpp`
- **Change:** repass the certificate from the `esp_http_client_config_t` to the `esp_https_ota_begin` call.

## 2. Security Considerations

### 2.1 Mutual Exclusivity
- When `server_cert_pem` is provided, HTTPS should be enforced.
- For local testing, we should allow the certificate to be `nullptr` only if `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP` is enabled in the SDK configuration.

### 2.2 Certificate Management
- **Embedded Certificates:** Instructions should be provided on how to use `COMPONENT_EMBED_TXTFILES` in `CMakeLists.txt` to include `.pem` files directly in the binary.
- **Certificate Bundles:** Consider supporting the ESP-IDF Certificate Bundle for simpler management of well-known CAs (like GitHub/AWS).

## 3. Implementation Steps

1.  **Modify Interfaces:** Update `IHttpClient` and `IOtaSession` if needed to ensure the certificate context is preserved.
2.  **Update Config Struct:** Add the `server_cert_pem` field.
3.  **Refactor HALs:** Update `HttpClient` and `OtaSession` implementations.
4.  **Update Mocks:** Adjust host-side tests to account for the new configuration field.
5.  **Documentation:** Update `README.md` and `API.md` with HTTPS setup instructions.

## 4. Local Testing Compatibility
To keep local development simple:
- The component will still support `http://` URLs.
- If the URL is `https://` and no certificate is provided, the connection should fail unless explicitly bypassed (not recommended for production).

---
*Date: May 2026*
