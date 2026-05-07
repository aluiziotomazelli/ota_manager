# OTA Test Server

This directory contains the tools to manage and serve OTA updates for ESP32 devices during development and integration tests.

## Components

1.  **`manage.py`**: The primary CLI tool for firmware management (deploying binaries, generating manifests, and serving files).
2.  **`ota-server.sh`**: A wrapper script that configures system-level networking (Avahi/mDNS) and starts the server.

---

## Workflow

### 1. Deploying a New Firmware

The `deploy` command organizes the binary into the repository and updates the device's manifest. By default, it automatically detects the device type and version from the binary using `esptool.py`, and resolves your local IP for the manifest URL.

**Basic Usage (Zero Config):**
```bash
python3 manage.py deploy <path_to_bin>
```

**Advanced Usage (Manual Override):**
```bash
python3 manage.py deploy <path_to_bin> --device app_hub --version 1.2.0 --host ota-server.local
```

This command will:
- **Auto-Detect**: Extract Project Name and Version from the binary metadata.
- **Auto-IP**: Resolve your local network IP (or use `--host` / `OTA_HOSTNAME`).
- **Organize**: Create `repository/<device>/<version>/` and copy the binary.
- **Manifest**: Generate `manifests/<device>.json` with the correct SHA-256 and absolute URL.

### 2. Network Configuration

The script builds the `firmware_url` dynamically. It follows this priority for the host/IP:
1.  Command line argument: `--host <value>`
2.  Environment variable: `OTA_HOSTNAME`
3.  Automatic detection: Local network IP (default)

> **Note**: If you are using mDNS (e.g., `ota-server.local` via `ota-server.sh`), make sure to pass `--host ota-server.local` during deploy, or set `export OTA_HOSTNAME=ota-server.local` beforehand. Otherwise, the manifest will be generated with your machine's raw IP address, which may conflict with your mDNS setup.

### 3. Starting the Server

#### Option A: Using the Shell Script (Recommended for Linux)
Starts the server. By default, it runs without mDNS configuration.

```bash
# Default mode (no mDNS, uses local IP)
./ota-server.sh start

# Enable mDNS (requires sudo, configures system hostname)
./ota-server.sh start --mdns
```

> **Note**: When using `--mdns`, the script will automatically change your system hostname and restore it back to the original value upon exit (via `trap` on exit or Ctrl+C). You can customize the hostname with `--hostname <name>`.


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
