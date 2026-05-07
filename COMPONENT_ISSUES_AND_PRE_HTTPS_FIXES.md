# OTA Manager Existing Issues and Pre-HTTPS Fixes

This document lists the most important issues currently present in the component that should be fixed before the HTTPS migration. These are not HTTPS-specific problems. They already affect correctness, robustness, or API clarity in the current HTTP-only design.

The list is ordered by implementation risk and operational impact.

## 1. Unsafe Worker Shutdown and Resource Lifetime

### Problem

The OTA worker task can still be running while `deinit()` deletes its task handle and synchronization primitives.

Current behavior:

- `deinit()` calls `abort()`, notifies `OTA_STOP_BIT`, waits up to 1 second, then proceeds with cleanup even on timeout.
- The worker task still uses `state_mutex_`, `shutdown_done_`, and `ota_task_handle_` during its exit path.
- If the worker is slow to stop, the component may delete resources that are still in use.

This is a classic ownership and shutdown ordering bug. HTTPS will make it worse because TLS teardown and network timeouts can increase stop latency.

### Why It Matters

- Possible use-after-free on FreeRTOS synchronization objects.
- Possible deletion of a task that is still executing.
- Nondeterministic behavior during `deinit()`, `cancel_ota()`, or error recovery.

### Most Efficient Fix

Make the worker task the sole owner of its own termination, and make `deinit()` a coordinated join-like shutdown.

Recommended changes:

1. `deinit()` requests stop and waits for a definitive worker exit signal.
2. The worker exits its loop, clears `ota_task_handle_`, signals shutdown completion, and self-deletes.
3. `deinit()` must not call `delete_task()` on a worker that may still be alive.
4. `deinit()` must not delete `state_mutex_` or `shutdown_done_` until worker exit is confirmed.
5. If a timeout is kept, return a failure status or leave the component in a safe partially-initialized state instead of force-deleting resources.

### Preferred Outcome

- One clear owner for task termination.
- No forced cleanup of resources still visible to another thread.
- Deterministic `deinit()` semantics.

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

### Problem

The component currently relies on `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y` as a practical requirement for normal operation. That is fine for local development, but weak as the long-term behavior model.

### Why It Matters

- Environment policy is encoded mostly in build flags.
- Runtime intent is not expressed clearly by the API.
- It becomes harder to support both secure production and simple local testing cleanly.

### Most Efficient Fix

Move the policy decision into component configuration and let build flags act only as lower-level capability switches.

Recommended direction:

1. Add an explicit runtime policy such as `allow_http_during_development`.
2. Reject HTTP URLs when the policy is disabled.
3. Keep `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP` only as an implementation prerequisite for dev mode.

### Preferred Outcome

- Secure-by-default runtime behavior.
- Simple, explicit local development opt-in.

## 8. On-Target Validation Is Too Thin for Transport Evolution

### Problem

Host tests cover orchestration well, but they do not validate actual transport wiring, TLS behavior, or interaction with ESP-IDF HTTPS OTA internals.

### Why It Matters

- HTTPS regressions will not be caught by current mocks.
- Build-only checks are not enough for certificate handling, redirect behavior, and HTTP dev fallback.

### Most Efficient Fix

Add a focused on-target integration matrix before and during the HTTPS migration.

Recommended minimum scenarios:

1. HTTPS manifest + HTTPS firmware with pinned PEM.
2. HTTPS manifest + HTTPS firmware with CRT bundle.
3. HTTP manifest + HTTP firmware in explicit dev mode.
4. HTTPS manifest that points to HTTP firmware when dev mode is disabled.
5. Missing or invalid certificate configuration.

### Preferred Outcome

- Real confidence in transport behavior, not just orchestration behavior.

## Suggested Execution Order

The most efficient order before the HTTPS migration is:

1. Fix worker shutdown and resource lifetime.
2. Redesign transport configuration ownership.
3. Split and unify manifest/download transport settings.
4. Add explicit runtime transport policy checks for both URLs.
5. Replace ESP-IDF transport types in public interfaces with domain request structs.
6. Fix the unchecked version parsing path.
7. Expand on-target integration coverage.

## What Should Be Fixed Before HTTPS Starts

The following should be considered blocking prerequisites:

- Worker shutdown and resource lifetime.
- Transport configuration ownership.
- Policy enforcement at both network boundaries.
- On-target integration coverage for transport behavior.

Everything else can be done in the same preparatory phase, but these four items have the highest leverage and risk reduction.
