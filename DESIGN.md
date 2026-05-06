# OTA Manager Design Document

## Overview
The `ota_manager` is a passive, dependency-injected OTA update component for ESP-IDF. It is designed to be highly testable, thread-safe, and reactive.

## Architectural Decisions

### 1. Domain-Oriented HAL (Hardware Abstraction Layer)
To ensure the orchestration logic remains testable on host platforms and decoupled from the ESP-IDF SDK, we adopted a domain-oriented HAL strategy rather than a header-based one.
- **Interfaces:** Each HAL (`IHttpClient`, `IOtaSession`, `ISystem`, `ITaskScheduler`, `IRollbackManager`) represents a business domain responsibility.
- **Low-Level Transparency:** HAL interfaces preserve native ESP-IDF types (e.g., `esp_err_t`, `TaskHandle_t`) to ensure the component remains technically aware of the target platform while maintaining a clean abstraction boundary.
- **Decoupling:** Business logic interacts only with these interfaces, allowing for total isolation during host-based unit testing via Google Mock.

### 2. Pure Iterative Finite State Machine (FSM)
To handle the OTA orchestration process without blocking the application or risking race conditions, we implemented a Pure Iterative FSM within a dedicated worker task.
- **Event-Driven:** The task uses `xTaskNotifyWait` to react to `OTA_START_BIT`, `OTA_STOP_BIT`, and `OTA_CANCEL_BIT`.
- **Iterative Execution:** The download process is broken down into discrete steps. Instead of a monolithic blocking loop, the task performs a single chunk/step per iteration, allowing the loop to remain responsive to `STOP` or `CANCEL` signals at all times.
- **State Machine Logic:** The status (e.g., `MANIFEST_FETCH`, `DOWNLOADING`) is managed within a central `switch` block, ensuring clean, deterministic transitions.

### 3. Synchronization and Thread Safety
Managing the lifecycle of a long-running worker task is complex. We implemented the following mechanisms:
- **Notification-Based Control:** Replaced boolean flags and mutex-heavy cancellation checks with direct FreeRTOS task notifications (`OTA_CANCEL_BIT`).
- **Resource Protection:** Used a `state_mutex_` to ensure atomic state updates and safe access to task handles.
- **Graceful Shutdown:** Implemented a binary semaphore (`shutdown_done_`) that the `deinit()` method uses to synchronize the destruction of task resources. The worker task is guaranteed to signal this semaphore before terminating, preventing `deinit()` from deleting a task handle that is still in use.

### 4. Integration and Validation
- **Host Testing:** All complex components are designed to be tested on the host by mocking the HAL interfaces. Stub headers are provided for ESP-IDF specific files to facilitate successful host-side compilation.
### 4. Firmware Integrity Verification
The `ota_manager` enforces mandatory SHA-256 validation for every update to ensure firmware integrity.
- **Precision Hashing:** Instead of using the native `esp_partition_get_sha256()` which calculates the hash over the entire partition (including potential flash padding/garbage), we implement a manual calculation using `mbedtls` that processes exactly `firmware_size` bytes. 
- **Consistency:** This approach ensures that the hash calculated on the server (the build machine) matches the one computed on the ESP32. It avoids mismatches caused by different partition sizes or trailing flash data, ensuring the binary transferred is exactly the one validated.
- **Security:** By hashing the exact binary data before confirmation, we protect the device from partial downloads, corruption, or malicious modifications injected into the partition.
