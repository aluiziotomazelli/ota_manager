# OTA Manager Scenario Examples

This directory contains a set of scenarios designed to validate the OTA (Over-the-Air) update process using the `ota_manager` component.

## Scenarios

The examples are organized by version to simulate a real update lifecycle:

1.  **`v1.0.0_base`**: The initial firmware version. This should be flashed to the device via cable (UART).
2.  **`v1.1.0_success`**: A newer version used to test a successful update flow.
3.  **`v1.2.0_failure`**: A version that simulates a "bad" firmware (e.g., fails sanity checks after boot) to test the automatic rollback functionality.

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

1.  **Flash Base**: Build and flash `v1.0.0_base` to your ESP32.
2.  **Build other versions**: Build `v1.1.0_success` and `v1.2.0_failure` to generate the `.bin` files.
3.  **Start Server**: Run the OTA server and use `manage.py deploy` to host the `.bin` files from the second scenario: `v1.1.0_success`.
4.  **Trigger Update**: Press the **BOOT button** (GPIO 0 on ESP32/S3, GPIO 9 on C3) to initiate the OTA process.
5.  **Observe**: Monitor the serial logs to see the status transitions and the version change after reboot.
6.  **Deploy tird scenario**: Use `manage.py deploy` to host the `.bin` files from the third scenario: `v1.2.0_failure`.
7.  **Trigger Update**: Press the **BOOT button** (GPIO 0 on ESP32/S3, GPIO 9 on C3) to initiate the OTA process.
8.  **Observe**: Monitor the serial logs to see the OTA, when the v1.2.0 is booted and it is not confirmed, it should rollback to v1.1.0.
