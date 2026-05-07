# OTA Manager — API Reference

Complete API documentation for the OTA Manager component. This reference covers all public methods, their parameters, return values, and usage notes.

---

## Table of Contents

- [Data Types and Enumerations](#data-types-and-enumerations)
  - [`OtaStatus`](#otastatus)
  - [`OtaVersion`](#otaversion)
  - [`OtaManifest`](#otamanifest)
  - [`OtaConfig`](#otaconfig)
- [Lifecycle Methods](#lifecycle-methods)
  - [`init()`](#init)
  - [`deinit()`](#deinit)
- [Orchestration](#orchestration)
  - [`start_ota()`](#start_ota)
  - [`cancel_ota()`](#cancel_ota)
- [Status and Validation](#status-and-validation)
  - [`get_status()`](#get_status)
  - [`check_pending_verify()`](#check_pending_verify)
  - [`confirm_app_valid()`](#confirm_app_valid)
  - [`rollback_and_reboot()`](#rollback_and_reboot)

---

## Data Types and Enumerations

### `OtaStatus`

Enumeration of possible OTA operation states.

```cpp
enum class OtaStatus {
    IDLE,             /**< OTA manager is idle, no operation in progress */
    MANIFEST_FETCH,   /**< Currently fetching the manifest file */
    VERSION_CHECK,    /**< Comparing version numbers */
    DOWNLOADING,      /**< Downloading firmware image */
    VERIFYING,        /**< Verifying downloaded firmware */
    READY_TO_RESTART, /**< Firmware downloaded and verified, ready to apply */
    FAILED,           /**< OTA operation failed */
    PENDING_VERIFY    /**< New firmware applied, waiting for confirmation */
};
```

---

### `OtaVersion`

Structure representing a semantic version number.

```cpp
struct OtaVersion {
    uint16_t major; /**< Major version number */
    uint16_t minor; /**< Minor version number */
    uint16_t patch; /**< Patch version number */
};
```

---

### `OtaManifest`

Structure representing an OTA manifest file metadata.

```cpp
struct OtaManifest {
    std::string device_type;  /**< Device type identifier for compatibility */
    OtaVersion version;       /**< Version of the firmware */
    std::string firmware_url; /**< URL to download the firmware image */
    uint32_t firmware_size;   /**< Size of the firmware image in bytes */
    std::string sha256_hex;   /**< SHA256 hash of the firmware for integrity verification */
};
```

---

### `OtaConfig`

Configuration structure for initializing the `OtaManager`.

```cpp
struct TransportConfig {
    uint32_t manifest_timeout_ms; /**< Timeout for manifest fetching (ms) */
    uint32_t firmware_timeout_ms; /**< Timeout for firmware download (ms) */
};

struct OtaConfig {
    std::string device_type;  /**< Device type to check in manifest compatibility */
    std::string manifest_url; /**< URL to fetch the OTA manifest from */
    uint32_t task_stack_size; /**< Stack size for the OTA task */
    uint8_t task_priority;    /**< Priority for the OTA task */
    TransportConfig transport;    /**< Transport-specific settings */
    bool allow_same_version;  /**< Whether to allow updates to the same version */
    bool restart_on_success;  /**< Whether to automatically restart after successful update */
};
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `device_type` | `std::string` | - | **Required.** Identifier for the target hardware |
| `manifest_url` | `std::string` | - | **Required.** Full HTTP URL for the manifest JSON |
| `task_stack_size` | `uint32_t` | `8192` | Stack size for the worker task |
| `task_priority` | `uint8_t` | `5` | Priority for the worker task |
| `transport` | `TransportConfig` | - | Network operation timeouts |
| `allow_same_version` | `bool` | `false` | If true, permits reinstalling current version |
| `restart_on_success` | `bool` | `true` | If true, reboots immediately after hash verification |

---

## Lifecycle Methods

### `init()`

Initializes the OTA manager with the provided configuration. This method creates necessary synchronization primitives (mutexes/semaphores).

```cpp
bool init(const OtaConfig& config);
```

- **Parameters:**
    - `config`: Reference to the `OtaConfig` structure.
- **Returns:** `true` if initialization was successful, `false` otherwise (e.g., failed to create mutex).

### `deinit()`

Cleans up resources and ensures the background task is safely terminated. If an OTA is in progress, it will be aborted.

```cpp
void deinit();
```

---

## Orchestration

### `start_ota()`

Initiates the OTA update process in the background. If the task isn't running, it will be created.

```cpp
bool start_ota();
```

- **Returns:** `true` if the process was successfully queued/started, `false` if the manager is already busy or failed to create the task.

### `cancel_ota()`

Requests a graceful cancellation of the current OTA process. The background task will abort the download and return to `IDLE` state as soon as possible.

```cpp
void cancel_ota();
```

---

## Status and Validation

### `get_status()`

Returns the current state of the OTA manager.

```cpp
OtaStatus get_status() const;
```

- **Returns:** Current `OtaStatus`. This method is thread-safe.

### `check_pending_verify()`

Checks if the system has recently booted from a new firmware that is waiting for validation (rollback state).

```cpp
bool check_pending_verify() const;
```

- **Returns:** `true` if the current partition is in `PENDING_VERIFY` state.

### `confirm_app_valid()`

Confirms that the currently running firmware is valid. This prevents the bootloader from rolling back to the previous version on the next reboot.

```cpp
bool confirm_app_valid();
```

- **Returns:** `true` if the operation was successful.

### `rollback_and_reboot()`

Explicitly rejects the current firmware, triggers a rollback to the previous working version, and reboots the device immediately.

```cpp
void rollback_and_reboot();
```

---

## Usage Example

```cpp
OtaDependencies deps = { ... }; // HAL implementations
OtaConfig config = { ... };

OtaManager ota(deps, config);
ota.init(config);

// Start update
if (ota.start_ota()) {
    while (ota.get_status() != OtaStatus::READY_TO_RESTART && ota.get_status() != OtaStatus::FAILED) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```
