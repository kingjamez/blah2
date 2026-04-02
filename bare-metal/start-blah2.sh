#!/bin/bash
# start-blah2.sh — Start blah2 services
# Usage: sudo bash start-blah2.sh [config-file]

CONFIG="${1:-/opt/blah2/config/config.yml}"

if [[ ! -f "${CONFIG}" ]]; then
    echo "[ERROR] Config file not found: ${CONFIG}"
    exit 1
fi

echo "Starting blah2 (config: ${CONFIG})..."
systemctl start blah2-api
sleep 2
systemctl start blah2
echo "blah2 started."
