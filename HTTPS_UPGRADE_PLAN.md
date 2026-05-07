# HTTPS Upgrade Plan for OTA Manager

This document defines the migration from the current HTTP-only OTA flow to a secure-by-default HTTPS design while preserving a simple and explicit HTTP mode for local development.

This plan assumes the pre-migration fixes described in `COMPONENT_ISSUES_AND_PRE_HTTPS_FIXES.md` are completed first, especially:

- safe worker shutdown and resource lifetime
- clearer transport configuration ownership
- policy checks at both network boundaries
- stronger on-target transport validation

## Objective

Enable secure HTTPS communication for both manifest retrieval and firmware download with the following goals:

- secure by default
- explicit opt-in for local HTTP development
- compatibility with GitHub Releases assets as a production delivery target
- no TLS policy leakage into orchestration logic
- preserved host-testability for OTA decision logic

## 1. Target Behavior

The target component behavior is:

- `https://` is the default and recommended scheme for both manifest and firmware.
- `http://` remains supported only when explicitly enabled by runtime configuration and when the ESP-IDF build also allows it.
- `OtaManager` enforces transport policy using data only. It must not contain TLS-specific ESP-IDF API logic.
- Concrete transport implementations own the low-level ESP-IDF HTTPS wiring.
- The application can choose one of two certificate strategies:
  - pinned PEM certificate
  - ESP x509 certificate bundle

## 2. Architectural Direction

The current codebase already exposes ESP-IDF transport types in public interfaces. Since API changes are acceptable, the migration should improve the boundary instead of preserving it.

### Recommended Public Model

Replace low-level transport parameters in public interfaces with domain request/configuration structures.

Suggested direction:

- `ManifestFetchRequest`
- `FirmwareDownloadRequest`
- `OtaSecurityConfig`
- `OtaTransportConfig`

This allows:

- one source of truth for scheme policy and certificate strategy
- no duplicated TLS configuration between `OtaManager`, `HttpClient`, and `OtaSession`
- easier testing and documentation

## 3. Proposed Configuration Model

Instead of adding only `server_cert_pem`, introduce explicit transport and security configuration.

Example direction:

```cpp
enum class OtaCertificateMode
{
    NONE,
    PINNED_PEM,
    CRT_BUNDLE
};

struct OtaSecurityConfig
{
    bool allow_http_during_development = false;
    OtaCertificateMode certificate_mode = OtaCertificateMode::NONE;
    const char* server_cert_pem = nullptr;
};

struct OtaTransportConfig
{
    uint32_t manifest_timeout_ms = 10000;
    uint32_t firmware_timeout_ms = 30000;
};

struct OtaConfig
{
    std::string device_type;
    std::string manifest_url;
    uint32_t task_stack_size;
    uint8_t task_priority;
    bool allow_same_version;
    bool restart_on_success;
    OtaTransportConfig transport;
    OtaSecurityConfig security;
};
```

Notes:

- `allow_http_during_development` controls runtime policy.
- `certificate_mode` controls how HTTPS server verification is configured.
- `server_cert_pem` is used only for `PINNED_PEM`.
- `CRT_BUNDLE` is the preferred production mode for public services backed by widely trusted CAs.

## 4. Certificate Options

The component should support two explicit HTTPS verification modes.

### Option A: Pinned PEM Certificate

Best for:

- local HTTPS server
- controlled infrastructure
- self-signed or privately issued certificates
- environments where pinning a specific certificate or CA is desirable

Implementation idea:

- embed the PEM file with `COMPONENT_EMBED_TXTFILES`
- expose linker symbols
- assign the PEM pointer to transport configuration

Example:

```cmake
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${component_requires}
    PRIV_REQUIRES json
    EMBED_TXTFILES "certs/local_ca.pem"
)
```

```cpp
extern const char local_ca_pem_start[] asm("_binary_local_ca_pem_start");

OtaConfig config = {
    .device_type = "app_hub",
    .manifest_url = "https://ota.local/manifests/app_hub.json",
    .task_stack_size = 8192,
    .task_priority = 5,
    .allow_same_version = false,
    .restart_on_success = true,
    .transport = {
        .manifest_timeout_ms = 10000,
        .firmware_timeout_ms = 30000,
    },
    .security = {
        .allow_http_during_development = false,
        .certificate_mode = OtaCertificateMode::PINNED_PEM,
        .server_cert_pem = local_ca_pem_start,
    },
};
```

### Option B: ESP x509 Certificate Bundle

Best for:

