#!/usr/bin/env python3
import os
import sys
import json
import hashlib
import shutil
import argparse
from http.server import HTTPServer, SimpleHTTPRequestHandler

# Configurações de diretório (relativas ao local do script)
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.join(BASE_DIR, "repository")
MANIFESTS_DIR = os.path.join(BASE_DIR, "manifests")

def calculate_sha256(file_path):
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()

def deploy(device_type, version, bin_path):
    print(f"[*] Deploying {device_type} version {version} from {bin_path}...")
    
    if not os.path.exists(bin_path):
        print(f"[!] Error: Binary file not found: {bin_path}")
        sys.exit(1)

    # 1. Criar estrutura de diretórios
    target_dir = os.path.join(REPO_DIR, device_type, version)
    os.makedirs(target_dir, exist_ok=True)
    os.makedirs(MANIFESTS_DIR, exist_ok=True)

    # 2. Copiar binário
    file_name = os.path.basename(bin_path)
    target_bin = os.path.join(target_dir, file_name)
    shutil.copy2(bin_path, target_bin)
    print(f"[+] Binary copied to {target_bin}")

    # 3. Calcular Hash
    file_sha256 = calculate_sha256(target_bin)
    file_size = os.path.getsize(target_bin)

    # 4. Gerar URL relativa (ou absoluta se preferir)
    # Aqui usamos o caminho relativo ao servidor HTTP
    relative_bin_url = f"/repository/{device_type}/{version}/{file_name}"

    # 5. Criar/Atualizar Manifesto
    manifest_data = {
        "version": version,
        "sha256": file_sha256,
        "size": file_size,
        "url": relative_bin_url
    }

    manifest_file = os.path.join(MANIFESTS_DIR, f"{device_type}.json")
    with open(manifest_file, "w") as f:
        json.dump(manifest_data, f, indent=4)
    
    print(f"[+] Manifest updated: {manifest_file}")
    print(f"    Version: {version}")
    print(f"    SHA256:  {file_sha256}")
    print(f"    URL:     {relative_bin_url}")

def serve(port):
    os.chdir(BASE_DIR)
    print(f"[*] Starting OTA Server on port {port}...")
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
    deploy_parser.add_argument("device_type", help="Type of device (e.g., app_hub, app_water_tank)")
    deploy_parser.add_argument("version", help="Version string (e.g., 1.1.0)")
    deploy_parser.add_argument("bin_path", help="Path to the .bin file")

    # Command: serve
    serve_parser = subparsers.add_parser("serve", help="Start the HTTP server")
    serve_parser.add_argument("--port", type=int, default=8070, help="Port to listen on (default: 8070)")

    args = parser.parse_args()

    if args.command == "deploy":
        deploy(args.device_type, args.version, args.bin_path)
    elif args.command == "serve":
        serve(args.port)
    else:
        parser.print_help()
