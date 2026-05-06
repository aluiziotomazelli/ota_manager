# OTA Test Server

This directory contains the tools to manage and serve OTA updates for ESP32 devices during development and integration tests.

## Components

1.  **`manage.py`**: The primary CLI tool for firmware management (deploying binaries, generating manifests, and serving files).
2.  **`ota-server.sh`**: A wrapper script that configures system-level networking (Avahi/mDNS) and starts the server.

---

## Workflow

### 1. Deploying a New Firmware

When you have a compiled `.bin` file, use the `deploy` command to organize it into the repository and update the device's manifest.

```bash
python3 manage.py deploy <device_type> <version> <path_to_bin>
```

**Example:**
```bash
python3 manage.py deploy central_hub 1.1.0 ../scenarios/v1.1.0_success/build/ota_test_v110.bin
```

This command will:
- Calculate the SHA-256 hash of the binary.
- Create the directory `repository/central_hub/1.1.0/`.
- Copy the binary to that directory.
- Update `manifests/central_hub.json` with the new version, size, hash, and relative URL.

### 2. Starting the Server

#### Option A: Using the Shell Script (Recommended for Linux)
Configures `ota-server.local` via mDNS and starts the server.
```bash
./ota-server.sh start
```

#### Option B: Using Python directly
Starts the server on a specific port without touching system DNS.
```bash
python3 manage.py serve --port 8070
```

---

## Directory Structure

- `repository/`: Contains the immutable history of all deployed firmwares, organized by `device_type` and `version`.
- `manifests/`: Contains the latest manifest for each `device_type`. This is what the ESP32 should point to.
- `static/`: (Optional) For any other static files needed during testing.

## Configuring the ESP32

In your ESP32 code (`OtaConfig`), point the `manifest_url` to:
`http://<your-ip-or-hostname>:8070/manifests/<device_type>.json`

Example for `central_hub`:
`http://ota-server.local:8070/manifests/central_hub.json`
