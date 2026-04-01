#!/usr/bin/env bash
# =============================================================================
# blah2 bare-metal watchdog
#
# Drop-in replacement for script/blah2_rspduo_restart.bash that works without
# Docker. Intended to be run every 5 minutes via cron (see install.sh).
#
# Logic:
#   1. Query the Node.js API for the latest map data.
#   2. If the response is not valid JSON, or the embedded timestamp is more
#      than 60 seconds old, the pipeline has stalled.
#   3. On a stall: kill the sdrplay_apiService daemon (it can wedge after a
#      USB glitch), restart the sdrplay systemd service, then restart both
#      blah2 services.
# =============================================================================

set -uo pipefail

API_URL="http://127.0.0.1:3000/api/map"
MAX_AGE_SECONDS=60
LOG_PREFIX="[blah2-watchdog] $(date '+%Y-%m-%d %H:%M:%S')"

# ── Fetch the latest map response ─────────────────────────────────────────────
RESPONSE=$(curl -sf --max-time 5 "${API_URL}" 2>/dev/null || true)

# ── Check 1: Is the response non-empty and JSON? ──────────────────────────────
FIRST_CHAR="${RESPONSE:0:1}"
if [[ "${FIRST_CHAR}" != "{" ]]; then
    echo "${LOG_PREFIX} API returned non-JSON or empty response — restarting."
    need_restart=true
else
    # ── Check 2: Is the embedded Unix timestamp recent enough? ────────────────
    # The map JSON starts with: {"timestamp":1234567890,...}
    # Extract the 10-digit epoch value from the first 30 characters.
    TIMESTAMP=$(echo "${RESPONSE}" | head -c 30 | grep -oP '"timestamp":\K[0-9]+' || true)
    CURR_TIMESTAMP=$(date +%s)

    if [[ -z "${TIMESTAMP}" ]]; then
        echo "${LOG_PREFIX} Could not parse timestamp from API response — restarting."
        need_restart=true
    else
        DIFF=$(( CURR_TIMESTAMP - TIMESTAMP ))
        if [[ ${DIFF} -gt ${MAX_AGE_SECONDS} ]]; then
            echo "${LOG_PREFIX} Data is ${DIFF}s old (max ${MAX_AGE_SECONDS}s) — restarting."
            need_restart=true
        else
            echo "${LOG_PREFIX} OK — data is ${DIFF}s old."
            need_restart=false
        fi
    fi
fi

# ── Restart sequence ──────────────────────────────────────────────────────────
if [[ "${need_restart:-false}" == "true" ]]; then
    echo "${LOG_PREFIX} Stopping blah2 services..."
    systemctl stop blah2.service blah2-api.service || true

    # Kill any wedged sdrplay_apiService processes (mirrors the original script).
    if pgrep -f "sdrplay_apiService" > /dev/null; then
        echo "${LOG_PREFIX} Killing sdrplay_apiService..."
        kill -9 "$(pgrep -f sdrplay_apiService)" || true
    fi

    # Restart the SDRplay daemon if the service unit is present.
    if systemctl list-unit-files sdrplay.service &>/dev/null; then
        echo "${LOG_PREFIX} Restarting sdrplay.service..."
        systemctl restart sdrplay.service
        # Give the hardware a moment to enumerate on USB.
        sleep 3
    fi

    echo "${LOG_PREFIX} Starting blah2 services..."
    systemctl start blah2-api.service
    # Small delay so the API TCP listeners are up before the C++ process
    # tries to connect on the first CPI.
    sleep 2
    systemctl start blah2.service

    echo "${LOG_PREFIX} Restart complete."
fi
