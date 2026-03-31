#!/bin/bash
# blah2 container entrypoint
# Starts the SDRplay API service daemon before launching blah2.
# Required because sdrplay_api_Open() connects to sdrplay_apiService via
# a local socket — without it running, blah2 exits immediately with
# "API open failed".

set -e

# Start the SDRplay API service in the background
if command -v sdrplay_apiService &>/dev/null; then
    echo "[entrypoint] Starting sdrplay_apiService..."
    sdrplay_apiService &
    SDRPLAY_PID=$!

    # Wait for the service to be ready (it opens a TCP socket on port 5025)
    for i in $(seq 1 20); do
        if grep -q "5025" /proc/net/tcp6 2>/dev/null || \
           grep -q "5025" /proc/net/tcp  2>/dev/null; then
            echo "[entrypoint] sdrplay_apiService ready."
            break
        fi
        sleep 0.5
    done
else
    echo "[entrypoint] WARNING: sdrplay_apiService not found — RSPduo will not work."
fi

# Hand off to the blah2 command (passed as arguments to this script)
exec "$@"
