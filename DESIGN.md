# OTA Manager Design Document

## Overview
The `ota_manager` is a passive, dependency-injected OTA update component for ESP-IDF. It is designed to be highly testable, thread-safe, and reactive. The component follows an **Active Object + Pure Iterative FSM** architecture, isolating core orchestration logic from platform-specific APIs via domain-oriented HAL interfaces.

## Architectural Decisions

### 1. Domain-Oriented HAL (Hardware Abstraction Layer)
To ensure the orchestration logic remains testable on host platforms and decoupled from the ESP-IDF SDK, we adopted a domain-oriented HAL strategy rather than a header-based one.

- **Interfaces:** Each HAL represents a single business domain responsibility:
  - `IHttpClient` — fetching remote resources (manifest download)
  - `IManifestParser` — parsing JSON manifest files into `OtaManifest` structures
  - `IOtaSession` — managing the firmware download session (begin, perform, finish, abort)
  - `ISystem` — accessing partition info, running app description, SHA-256 calculation
  - `ITaskScheduler` — abstracting FreeRTOS task creation, notifications, semaphores, and mutexes
  - `IRollbackManager` — querying and controlling OTA rollback state (pending verify, mark valid, rollback)

- **Low-Level Transparency:** HAL interfaces preserve native ESP-IDF types where appropriate (e.g., `esp_err_t`, `esp_app_desc_t*`) to ensure the component remains technically aware of the target platform. However, **transport-specific types are not leaked**: `IOtaSession::begin()` accepts a domain-level `OtaDownloadRequest` instead of `esp_http_client_config_t`, and `IHttpClient::fetch()` accepts a plain timeout parameter instead of an ESP-IDF config struct. This keeps the orchestration layer transport-agnostic.

- **Decoupling:** Business logic (`OtaManager`) interacts only with these interfaces via the `OtaDependencies` injection struct. This allows for total isolation during host-based unit testing via Google Mock, achieving 98% code coverage.

- **Default Implementations:** Concrete HAL implementations (`HttpClient`, `OtaSession`, `System`, `TaskScheduler`, `RollbackManager`, `ManifestParser`) wrap the corresponding ESP-IDF APIs and are ready for production use.

### 2. Pure Iterative Finite State Machine (FSM)
To handle the OTA orchestration process without blocking the application or risking race conditions, we implemented a Pure Iterative FSM within a dedicated FreeRTOS worker task.

- **States (OtaStatus):**
  ```
  IDLE → MANIFEST_FETCH → VERSION_CHECK → DOWNLOADING → VERIFYING → READY_TO_RESTART
                                                                        ↕
                                                                    PENDING_VERIFY
    Any state → FAILED
  ```

- **Event-Driven:** The worker task uses `xTaskNotifyWait` to react to three notification bits:
  - `OTA_START_BIT` (0x01) — begins the OTA flow
  - `OTA_STOP_BIT` (0x02) — signals graceful shutdown
  - `OTA_CANCEL_BIT` (0x04) — aborts the current operation, returns to IDLE

- **Iterative Execution:** The download process is broken down into discrete steps. Instead of a monolithic blocking loop, the task performs a single chunk/step per iteration, returning `OtaStepResult::IN_PROGRESS` until completion. This allows the loop to remain responsive to `STOP` or `CANCEL` signals at all times.

- **State Machine Logic:** The current status is managed within a central `switch` block in `ota_task()`, ensuring clean, deterministic transitions. Each state handler (`handle_manifest_state`, `handle_version_state`, `handle_download_state`, `handle_verification_state`) returns `SUCCESS`, `FAILED`, or `IN_PROGRESS`, and the central loop advances the status accordingly.

### 3. Synchronization and Thread Safety
Managing the lifecycle of a long-running worker task is complex. We implemented the following mechanisms:

- **Notification-Based Control:** Direct FreeRTOS task notifications (`xTaskNotify` / `xTaskNotifyWait`) replace boolean flags and mutex-heavy cancellation checks. This provides lightweight, real-time signaling with minimal overhead.

- **Resource Protection:** A `state_mutex_` (FreeRTOS mutex semaphore) ensures atomic reads and writes of `status_` and `ota_task_handle_`. All public-facing getters (`get_status()`) and setters (`set_status()`) acquire this mutex. The worker task creation and notification in `start_ota()` are performed while holding the mutex to prevent race conditions with `deinit()`.

