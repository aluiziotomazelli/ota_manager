#!/usr/bin/env python3
import os
import sys
import json
import hashlib
import shutil
import argparse
import subprocess
import re
import socket
from http.server import HTTPServer, SimpleHTTPRequestHandler

# Directory configurations (relative to the script location)
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.join(BASE_DIR, "repository")
MANIFESTS_DIR = os.path.join(BASE_DIR, "manifests")

def get_local_ip():
    """Detects the machine's local IP."""
    try:
        # Attempts to connect to an external IP (does not send data) to discover the active interface
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def calculate_sha256(file_path):
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()

def extract_metadata(bin_path):
    """Extracts device_type and version from binary using esptool.py"""
    print(f"[*] Extracting metadata from {bin_path} using esptool.py...")
    try:
        result = subprocess.run(
            ["esptool.py", "image_info", "--version", "2", bin_path],
            capture_output=True, text=True, check=True
        )
        output = result.stdout
        
        metadata = {}
        # Find Project name
        project_match = re.search(r"Project name:\s+(.+)", output)
        if project_match:
            metadata['device_type'] = project_match.group(1).strip()
            
        # Find App version
        version_match = re.search(r"App version:\s+(.+)", output)
        if version_match:
            metadata['version'] = version_match.group(1).strip()
            
        return metadata
    except Exception as e:
        print(f"[!] Warning: Could not auto-detect metadata: {e}")
        return {}

def deploy(bin_path, device_type=None, version=None, host=None, port=None):
    if not os.path.exists(bin_path):
        print(f"[!] Error: Binary file not found: {bin_path}")
        sys.exit(1)

    # 1. Metadata auto-detection if data is missing
    if not device_type or not version:
        auto_data = extract_metadata(bin_path)
        device_type = device_type or auto_data.get('device_type')
        version = version or auto_data.get('version')

    if not device_type or not version:
        print("[!] Error: Could not determine device_type or version. Please provide them manually.")
        sys.exit(1)

    # 2. Host and Port configuration for the URL
    # Priority: Argument > Environment Variable > Auto-detection/Default
    host = host or os.environ.get("OTA_HOSTNAME") or get_local_ip()
    port = port or int(os.environ.get("OTA_PORT", 8070))

    print(f"[*] Target: {host}:{port}")
    print(f"[*] Deploying {device_type} version {version} from {bin_path}...")

    # 3. Create directory structure
    target_dir = os.path.join(REPO_DIR, device_type, version)
    os.makedirs(target_dir, exist_ok=True)
    os.makedirs(MANIFESTS_DIR, exist_ok=True)

    # 4. Copy binary
    file_name = os.path.basename(bin_path)
    target_bin = os.path.join(target_dir, file_name)
    shutil.copy2(bin_path, target_bin)
    print(f"[+] Binary copied to {target_bin}")

    # 5. Calculate Hash and Size
    file_sha256 = calculate_sha256(target_bin)
    file_size = os.path.getsize(target_bin)

    # 6. Generate Firmware URL
    relative_bin_url = f"/repository/{device_type}/{version}/{file_name}"
    firmware_url = f"http://{host}:{port}{relative_bin_url}"

    # 7. Create/Update Manifest
    manifest_data = {
        "device_type": device_type,
        "version": version,
        "sha256_hex": file_sha256,
        "firmware_size": file_size,
        "firmware_url": firmware_url
    }

    manifest_file = os.path.join(MANIFESTS_DIR, f"{device_type}.json")
    with open(manifest_file, "w") as f:
        json.dump(manifest_data, f, indent=4)
    
    print(f"[+] Manifest updated: {manifest_file}")
    print(f"    Device:       {device_type}")
    print(f"    Version:      {version}")
    print(f"    Firmware URL: {firmware_url}")
    print(f"    SHA256:       {file_sha256}")

def serve(port):
    os.chdir(BASE_DIR)
    print(f"[*] Starting OTA Server on port {port}...")
    print(f"[*] Local IP detected: {get_local_ip()}")
    print(f"[*] Base directory: {BASE_DIR}")
    server_address = ('', port)
    httpd = HTTPServer(server_address, SimpleHTTPRequestHandler)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[!] Server stopped.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="OTA Server Management Tool")
    subparsers = parser.add_subparsers(dest="command", help="Commands")

    # Command: deploy
    deploy_parser = subparsers.add_parser("deploy", help="Deploy a new firmware version")
    deploy_parser.add_argument("bin_path", help="Path to the .bin file")
    deploy_parser.add_argument("--device", help="Force device type (overrides auto-detection)")
    deploy_parser.add_argument("--version", help="Force version string (overrides auto-detection)")
    deploy_parser.add_argument("--host", help="Force server host/IP in manifest (overrides auto-detection)")
    deploy_parser.add_argument("--port", type=int, help="Force server port in manifest (overrides auto-detection)")

    # Command: serve
    serve_parser = subparsers.add_parser("serve", help="Start the HTTP server")
    serve_parser.add_argument("--port", type=int, default=8070, help="Port to listen on (default: 8070)")

    args = parser.parse_args()

    if args.command == "deploy":
        deploy(args.bin_path, 
               device_type=args.device, 
               version=args.version, 
               host=args.host, 
               port=args.port)
    elif args.command == "serve":
        serve(args.port)
    else:
        parser.print_help()
