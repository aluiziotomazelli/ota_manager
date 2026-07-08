# OTA Manager

[![ESP-IDF Build](https://github.com/aluiziotomazelli/ota_manager/actions/workflows/build.yml/badge.svg)](https://github.com/aluiziotomazelli/ota_manager/actions/workflows/build.yml)
[![Host Tests](https://github.com/aluiziotomazelli/ota_manager/actions/workflows/host_test.yml/badge.svg)](https://github.com/aluiziotomazelli/ota_manager/actions/workflows/host_test.yml)
[![Coverage](https://img.shields.io/badge/coverage-98%25-green)](https://aluiziotomazelli.github.io/ota_manager/index.html)

> **Security Note**
> This component is designed for **local network use only** and utilizes **plain HTTP** for manifest and firmware retrieval. It is intended for environments where the user has full control over the network infrastructure. Because this runs only in HTTP, the `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y` must be enabled in `sdkconfig` via `menuconfig` to permit HTTP firmware downloads.

A passive, dependency-injected OTA (Over-the-Air) update component designed for ESP-IDF. It provides a robust, thread-safe, and highly testable orchestration logic for firmware updates.

## Overview

`OtaManager` follows an **Active Object + Pure Iterative FSM** architecture. It isolates core orchestration logic from platform-specific APIs via domain-oriented HALs, making it 100% testable on host platforms.

Unlike traditional OTA implementations, this component is **passive**: the application remains in full control of when the update window opens, when to trigger the process, and when to confirm the new image after reboot.

## Key Features

- **Iterative FSM**: The OTA process is broken down into small, non-blocking steps, ensuring the background task remains responsive to `STOP` or `CANCEL` signals at all times.
- **Dependency Injection**: All platform/SDK dependencies (HTTP, Flash, System, Tasks) are injected via interfaces, allowing for total isolation during unit testing.
- **Secure by Design**: Includes robust SemVer version comparison (downgrade prevention), device-type validation, and mandatory SHA-256 hash verification on the update partition.
- **Thread-Safe**: Uses direct FreeRTOS task notifications for lightweight signaling and semaphores for guaranteed safe resource cleanup during deinitialization.
- **Passive Trigger Model**: Designed for modern IoT applications where the firmware manages its own update lifecycle.

## Architecture

`OtaManager` uses a **Domain-Oriented HAL pattern** to decouple business logic from the hardware/SDK:

```
┌─────────────────────────────────────────────────────────┐
│                    OtaManager                           │
│             (Active Object / FSM Task)                  │
└─────────────────────────────────────────────────────────┘
              │              │              │
    ┌─────────┴──────┐ ┌───┴────────┐ ┌──┴──────────┐
    │  IHttpClient   │ │ IOtaSession│ │   ISystem   │
    │  (Manifest)    │ │ (Download) │ │ (Partitions)│
    └────────────────┘ └────────────┘ └─────────────┘
              │              │              │
    ┌─────────┴──────┐ ┌───┴────────┐ ┌──┴──────────┐
    │ ITaskScheduler │ │ IRollbackMgr│ │IManifestPrsr│
    │ (FreeRTOS)     │ │ (App State)│ │ (JSON)      │
    └────────────────┘ └────────────┘ └─────────────┘
```

### Core Components

| Component | Responsibility |
|-----------|---------------|
| `OtaManager` | Main orchestrator, manages state machine and background task lifecycle. |
| `HttpClient` | Fetches the update manifest over HTTP. |
| `OtaSession` | Encapsulates the `esp_https_ota` workflow (begin, perform, finish). |
| `ManifestParser` | Parses JSON manifests and validates metadata. |
| `VersionHelper` | Robust SemVer parser and comparator. |
| `System` | Provides access to running app info and partition hash calculation. |
| `TaskScheduler` | Abstraction for FreeRTOS tasks and synchronization primitives. |

## Requirements

- **Framework**: ESP-IDF v5.0+
- **Language**: C++17
- **Network**: Requires `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y` enabled in `sdkconfig` to permit HTTP firmware downloads.
- **Rollback**: Requires `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` enabled in `sdkconfig` to allow automatic firmware rollback upon failed verification.
- **Dependencies**:
  - `cJSON` (included in ESP-IDF)
  - `esp_https_ota`
  - `app_update`

## Quick Start Guide

### 1. Installation

Add the component as a submodule to your ESP-IDF project:

```bash
git submodule add https://github.com/aluiziotomazelli/ota_manager components/ota_manager
```

### 2. Basic Initialization

```cpp
#include "ota_manager.hpp"

// 1. Setup HAL implementations (or use defaults)
HttpClient http;
ManifestParser parser;
OtaSession session;
System sys;
TaskScheduler scheduler;
RollbackManager rollback;

OtaDependencies deps = { http, parser, session, sys, scheduler, rollback };

// 2. Configure the manager
OtaConfig config = {
    .device_type = "water_tank",
    .manifest_url = "http://192.168.1.100/update.json",
    .task_stack_size = 8192,
    .task_priority = 5,
    .transport = {
        .manifest_timeout_ms = 10000,
        .firmware_timeout_ms = 10000,
    },
    .security = {
        .allow_http_during_development = true,
    },
    .allow_same_version = false,
    .restart_on_success = true
};

// 3. Initialize and Start
OtaManager ota(deps);
if (ota.init(config)) {
    // Trigger OTA process in the background
    ota.start_ota();
}
```

### 3. Verification after Boot

```cpp
// Check if the current app is pending verification
if (ota.check_pending_verify()) {
    // Run application-level diagnostics...
    if (diagnostics_passed) {
        ota.confirm_app_valid();
        ESP_LOGI(TAG, "New firmware confirmed!");
    } else {
        ota.rollback_and_reboot();
    }
}
```

## Local Server Setup

To perform an update, the device needs access to a manifest JSON file and the firmware binary.

### 1. Manifest Format

Create a `manifest.json` based on the following template:

```json
{
    "device_type": "water_tank",
    "version": "1.1.0",
    "firmware_url": "http://<your-server-ip>:8080/firmware.bin",
    "firmware_size": 693312,
    "sha256_hex": "2e3c09f3e4b52479e0f22f7783d78909d94943f338d42d3858c2794c483a992e"
}
```

### 2. How to Set Firmware Version

The `version` in the manifest must match the version "stamped" into your firmware binary. You can set your application version in one of three ways:

- **Option A: `version.txt` file (easiest)**: Create a file named `version.txt` in your project root with the version string:
  ```text
  1.1.0
  ```
- **Option B: `CMakeLists.txt`**: Set the `PROJECT_VER` variable before including `project.cmake`:
  ```cmake
  set(PROJECT_VER "1.1.0")
  include($ENV{IDF_PATH}/tools/cmake/project.cmake)
  ```
- **Option C: Git Tags**: If your project is a Git repository, ESP-IDF will automatically use `git describe --always --tags --dirty` as the version string.

For more detailed instructions on how to set firmware version see [ESP-IDF Build/Project Variables](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html#build-project-variables)

### 3. Hosting Files

You can use a simple Python HTTP server to host the files:

```bash
# In the folder containing manifest.json and firmware.bin
python3 -m http.server 8080
```

### 4. Calculating SHA-256

To get the hash for your binary:

```bash
sha256sum firmware.bin
```

### 5. Automating Manifest Management

For a streamlined development workflow, use the included OTA Test Server tools. It provides a CLI (`manage.py`) that automatically calculates the SHA-256 hash, determines firmware metadata, and generates a properly formatted `manifest.json`.

- **Location**: `ota_server/`
- **Documentation**: See [OTA Test Server README](ota_server/README.md) for full usage instructions and automation examples.

## Project Structure

```
ota_manager/
├── include/
│   ├── interfaces/             # Pure abstract interfaces
│   │   ├── i_*.hpp
│   ├── ota_manager.hpp         # Main public header
│   └── ota_types.hpp           # Common data structures
├── src/                        # Concrete HAL implementations + FSM
│   ├── ota_manager.cpp         # Orchestration logic
│   ├── http_client.cpp         # HTTP manifest fetch
│   ├── ota_session.cpp         # esp_https_ota wrapper
│   ├── manifest_parser.cpp     # JSON parsing
│   ├── rollback_manager.cpp    # Rollback state management
│   ├── system.cpp              # Partition hashing (mbedtls)
│   └── version_helper.cpp      # SemVer parser
├── host_test/                  # Host-based GTest + GMock suite
│   └── test_ota_manager/
├── examples/
│   └── scenarios/              # On-target integration test scenarios
│       ├── v1.0.0_base/
│       ├── v1.1.0_success/
│       ├── v1.2.0_failure/
│       └── v1.3.0_security_failure/
├── ota_server/                 # Local OTA test server (manage.py)
├── test_apps/                  # Additional on-device tests
│   └── test_build/
└── docs/                       # Architecture and planning docs
    ├── DESIGN.md
    ├── API.md
    ├── CHANGELOG.md
    └── HTTPS_UPGRADE_PLAN.md
```

## Testing

### Host Tests (Linux)

Unit tests run on Linux using **Google Test** and **Google Mock**. Since the component is fully dependency-injected, we can simulate complex network failures and partition errors without hardware.

```bash
cd host_test/test_ota_manager
. ~/dev/esp/esp-idf/export.sh
idf.py build
```

### On-Target Integration Tests (Scenario Examples)

The [`examples/scenarios/`](examples/scenarios/) directory contains **four firmware versions** that serve as on-target integration tests, exercising the full OTA lifecycle on real ESP32 hardware:

| Scenario | Version | What It Validates |
|----------|---------|-------------------|
| `v1.0.0_base` | 1.0.0 | Base firmware, flashed via UART as the initial state |
| `v1.1.0_success` | 1.1.0 | Successful OTA update flow (download, verify, reboot) |
| `v1.2.0_failure` | 1.2.0 | Simulated bad firmware — bootloader automatic rollback after missing confirmation |
| `v1.3.0_security_failure` | 1.3.0 | Security policy gate — rejects HTTP when `allow_http_during_development = false` |

Each scenario includes its own `partitions.csv`, `sdkconfig.defaults`, and a `main.cpp` with Wi-Fi connection and button-triggered OTA. Together, they form a **progressive test sequence**: start with `v1.0.0_base`, OTA to `v1.1.0_success`, then to `v1.2.0_failure` (observe rollback), then to `v1.3.0_security_failure` (observe security policy rejection).

For detailed instructions, see the [Scenarios README](examples/scenarios/README.md).

## Documentation

- [`API.md`](API.md) - Complete API reference with data types, lifecycle methods, and usage example
- [`DESIGN.md`](DESIGN.md) - Internal architecture and design decisions
- [`CHANGELOG.md`](CHANGELOG.md) - Project history and releases

## License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.
