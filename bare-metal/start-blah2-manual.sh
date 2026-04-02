#!/bin/bash
# start-blah2-manual.sh — Start blah2 in foreground (for debugging)
# Usage: bash start-blah2-manual.sh [config-file]
# Ctrl+C to stop blah2; the API background process is cleaned up on exit.

CONFIG="${1:-/opt/blah2/config/config.yml}"

if [[ ! -f "${CONFIG}" ]]; then
    echo "[ERROR] Config file not found: ${CONFIG}"
    exit 1
fi

# Clean up API on exit
cleanup() {
    echo ""
    echo "Stopping API..."
    kill "${API_PID}" 2>/dev/null
    echo "Done."
}
trap cleanup EXIT

echo "Starting API (background)..."
node /opt/blah2/api/server.js "${CONFIG}" &
API_PID=$!
sleep 2

echo "Starting blah2 (foreground, Ctrl+C to stop)..."
/opt/blah2/bin/blah2 -c "${CONFIG}"