- **Graceful Shutdown:** A binary semaphore (`shutdown_done_`) coordinates the handoff between `deinit()` and the worker task:
  1. `deinit()` sends `OTA_STOP_BIT` and waits on `shutdown_done_` (with a timeout).
  2. The worker task exits its loop, calls `finalize_worker_shutdown()` which:
     - Acquires `state_mutex_`, clears `ota_task_handle_`, releases mutex
     - Signals `shutdown_done_`
     - Self-deletes via `vTaskDelete(nullptr)`
  3. `deinit()` only deletes synchronization primitives after confirming worker exit.
  4. If the shutdown times out, `deinit()` returns `false` and preserves all resources — no forced cleanup is performed.

- **`deinit()` Return Value:** Changed from `void` to `bool` to communicate shutdown success/failure explicitly. A timeout does not silently degrade into unsafe cleanup.

### 4. Cancellation vs Shutdown
The component treats cancellation and shutdown as semantically distinct operations.

| Operation | Scope | Worker Infrastructure | Effect |
|-----------|-------|----------------------|--------|
| `cancel_ota()` | Current transfer only | Preserved (task stays alive) | Aborts session, returns to `IDLE` |
| `deinit()` | Component lifecycle | Destroyed (task exits) | Aborts session, stops worker, releases resources |

- `cancel_ota()` aborts the active `OtaSession`, sends `OTA_CANCEL_BIT` to the worker, and transitions the manager to `IDLE`. The worker task remains alive and can process future `OTA_START_BIT` notifications.
- `deinit()` aborts the active session, sends `OTA_STOP_BIT`, waits for the worker exit confirmation, and only then releases `state_mutex_` and `shutdown_done_`.

This separation prevents a cancellation request from being interpreted as component teardown, and allows the worker to be reused across multiple OTA cycles without recreation overhead.

### 5. Security Policy Enforcement
The component implements a **data-driven security policy** that operates entirely within the orchestration layer — no TLS-specific ESP-IDF headers or SSL APIs appear in `ota_manager.cpp`.

- **Configuration:** `OtaSecurityConfig` (embedded in `OtaConfig`) provides a single flag:
  ```cpp
  struct OtaSecurityConfig {
      bool allow_http_during_development = false; // secure by default
  };
  ```

- **Validation Gates (`validate_url()`):** Applied at two network boundaries:
  1. **Manifest URL** — validated at the start of `handle_manifest_state()`, before any HTTP request
  2. **Firmware URL** — validated at the start of `handle_download_state()`, before `OtaSession::begin()`

- **Behavior:**
  - If `allow_http_during_development = false` (production default), only `https://` URLs are accepted.
  - If `allow_http_during_development = true`, all URLs are accepted (intended for local development).
  - Rejected URLs produce a clear log message (`Security policy violation: insecure URL not allowed`) and return `OtaStepResult::FAILED`.

- **Design Rationale:** Keeping policy decisions in `OtaManager` (not in transport implementations) ensures that policy is applied uniformly regardless of the underlying transport, is testable via host unit tests, and is visible in a single location.

### 6. Transport Configuration Ownership
The component uses a **configuration-ownership model** where `OtaManager` is the single source of truth for all transport settings.

- **`TransportConfig`:** Groups timeout settings explicitly:
  ```cpp
  struct TransportConfig {
      uint32_t manifest_timeout_ms; // timeout for manifest HTTP fetch
      uint32_t firmware_timeout_ms; // timeout for firmware OTA download
  };
  ```
  This replaces the earlier single `http_timeout_ms`, ensuring that manifest and firmware operations can be tuned independently.

- **Configuration Flow:** `OtaConfig` is passed to `init()` and stored internally. When `OtaManager` calls `IHttpClient::fetch()` or `IOtaSession::begin()`, it extracts the relevant timeout from its own `config_.transport` and passes it as a parameter. Dependencies remain stateless regarding OTA-specific configuration until the moment of use.

