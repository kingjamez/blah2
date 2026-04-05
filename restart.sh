#!/bin/bash
# Restart blah2 services
# Usage: sudo ./restart.sh [config-file]

CONFIG="${1:-/opt/blah2/config/config.yml}"

if [[ ! -f "${CONFIG}" ]]; then
    echo "[ERROR] Config file not found: ${CONFIG}"
    exit 1
fi

echo "Restarting blah2 (config: ${CONFIG})..."
systemctl stop blah2 2>/dev/null
systemctl stop blah2-api 2>/dev/null
killall blah2 2>/dev/null
kill $(lsof -ti :3000) 2>/dev/null
sleep 1

systemctl start blah2-api
sleep 2
systemctl start blah2
systemctl --no-pager status blah2 blah2-api --lines=0
echo "blah2 restarted."