- GitHub Releases assets
- public CDNs
- servers using standard public certificate chains
- production environments where CA rotation should not require firmware changes as often

Implementation idea:

- enable ESP-IDF certificate bundle support
- use `crt_bundle_attach` in the HTTP client configuration
- do not set `server_cert_pem` in this mode

Example:

```cpp
OtaConfig config = {
    .device_type = "app_hub",
    .manifest_url = "https://github.example/manifests/app_hub.json",
    .task_stack_size = 8192,
    .task_priority = 5,
    .allow_same_version = false,
    .restart_on_success = true,
    .transport = {
        .manifest_timeout_ms = 10000,
        .firmware_timeout_ms = 30000,
    },
    .security = {
        .allow_http_during_development = false,
        .certificate_mode = OtaCertificateMode::CRT_BUNDLE,
        .server_cert_pem = nullptr,
    },
};
```

Operational recommendation:

- use `PINNED_PEM` for local HTTPS development if needed
- use `CRT_BUNDLE` for GitHub Releases in production

## 5. Incremental Migration Plan

### Step 0: Complete the Pre-HTTPS Fixes

Action:

- finish the preparatory fixes from `COMPONENT_ISSUES_AND_PRE_HTTPS_FIXES.md`

Why first:

- HTTPS increases network latency, handshake cost, and teardown complexity
- existing lifecycle issues must not be debugged at the same time as TLS changes

Verification:

- host tests still pass
- on-target test app builds
- worker shutdown behavior is deterministic

### Step 1: Redesign the Public Transport/Security Contract

Action:

- replace the current single `http_timeout_ms` and add explicit security configuration
- define transport request structs or equivalent data carriers
- remove ambiguity about who owns TLS configuration

Files likely impacted:

- `include/ota_types.hpp`
- `include/interfaces/i_http_client.hpp`
- `include/interfaces/i_ota_session.hpp`
- `include/ota_manager.hpp`

Impact:

- public API breakage is expected
- this is acceptable and desirable at this stage

Verification:

- all examples and test apps compile with the new API

### Step 2: Add Data-Driven Policy Checks in `OtaManager`

Action:

- validate `config.manifest_url` before manifest fetch
- validate `manifest.firmware_url` after manifest parsing
- reject insecure schemes unless `allow_http_during_development` is enabled
- log a prominent warning when HTTP dev mode is used
- reject HTTPS when certificate mode is invalid for the selected transport

Rules:

- `OtaManager` must use string and config checks only
- no TLS-specific ESP-IDF headers or SSL APIs in `ota_manager.cpp`

Impact:

- centralizes update policy
- prevents mixed-scheme surprises

Verification:

- host tests cover scheme acceptance and rejection cases

### Step 3: Refactor Manifest Transport (`HttpClient`)

Action:

- update `HttpClient` to accept a domain request or equivalent transport config
- map certificate mode to ESP-IDF HTTP client settings
- use manifest-specific timeout from configuration

Implementation details:

- HTTP dev mode:
  - plain `http://` URL is allowed only if runtime policy allows it
- HTTPS with PEM:
  - set `esp_http_client_config_t::cert_pem`
- HTTPS with CRT bundle:
  - set `esp_http_client_config_t::crt_bundle_attach`

Files likely impacted:

- `include/http_client.hpp`
- `src/http_client.cpp`

Verification:

- on-target test for HTTPS manifest fetch with PEM
- on-target test for HTTPS manifest fetch with CRT bundle
- on-target test for HTTP dev mode

### Step 4: Refactor Firmware Download Transport (`OtaSession`)

Action:

- replace the low-level `begin(const esp_http_client_config_t*)` flow with a domain request or equivalent
- inside `OtaSession`, translate that request into `esp_https_ota_config_t`
- map certificate mode to the embedded `http_config`
- use firmware-specific timeout from configuration

Implementation details:

- HTTP dev mode:
  - allowed only when explicitly enabled and supported by ESP-IDF build configuration
- HTTPS with PEM:
  - set `ota_config.http_config->cert_pem`
- HTTPS with CRT bundle:
  - set `ota_config.http_config->crt_bundle_attach`

Files likely impacted:

- `include/ota_session.hpp`
- `src/ota_session.cpp`
- `include/interfaces/i_ota_session.hpp`

Verification:

- on-target test for HTTPS firmware download with PEM
- on-target test for HTTPS firmware download with CRT bundle
- on-target test for HTTP dev mode

### Step 5: Update Examples, Test Apps, and OTA Server Tooling

Action:

- update examples to the new API
- keep a simple local HTTP path for development
- prepare `ota_server/manage.py` for optional future HTTPS support if desired