- **Domain Request Structures:** Transport parameters are passed via domain-level structs rather than ESP-IDF types:
  - `OtaDownloadRequest` (for `IOtaSession::begin()`) carries `url` and `timeout_ms`.
  - `IHttpClient::fetch()` accepts `timeout_ms` directly as a parameter.
  This ensures that transport implementation details do not propagate into the interface layer.

### 7. Firmware Integrity Verification
The `ota_manager` enforces mandatory SHA-256 validation for every update to ensure firmware integrity.

- **Precision Hashing:** Instead of using the native `esp_partition_get_sha256()` — which calculates the hash over the entire partition (including potential flash padding/garbage after the image) — we implement a manual calculation using `mbedtls` that processes exactly `firmware_size` bytes from the update partition.

- **Consistency:** This approach ensures that the hash calculated on the server (the build machine, via `sha256sum firmware.bin`) matches the one computed on the ESP32. It avoids mismatches caused by different partition sizes or trailing flash data, ensuring the binary transferred is exactly the one validated.

- **Verification Flow:**
  1. After successful download (`OtaSession::finish()`), the FSM transitions to `VERIFYING`.
  2. `handle_verification_state()` reads `firmware_size` bytes from the update partition using `mbedtls_sha256_starts / update / finish`.
  3. The resulting hash is hex-encoded and compared against `manifest.sha256_hex`.
  4. On mismatch, the OTA is marked as `FAILED` (the partition is not set as bootable).

- **Edge Case Handling:** If `esp_partition_read` fails during the hash calculation loop, `mbedtls_sha256_finish` is skipped entirely to avoid producing a partial hash. The function returns the partition read error, which propagates as `OtaStepResult::FAILED`.

### 8. Lifecycle Model
The `OtaManager` lifecycle follows a strict stateful contract:

```
Constructor → init(config) → [start_ota() / cancel_ota()]* → deinit() → Destructor
```

| Method | Precondition | Postcondition on Success | Thread-Safe |
|--------|-------------|--------------------------|-------------|
| `init(config)` | Clean state | Synchronization primitives created, status = IDLE | No (call once) |
| `start_ota()` | Status is `IDLE` or `FAILED` | Worker task created (if absent), OTA flow begins | Yes |
| `cancel_ota()` | Any | Active session aborted, status = IDLE, worker preserved | Yes |
| `deinit()` | `init()` succeeded | Worker stopped, all resources released, returns `bool` | Yes (idempotent) |

- The destructor does **not** call `deinit()` to avoid double-free in dependency injection scenarios. The caller is responsible for calling `deinit()` manually before destruction.
- Repeated `deinit()` calls are safe: once resources are cleaned up, subsequent calls return `true` immediately.

### 9. Integration and Validation
The component uses a multi-layered testing strategy:

- **Host-Based Unit Tests (Google Test + Mock):**
  - `test_ota_manager.cpp` — unit tests for each FSM handler, lifecycle contracts, thread safety, and edge cases (malformed versions, security policy rejection, double-deinit).
  - `test_ota_manager_task.cpp` — integration tests using a real FreeRTOS task scheduler with mocked HALs, covering full OTA flows (success, cancellation, verification failure, graceful shutdown).
  - `test_manifest_parser.cpp` — tests for JSON parsing, field validation, and error handling.
  - `test_version_helper.cpp` — tests for SemVer parsing, edge cases (v prefix, 4-component versions, overflow).
  - **Coverage:** 98% line coverage, published to GitHub Pages.

- **On-Target Integration Tests:**
  - `test_apps/test_build/` — build verification for the target ESP-IDF environment.
  - `examples/scenarios/` — four firmware scenario versions that exercise the full OTA lifecycle on real hardware:
    1. `v1.0.0_base` — initial firmware (flashed via UART)
    2. `v1.1.0_success` — validates a successful OTA update
    3. `v1.2.0_failure` — validates automatic rollback after a bad firmware boot
    4. `v1.3.0_security_failure` — validates that the security policy gate rejects HTTP when `allow_http_during_development = false`

- **CI/CD (GitHub Actions):**
  - `build.yml` — builds `test_apps/test_build` using `espressif/idf:release-v5.5` container on push/PR to `main`.
  - `host_test.yml` — builds and runs all host tests, generates coverage reports, and deploys them to GitHub Pages.
  - Both workflows use `ccache` for faster incremental builds.
