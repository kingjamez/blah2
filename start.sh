#!/bin/bash
# Start blah2 services
# Usage: sudo ./start.sh [config-file]

CONFIG="${1:-/opt/blah2/config/config.yml}"

if [[ ! -f "${CONFIG}" ]]; then
    echo "[ERROR] Config file not found: ${CONFIG}"
    exit 1
fi

echo "Starting blah2 (config: ${CONFIG})..."
systemctl start blah2-api
sleep 2
systemctl start blah2
systemctl --no-pager status blah2 blah2-api --lines=0
echo "blah2 started."
