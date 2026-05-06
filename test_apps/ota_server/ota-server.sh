#!/bin/bash

# Script to configure and start OTA server for ESP32
# Author: Generated Script
# Date: 2025-01-01

set -e  # Exit on error

# ========== CONFIGURATIONS ==========
HOSTNAME_DEFAULT="ota-server"
PORT="${OTA_PORT:-8070}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$SCRIPT_DIR"

ENABLE_MDNS=false
CUSTOM_HOSTNAME="$HOSTNAME_DEFAULT"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# ========== FUNCTIONS ==========

restore_hostname() {
    if [ "$ENABLE_MDNS" = true ] && [ -n "$ORIGINAL_HOSTNAME" ]; then
        print_info "Restoring original hostname: $ORIGINAL_HOSTNAME"
        sudo hostnamectl set-hostname "$ORIGINAL_HOSTNAME"
        sudo systemctl restart avahi-daemon
    fi
}

# Trap to ensure hostname is restored on exit
trap restore_hostname EXIT

parse_args() {
    while [[ "$#" -gt 0 ]]; do
        case $1 in
            --mdns) ENABLE_MDNS=true ;;
            --hostname) CUSTOM_HOSTNAME="$2"; shift ;;
            *) print_error "Unknown option: $1"; exit 1 ;;
        esac
        shift
    done
}

print_step() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

print_info() {
    echo -e "${YELLOW}[i]${NC} $1"
}

setup_mdns() {
    if [ "$ENABLE_MDNS" = true ]; then
        check_avahi
        ORIGINAL_HOSTNAME=$(hostnamectl --static)
        print_info "Current hostname: $ORIGINAL_HOSTNAME"
        print_info "Setting hostname to: $CUSTOM_HOSTNAME"
        sudo hostnamectl set-hostname "$CUSTOM_HOSTNAME"
        sudo systemctl restart avahi-daemon
        print_step "Hostname configured: $CUSTOM_HOSTNAME"
    fi
}

check_avahi() {
    if ! command -v avahi-daemon &> /dev/null; then
        print_error "Avahi is not installed"
        read -p "Do you want to install it? (y/n): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            sudo apt update
            sudo apt install -y avahi-daemon
            print_step "Avahi installed"
        else
            print_error "Avahi is required. Exiting..."
            exit 1
        fi
    else
        print_step "Avahi is already installed"
    fi
}

setup_hostname() {
    CURRENT_HOSTNAME=$(hostnamectl --static)
    if [ "$CURRENT_HOSTNAME" != "$HOSTNAME" ]; then
        print_info "Current hostname: $CURRENT_HOSTNAME"
        print_info "Setting hostname to: $HOSTNAME"
        sudo hostnamectl set-hostname "$HOSTNAME"
        sudo systemctl restart avahi-daemon
        print_step "Hostname configured: $HOSTNAME"
    else
        print_step "Hostname is already correct: $HOSTNAME"
    fi
}

check_server_dir() {
    if [ ! -d "$SERVER_DIR" ]; then
        print_error "Directory $SERVER_DIR does not exist"
        read -p "Do you want to create it? (y/n): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            mkdir -p "$SERVER_DIR"
            print_step "Directory created: $SERVER_DIR"
        else
            print_error "Directory required. Exiting..."
            exit 1
        fi
    else
        print_step "Directory found: $SERVER_DIR"
    fi
}

list_firmwares() {
    print_info "Available firmware structure:"
    echo ""
    cd "$SERVER_DIR"
    tree -L 2 2>/dev/null || find . -maxdepth 2 -name "*.bin" -exec ls -lh {} \;
    echo ""
}

check_port() {
    if lsof -Pi :$PORT -sTCP:LISTEN -t >/dev/null 2>&1 ; then
        print_error "Port $PORT is already in use"
        print_info "Processes using the port:"
        lsof -i :$PORT
        read -p "Do you want to kill the process? (y/n): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            sudo kill -9 $(lsof -t -i:$PORT)
            print_step "Process terminated"
        else
            print_error "Cannot start server. Exiting..."
            exit 1
        fi
    fi
}

start_server() {
    print_info "Starting OTA Server via manage.py..."
    print_info "Base URL: http://$HOSTNAME.local:$PORT"
    print_info "Current IP: $(hostname -I | awk '{print $1}')"
    print_info ""
    print_info "Press Ctrl+C to stop the server"
    print_info "Stop DNS: sudo systemctl stop avahi-daemon"
    print_info "Disable: sudo systemctl disable avahi-daemon"
    echo ""
    
    python3 "$SCRIPT_DIR/manage.py" serve --port $PORT
}

show_help() {
    echo "Usage: $0 [option]"
    echo ""
    echo "Options:"
    echo "  start    - Start OTA server (default)"
    echo "  setup    - Only configure hostname and check dependencies"
    echo "  list     - List available firmwares"
    echo "  close    - Close avahi DNS deamon"
    echo "  help     - Show this help"
    echo ""
}

close_avahi() {
    sudo systemctl stop avahi-daemon
    sudo systemctl disable avahi-daemon
}

# ========== MAIN ==========

case "${1:-start}" in
    setup)
        print_info "=== OTA Server Setup ==="
        check_avahi
        setup_hostname
        check_server_dir
        print_step "Setup completed!"
        ;;
    
    list)
        list_firmwares
        ;;
    
    start)
        shift # Consome o 'start'
        parse_args "$@"
        print_info "=== Starting OTA Server ==="
        setup_mdns
        check_port
        start_server
        ;;
        
    close)
    	print_info "=== Closing Avahi ==="
    	close_avahi
    	;;
    
    help)
        show_help
        ;;
    
    *)
        print_error "Invalid option: $1"
        show_help
        exit 1
        ;;
esac
