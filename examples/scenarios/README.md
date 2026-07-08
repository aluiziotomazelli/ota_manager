# OTA Manager Scenario Examples

This directory contains a set of scenarios designed to validate the OTA (Over-the-Air) update process using the `ota_manager` component.

## Scenarios

The examples are organized by version to simulate a real update lifecycle:

1.  **`v1.0.0_base`**: The initial firmware version. This should be flashed to the device via cable (UART).
2.  **`v1.1.0_success`**: A newer version used to test a successful update flow. Configures `allow_http_during_development = true` to permit HTTP downloads.
3.  **`v1.2.0_failure`**: A version that simulates a "bad" firmware (e.g., fails sanity checks after boot) to test the automatic rollback functionality. It confirms itself only after simulated sanity checks fail, triggering `esp_restart()` to force the bootloader rollback.
4.  **`v1.3.0_security_failure`**: A version that validates the **security policy enforcement** feature. It sets `allow_http_during_development = false` while the manifest URL still uses HTTP. When the OTA button is pressed, the `OtaManager` rejects the update immediately at the manifest URL validation stage — **before any network request is made** — and transitions to `FAILED` state. This proves that the security policy gate works correctly.

## Flash and Partitions

The scenarios are configured for a **4 MB Flash** size. Each scenario includes a `partitions.csv` file with a custom layout that provides two OTA slots (1920 KB each) and an OTA Data partition (144 KB), enabling the update and rollback process.

- **Configuration**: `CONFIG_PARTITION_TABLE_CUSTOM=y` and `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` are pre-configured in `sdkconfig.defaults`.
- **Partition Table**: The layout is defined in the `partitions.csv` file within each scenario directory.

## Configuration

Before building the scenarios, you need to configure your Wi-Fi and Server credentials:

1.  Locate the [example_secrets.h](example_secrets.h) file in this directory.
2.  Copy or rename it to `secrets.h`.
3.  Edit `secrets.h` with your Wi-Fi SSID, Password, and the URL of your OTA manifest.

```cpp
#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"
#define SERVER_URL "http://<your-ip>:8070/manifests/test_ota_manager.json"
```

> **Note**: The scenarios are configured to include this shared file via `#include "../secrets.h"`.

## OTA Server

To serve the firmware binaries and manifests, you can use the [ota_server](../../ota_server) tool located in the project root. It provides scripts to automate the deployment of binaries and management of manifests.

For detailed instructions, see the [OTA Server README](../../ota_server/README.md).

## How to Run

### Phase 1 — Successful Update (v1.0.0 → v1.1.0)

1.  **Flash Base**: Build and flash `v1.0.0_base` to your ESP32 via UART.
2.  **Build the success scenario**: Build `v1.1.0_success` to generate the firmware binary.
3.  **Start Server**: Run the OTA server and use `manage.py deploy` to host the binary from `v1.1.0_success`.
4.  **Trigger Update**: Press the **BOOT button** (GPIO 0 on ESP32/S3, GPIO 9 on C3) to initiate the OTA process.
5.  **Observe**: Monitor the serial logs to see the status transitions. The device should download v1.1.0, verify it, and reboot into the new version.

### Phase 2 — Rollback Test (v1.1.0 → v1.2.0 → rollback to v1.1.0)

6.  **Build the failure scenario**: Build `v1.2.0_failure` to generate the firmware binary.
7.  **Deploy**: Use `manage.py deploy` to host the binary from `v1.2.0_failure`.
8.  **Trigger Update**: Press the **BOOT button** to initiate the OTA process.
9.  **Observe**: The device downloads and boots v1.2.0. That firmware simulates a sanity check failure and calls `esp_restart()`. The bootloader detects the image was not confirmed and automatically rolls back to v1.1.0.

### Phase 3 — Security Policy Test (v1.1.0 → v1.3.0 → policy blocks HTTP)

10. **Build the security test scenario**: Build `v1.3.0_security_failure` to generate the firmware binary.
11. **Deploy**: Use `manage.py deploy` to host the binary from `v1.3.0_security_failure`.
12. **Update to v1.3.0**: With the device running v1.1.0 (which has `allow_http_during_development = true`), press the **BOOT button**. The update to v1.3.0 succeeds, and the device reboots.
13. **Trigger the security test**: Now the device is running v1.3.0, which has `allow_http_during_development = false`. Press the **BOOT button** again. The `OtaManager` immediately rejects the manifest URL (`http://...`) and transitions to `FAILED` — **before any network request is made**.
14. **Observe**: The serial log will show:
    ```
    OtaManager: Security policy violation: insecure URL not allowed: http://...
    main: OTA Failed as expected due to security policy!
    ```
    The device remains on v1.3.0. This proves the security policy enforcement gate is working correctly.