Recommended local workflow:

1. default production config uses HTTPS
2. local development uses:
   - `allow_http_during_development = true`
   - manifest URL pointing to local `ota_server`
   - ESP-IDF build with `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y`

Optional future enhancement:

- add `--scheme http|https` and certificate options to `ota_server/manage.py`
- support generating manifests for both local HTTP and hosted HTTPS targets

Verification:

- the local server flow remains simple for iterative development

### Step 6: Expand Automated Validation

Action:

- keep host tests for OTA logic
- add on-target integration tests for transport behavior

Minimum scenarios:

1. HTTPS manifest + HTTPS firmware with `PINNED_PEM`
2. HTTPS manifest + HTTPS firmware with `CRT_BUNDLE`
3. HTTP manifest + HTTP firmware with explicit dev mode enabled
4. HTTPS manifest + HTTP firmware rejected when dev mode is disabled
5. HTTPS manifest rejected when certificate mode is invalid
6. missing certificate in `PINNED_PEM` mode rejected

Verification:

- test matrix passes before documenting the migration as complete

### Step 7: Update Public Documentation

Action:

- rewrite `README.md` and examples to reflect secure-by-default behavior
- document both certificate modes
- document explicit HTTP dev mode
- document the required ESP-IDF Kconfig flags for each scenario

## 6. Security Policy Rules

The final component behavior should follow these rules:

1. Reject `http://` manifest URLs unless dev mode is enabled.
2. Reject `http://` firmware URLs unless dev mode is enabled.
3. Reject mixed-scheme flows unless explicitly allowed by the same dev policy.
4. Reject `PINNED_PEM` mode when `server_cert_pem == nullptr`.
5. Reject `NONE` certificate mode for `https://`.
6. Warn clearly when HTTP dev mode is in use.

## 7. Local Development Mode

The goal is simple local testing without production-like ceremony.

Recommended developer flow:

1. Build firmware with `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y`.
2. Run `ota_server/manage.py deploy <firmware.bin>`.
3. Run `ota_server/manage.py serve --port 8070`.
4. Use `manifest_url = "http://<local-host>:8070/manifests/<device>.json"`.
5. Set `allow_http_during_development = true`.

This keeps local OTA fast and simple while preserving secure production defaults.

## 8. GitHub Releases / CI-CD Addendum

The repository already has CI workflows for build and host tests:

- `.github/workflows/build.yml`
- `.github/workflows/host_test.yml`

For production OTA delivery through GitHub Releases assets, the missing part is CD rather than CI.

### Recommended Delivery Model

Use GitHub Actions to publish:

1. firmware binary asset
2. generated manifest asset
3. release metadata tied to a version tag

Recommended production URLs:

- manifest hosted on a stable HTTPS endpoint
- firmware binary hosted as a GitHub Release asset URL

Reasoning:

- the device needs a stable manifest URL
- the manifest can then point to the versioned binary asset
- GitHub Release assets are naturally versioned and immutable enough for OTA usage

### Recommended CD Flow

1. CI builds the firmware on tag creation.
2. A release workflow creates or updates a GitHub Release for that tag.
3. The workflow uploads the firmware `.bin` as a release asset.
4. The workflow generates `manifest.json` with:
   - `device_type`
   - `version`
   - `sha256_hex`
   - `firmware_size`
   - `firmware_url`
5. The workflow publishes the manifest to a stable location.

### Stable Manifest Hosting Options

Recommended options:

- GitHub Pages with one manifest per device type
- a dedicated HTTPS static bucket or site
- a small release-index repository or branch published as static content

Do not rely on a tag-specific manifest URL as the device entrypoint. The device should keep pointing to one stable manifest URL per device type.

### Certificate Strategy for GitHub

Recommended for GitHub Releases:

- use `CRT_BUNDLE`

Reasoning:

- GitHub uses publicly trusted certificates
- certificate chains may rotate
- a bundle-based strategy avoids frequent firmware updates just to refresh a pinned certificate

### Suggested Future Workflow Additions

Potential new workflows:

1. `release.yml`
   - trigger on version tags
   - build firmware
   - upload release assets
   - generate manifest
2. `manifest_publish.yml`
   - publish stable manifests to GitHub Pages or another static target

### Operational Note

If the manifest itself is also hosted on GitHub Pages or another public HTTPS endpoint, the same `CRT_BUNDLE` strategy can verify both manifest and firmware retrieval, keeping production configuration simpler.

---

Status: revised for secure-by-default migration
Date: May 2026
