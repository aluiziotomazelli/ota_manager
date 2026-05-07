# OTA Manager Existing Issues and Pre-HTTPS Fixes

This document lists the most important issues currently present in the component that should be fixed before the HTTPS migration. These are not HTTPS-specific problems. They already affect correctness, robustness, or API clarity in the current HTTP-only design.

The list is ordered by implementation risk and operational impact.

## 1. Unsafe Worker Shutdown and Resource Lifetime

**Status:** Resolved (Verified)

### Problem (Historical)

The OTA worker task could be running while `deinit()` deleted its task handle and synchronization primitives, leading to potential use-after-free or race conditions during shutdown.

### Resolution

The component was refactored to implement a coordinated shutdown. The `deinit()` method now requests a stop and waits for a definitive signal (`shutdown_done_`) from the worker. The worker manages its own termination path, signals completion, and self-deletes, ensuring that synchronization primitives are not deleted while in use.


## 2. Inconsistent Transport Configuration Between Manifest and Firmware Download

**Status:** Resolved (Commit 7b7cf40)

### Problem (Historical)

The component previously exposed a single `http_timeout_ms` in `OtaConfig`, but only the firmware download path used it. Manifest fetching used a hardcoded 5000 ms timeout inside `HttpClient`.

### Resolution

The component now uses explicit `TransportConfig` (containing `manifest_timeout_ms` and `firmware_timeout_ms`) within `OtaConfig`. `HttpClient::fetch` now accepts an explicit timeout parameter, ensuring consistent and configurable transport behavior for all network operations.


## 3. Policy Is Enforced Only Partially by the Current OTA Flow

**Status:** Resolved (Commit 27b79eb)

### Problem (Historical)

The component previously trusted the firmware URL embedded in the manifest without applying independent transport policy checks, and domain rules were not explicitly enforced at both the manifest and firmware network boundaries.

### Resolution

Added `OtaSecurityConfig` to `OtaConfig` with an explicit `allow_http_during_development` flag. `OtaManager` now performs independent URL validation (ensuring HTTPS or adherence to development policy) for both the manifest fetch and the firmware download phases.


## 4. Transport and TLS Configuration Ownership Is Not Well Defined

**Status:** Resolved (Commit 9b28aee)

### Problem (Historical)

`OtaConfig` was provided to `OtaManager::init()`, but concrete transport objects were constructed earlier by the application, creating an ownership mismatch for configuration settings like timeouts and security policies.

### Resolution

Consolidated configuration ownership within `OtaManager`. `OtaManager` now acts as the single source of truth for transport/security settings, deriving and pushing the necessary `esp_http_client_config_t` configurations to its dependencies at the time of execution.


## 5. `IOtaSession` Is Not Actually Transport-Agnostic

**Status:** Resolved (Commit 9b28aee)

### Problem (Historical)

The `IOtaSession` interface leaked ESP-IDF transport details by exposing `esp_http_client_config_t` in its `begin()` method, conflicting with the goal of providing a domain-level OTA abstraction.

### Resolution

Introduced `OtaDownloadRequest` as a domain-level structure to encapsulate download parameters. `IOtaSession` now accepts this structure, decoupling the orchestration layer from underlying transport types and localizing ESP-IDF dependencies within the concrete `OtaSession` implementation.


## 6. Version Validation Has an Unchecked Optional Value

**Status:** Resolved (Commit ddb85d5)

### Problem (Historical)

During download setup, the code parsed the running version and then called `.value()` without checking that parsing succeeded. A malformed running version string could lead to an unexpected runtime failure.

### Resolution

Updated `OtaManager::handle_download_state()` to explicitly validate both `current_v_opt` and `new_v_opt` before comparing them. This ensures that a failure to parse either version string results in a graceful return of `OtaStepResult::FAILED`.


## 7. HTTP-Only Development Support Is Tied Too Closely to Build Configuration

**Status:** Resolved (Commit 27b79eb)

### Problem (Historical)

The component previously relied on `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y` as the primary gate for HTTP support. Runtime intent was not expressed clearly by the API, making it hard to differentiate between secure production and local testing.

### Resolution

Moved the policy decision into `OtaSecurityConfig`. The new `allow_http_during_development` flag provides a clear runtime opt-in for insecure connections, ensuring secure-by-default behavior while maintaining simple local development support.


## 8. On-Target Validation Is Too Thin for Transport Evolution

**Status:** Partially Addressed

### Current Coverage

The repository now includes on-target scenario coverage for the HTTP-only phase:

- `v1.1.0_success` validates the normal HTTP development flow.
- `v1.2.0_failure` validates rollback after a bad firmware boots and is not confirmed.
- `v1.3.0_security_failure` validates that strict security policy rejects an HTTP manifest URL immediately.

This is enough for the current HTTP-only phase because it exercises the runtime policy boundary without introducing HTTPS-specific complexity too early.

### What Still Needs to Wait for HTTPS

Host tests still cannot validate:

- actual TLS handshake behavior
- pinned certificate verification
- certificate bundle configuration
- redirect and trust-chain behavior under real transport conditions

### Future HTTPS Matrix

When HTTPS migration starts, add a focused on-target matrix with:

1. HTTPS manifest + HTTPS firmware with pinned PEM.
2. HTTPS manifest + HTTPS firmware with CRT bundle.
3. HTTP manifest + HTTP firmware in explicit dev mode.
4. HTTPS manifest that points to HTTP firmware when dev mode is disabled.
5. Missing or invalid certificate configuration.

### Preferred Outcome

- Keep the current HTTP-only scenario set focused and useful now.
- Add HTTPS transport coverage only when it becomes relevant.

## Suggested Execution Order

The most efficient order before the HTTPS migration is:

1. Fix worker shutdown and resource lifetime. (DONE)
2. Keep the current HTTP-only scenario coverage stable.
3. Expand on-target HTTPS integration coverage when HTTPS work starts. (TODO)

Resolved items:
- Redesign transport configuration ownership. (Resolved)
- Split and unify manifest/download transport settings. (Resolved)
- Add explicit runtime transport policy checks for both URLs. (Resolved)
- Replace ESP-IDF transport types in public interfaces with domain request structs. (Resolved)
- Fix the unchecked version parsing path. (Resolved)

## What Should Be Fixed Before HTTPS Starts

The following should still be considered blocking prerequisites:

- Worker shutdown and resource lifetime.
- On-target integration coverage for transport behavior.

Everything else has been resolved in the preparatory phase, significantly reducing risk before starting the HTTPS migration.
