# Fixed Issues History

This document consolidates the design and implementation plans for all technical issues resolved in the `ota_manager` component during the pre-HTTPS preparatory phase. Each entry describes the original problem, the proposed solution, and the final resolution.

The original detailed plans have been moved to `todo/` for reference.

---

## 1. Unsafe Worker Shutdown and Resource Lifetime

**Original file:** `SHUTDOWN_LIFECYCLE_FIX_PLAN.md`  
**Resolved by:** Commits `3bf5b9f`, `f06f7ba`

### Problem

The OTA worker task could be running while `deinit()` deleted its task handle and synchronization primitives, leading to potential use-after-free or race conditions during shutdown. The implementation mixed two incompatible models: cooperative worker shutdown through notification bits and external forced destruction from `deinit()`.

### Solution

A coordinated shutdown model was implemented:

- `deinit()` returns `bool` instead of `void` — timeout no longer silently degrades into unsafe cleanup.
- `deinit()` sends `OTA_STOP_BIT`, waits on `shutdown_done_` semaphore, and only releases resources after confirmed worker exit.
- The worker self-deletes via `vTaskDelete(nullptr)`, clearing `ota_task_handle_` while holding the mutex before signaling completion.
- `cancel_ota()` remains lightweight: aborts the session, sends `OTA_CANCEL_BIT`, and returns to `IDLE` without destroying infrastructure.
- Repeated `deinit()` calls are idempotent.

---

## 2. Inconsistent Transport Configuration

**Original file:** `FIX-ISSUE-TRANSPORT-CONFIG.md`  
**Resolved by:** Commit `7b7cf40`

### Problem

The component exposed a single `http_timeout_ms` in `OtaConfig`, but only the firmware download path used it. Manifest fetching used a hardcoded 5000 ms timeout inside `HttpClient`.

### Solution

- Introduced `TransportConfig` struct with `manifest_timeout_ms` and `firmware_timeout_ms`.
- Updated `IHttpClient::fetch()` to accept an explicit `timeout_ms` parameter.
- `OtaManager` passes the appropriate timeout from its `config_.transport` at call time.
- All examples, tests, and documentation updated to use the new configuration.

---

## 3. Partial Security Policy Enforcement

**Original file:** `FIX-ISSUE-POLICY-ENFORCEMENT.md`  
**Resolved by:** Commit `27b79eb`

### Problem

The component trusted the firmware URL embedded in the manifest without applying independent transport policy checks. There were no runtime gates for URL scheme validation.

### Solution

- Added `OtaSecurityConfig` to `OtaConfig` with an `allow_http_during_development` flag (default: `false`).
- Implemented `validate_url()` in `OtaManager`, applied at two network boundaries:
  1. Manifest URL — validated before `http_client.fetch()`.
  2. Firmware URL — validated before `ota_session.begin()`.
- Rejected URLs produce a clear log message (`Security policy violation: insecure URL not allowed`) and return `OtaStepResult::FAILED`.
- Policy decisions remain in the orchestration layer, not in transport implementations.

---

## 4. Transport Configuration Ownership Mismatch

**Original file:** `FIX-ISSUE-TRANSPORT-OWNERSHIP.md`  
**Resolved by:** Commit `9b28aee`

### Problem

`OtaConfig` was provided to `OtaManager::init()`, but concrete transport objects were constructed earlier by the application, creating an ownership mismatch for configuration settings like timeouts and security policies.

### Solution

- Consolidated configuration ownership within `OtaManager`.
- `OtaManager` now acts as the single source of truth, deriving and pushing parameters to dependencies at call time.
- Dependencies (`HttpClient`, `OtaSession`) remain stateless regarding OTA-specific configuration until the moment of use.

---

## 5. IOtaSession Not Transport-Agnostic

**Original file:** `FIX-ISSUE-OTA-SESSION-ABSTRACTION.md`  
**Resolved by:** Commit `fa7ff0a`

### Problem

The `IOtaSession` interface leaked ESP-IDF transport details by exposing `esp_http_client_config_t` in its `begin()` method, conflicting with the goal of providing a domain-level OTA abstraction.

### Solution

- Introduced `OtaDownloadRequest` as a domain-level structure encapsulating `url` and `timeout_ms`.
- Changed `IOtaSession::begin()` to accept `const OtaDownloadRequest&`.
- The concrete `OtaSession` maps the domain request to the internal `esp_http_client_config_t` required by `esp_https_ota_begin()`.
- Mocks and tests updated accordingly.

---

## 6. Unchecked Optional in Version Validation

**Original file:** `FIX-ISSUE-VERSION-VALIDATION.md`  
**Resolved by:** Commit `ddb85d5`

### Problem

In `OtaManager::handle_download_state()`, the code parsed the running version and the new image version, then called `.value()` without checking that parsing succeeded. A malformed version string could cause a runtime crash.

### Solution

- Updated `handle_download_state()` to explicitly validate both `current_v_opt` and `new_v_opt` before comparing them.
- A failure to parse either version string now results in a graceful return of `OtaStepResult::FAILED`.
- The version parser (`VersionHelper::parse`) was also hardened against git metadata suffixes (e.g., `1.0.0-dirty`).

---

## 7. Race Condition in start_ota() (Code Review BUG-01)

**No separate plan file — resolved during code review**  
**Resolved by:** Commit `f06f7ba`

### Problem

`ota_task_handle_` was written by `create_task()` without holding the mutex, and read by `notify_task()` also without the mutex. A concurrent `deinit()` could delete the handle while it was being used.

### Solution

Task creation and notification are now performed while holding `state_mutex_`. The notificação is sent before releasing the mutex, garantindo que o handle está válido.

---

## 8. mbedTLS Integrity Issues (Code Review BUG-02, AVISO-01)

**No separate plan file — resolved during code review**  
**Resolved by:** Commit `f06f7ba`

### Problem

- Return values of `mbedtls_sha256_starts`, `mbedtls_sha256_update`, and `mbedtls_sha256_finish` were ignored. In mbedTLS 3.x (ESP-IDF v5.x), these return `int` error codes, and ignoring them could produce an invalid hash silently.
- If `esp_partition_read` failed mid-loop, `mbedtls_sha256_finish` was still called, producing a partial hash.

### Solution

- All mbedTLS return values are now checked. Failures propagate as `ESP_FAIL`.
- `mbedtls_sha256_finish` is only called if `err == ESP_OK`, preventing partial hash output.
- Updated `System::get_partition_sha256()` implementation accordingly.

---

## 9. Status Read Without Mutex (Code Review AVISO-02)

**No separate plan file — resolved during code review**  
**Resolved by:** Commit `f06f7ba`

### Problem

The worker task's wait-time calculation read `status_` directly without acquiring the mutex. Although the worker is the primary writer, a concurrent `cancel_ota()` could modify the status from another thread.

### Solution

The wait-time calculation now uses `get_status()` (which acquires the mutex) instead of reading `status_` directly.
