# On-Target OTA Validation Plan

This document outlines the procedure to validate the `ota_manager` component on real hardware (ESP32).

## Prerequisites

1.  **Hardware**: ESP32 DevKit (or any standard ESP32 module).
2.  **Network**: Local Wi-Fi network.
3.  **Local Server**: A host machine (Linux/Mac/PC) running a simple HTTP server (e.g., `python3 -m http.server 8080`).
4.  **Secrets**: A `main/include/secrets.h` file (excluded from Git) with:
    ```cpp
    #define WIFI_SSID "your_ssid"
    #define WIFI_PASS "your_password"
    #define SERVER_URL "http://<your-ip>:8080/manifest.json"
    ```

## Test Application Behavior

- **Periodic Logging**: Every 5 seconds, logs the current firmware version: `[APP] Running version: 1.x.x`.
- **Trigger**: Pressing the **BOOT button (GPIO 0)** initiates the OTA process via `ota_manager.start_ota()`.
- **Boot Check**: At every startup, the app calls `ota_manager.check_pending_verify()`.
    - If `true`, it performs a 10-second "sanity check" log loop.
    - If no crash occurs during those 10s, it calls `ota_manager.confirm_app_valid()`.

---

## Scenario 1: Successful Update (Happy Path)

### Goal
Verify that a newer firmware is correctly downloaded, verified, and applied.

### Steps
1.  **Build v1.0.0**: Set `version.txt` to `1.0.0`, build, and flash the ESP32.
2.  **Verify v1.0.0**: Monitor logs to see `Running version: 1.0.0`.
3.  **Prepare v1.1.0**:
    - Update `version.txt` to `1.1.0`.
    - Build the project.
    - Copy `build/ota_test_app.bin` to your server folder as `firmware_v110.bin`.
    - Create `manifest.json` on the server pointing to `firmware_v110.bin` with the correct SHA-256 and version `1.1.0`.
4.  **Trigger Update**: Press the BOOT button on the ESP32.
5.  **Observe**:
    - Logs should show `MANIFEST_FETCH` -> `DOWNLOADING` -> `VERIFYING` -> `READY_TO_RESTART`.
    - Device reboots.
    - Logs should show `Pending verify detected! Running sanity checks...`.
    - After 10s: `Firmware confirmed!`.
    - Logs now show `Running version: 1.1.0`.

---

## Scenario 2: Automatic Rollback (Failure Recovery)

### Goal
Verify that a firmware that fails to confirm its validity is automatically rolled back by the bootloader.

### Steps
1.  **Prepare "Bad" v1.2.0**:
    - Update `version.txt` to `1.2.0`.
    - Modify the code to **reboot immediately** after `check_pending_verify()` is detected, **without** calling `confirm_app_valid()`.
    - Build and host on the server as `firmware_bad.bin`.
    - Update `manifest.json` to version `1.2.0`.
2.  **Trigger Update**: Press the BOOT button on the device (currently running v1.1.0).
3.  **Observe Failure**:
    - Device downloads and boots into v1.2.0.
    - v1.2.0 detects `pending_verify`, fails diagnostics (reboots).
    - Upon next boot, the ESP32 Bootloader detects multiple failed boots/no confirmation and **rolls back** to the previous partition.
4.  **Verification**: Logs should show the device is back to `Running version: 1.1.0`.

---

## Success Criteria

- [ ] Device correctly ignores same or lower versions (already tested in host, confirmed via logs).
- [ ] Hash mismatch in manifest results in `OtaStatus::FAILED` and no reboot.
- [ ] Successful update results in the new version being reported after reboot.
- [ ] Rollback successfully returns the device to the last known good state if the new image is not confirmed.
