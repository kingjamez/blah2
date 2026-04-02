#!/usr/bin/env bash
# =============================================================================
# blah2 bare-metal installer — Step 1 of 2
# Tested on: Ubuntu 22.04 LTS (x86_64 and aarch64 / Raspberry Pi)
#
# Run from the root of the blah2 repo:
#   sudo ./bare-metal/install.sh
#
# This script installs everything EXCEPT SDR drivers and the blah2 binary
# (those are handled by install-sdr.sh which must be run after this one).
#
# What this script does:
#   1. Installs system build tools and non-SDR runtime packages
#   2. Installs vcpkg and builds all C++ library dependencies
#   3. Installs Node.js 20.x and API middleware dependencies
#   4. Deploys runtime files to /opt/blah2
#   5. Installs and configures nginx for the web frontend
#   6. Installs systemd units and the watchdog script
#
# After this script completes, run:
#   sudo ./bare-metal/install-sdr.sh
# =============================================================================

set -euo pipefail

# ── Colour helpers ────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

# ── Sanity checks ─────────────────────────────────────────────────────────────
[[ $EUID -ne 0 ]] && error "This script must be run as root (use sudo)."

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
INSTALL_DIR="/opt/blah2"

info "Repo directory : ${REPO_DIR}"
info "Install target : ${INSTALL_DIR}"
info "Architecture   : $(uname -m)"

# =============================================================================
# STEP 1 — System build tools and non-SDR runtime packages
# =============================================================================
info "Step 1: Installing system packages..."

apt-get update
DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get install -y \
    g++ make cmake git curl zip unzip \
    pkg-config gfortran \
    libfftw3-dev libfftw3-bin \
    libusb-dev libusb-1.0-0-dev \
    liblapack-dev libblas-dev \
    libarmadillo-dev libopenblas-dev \
    rsync \
    nginx \
    nodejs npm

apt-get autoremove -y
apt-get clean -y
info "  System packages installed."

# =============================================================================
# STEP 2 — vcpkg and C++ library dependencies
# =============================================================================
info "Step 2: Installing vcpkg and C++ dependencies..."

VCPKG_ROOT="/opt/vcpkg"
export VCPKG_ROOT

if [[ ! -d "${VCPKG_ROOT}" ]]; then
    git clone https://github.com/microsoft/vcpkg "${VCPKG_ROOT}"
fi

export PATH="${VCPKG_ROOT}:${PATH}"

# aarch64 (Raspberry Pi) cannot run vcpkg's bundled x86_64 cmake/ninja
# binaries — force it to use whatever the system provides instead.
[[ "$(uname -m)" == "aarch64" ]] && export VCPKG_FORCE_SYSTEM_BINARIES=1

if [[ ! -f "${VCPKG_ROOT}/vcpkg" ]]; then
    "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
fi

cd "${REPO_DIR}/lib"
vcpkg integrate install
vcpkg install --clean-after-build

info "  vcpkg dependencies installed."

# =============================================================================
# STEP 3 — Node.js 20.x and API middleware dependencies
# =============================================================================
info "Step 3: Installing Node.js and API dependencies..."

if ! node --version 2>/dev/null | grep -qE "^v(18|19|20|21|22)"; then
    info "  Fetching NodeSource setup script for Node.js 20.x..."
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
    apt-get install -y nodejs
fi

info "  Node.js $(node --version) ready."

cd "${REPO_DIR}/api"
npm install
info "  Node.js API dependencies installed."

# =============================================================================
# STEP 4 — Deploy runtime files to /opt/blah2
# =============================================================================
info "Step 4: Deploying runtime files to ${INSTALL_DIR}..."

# NOTE: The bin/ directory is NOT deployed here because the blah2 binary has
# not been built yet — that happens at the end of install-sdr.sh once all SDR
# libraries are available for the linker.
mkdir -p "${INSTALL_DIR}/bin"
mkdir -p "${INSTALL_DIR}/save"

rsync -a --delete "${REPO_DIR}/config/"  "${INSTALL_DIR}/config/"
rsync -a --delete "${REPO_DIR}/html/"    "${INSTALL_DIR}/html/"
rsync -a --delete "${REPO_DIR}/api/"     "${INSTALL_DIR}/api/"

info "  Config, HTML, and API deployed."

# =============================================================================
# STEP 5 — nginx configuration for the web frontend
# =============================================================================
info "Step 5: Configuring nginx..."

# Serves the static HTML frontend directly and proxies API calls through to
# the Node.js middleware on port 3000.
cat > /etc/nginx/sites-available/blah2 << 'NGINX_CONF'
server {
    listen 80 default_server;
    listen [::]:80 default_server;

    root /opt/blah2/html;
    index index.html;

    # Static frontend files
    location / {
        try_files $uri $uri/ =404;
    }

    # Proxy REST API, stash, and maxhold endpoints to Node.js
    location ~ ^/(api|stash|maxhold)/(.*) {
        proxy_pass         http://127.0.0.1:3000/$1/$2;
        proxy_http_version 1.1;
        proxy_set_header   Connection "";
        proxy_set_header   Host $host;
        proxy_connect_timeout 2s;
        add_header         Cache-Control "no-store";
    }

    location = /capture {
        proxy_pass http://127.0.0.1:3000/capture;
    }

    location = /capture/toggle {
        proxy_pass http://127.0.0.1:3000/capture/toggle;
    }
}
NGINX_CONF

rm -f /etc/nginx/sites-enabled/default
ln -sf /etc/nginx/sites-available/blah2 /etc/nginx/sites-enabled/blah2

nginx -t
systemctl enable nginx
systemctl restart nginx
info "  nginx configured and restarted."

# =============================================================================
# STEP 6 — systemd unit files and watchdog
# =============================================================================
info "Step 6: Installing systemd units and watchdog..."

# blah2-api — Node.js middleware (must be running before blah2 starts,
# because the C++ process connects to the API's TCP listener ports on startup)
cat > /etc/systemd/system/blah2-api.service << 'UNIT'
[Unit]
Description=blah2 API middleware (Node.js)
After=network.target sdrplay.service
Wants=sdrplay.service

[Service]
Type=simple
User=root
WorkingDirectory=/opt/blah2/api
ExecStart=/usr/bin/node /opt/blah2/api/server.js /opt/blah2/config/config.yml
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal
SyslogIdentifier=blah2-api

[Install]
WantedBy=multi-user.target
UNIT

# blah2 — C++ radar processor
cat > /etc/systemd/system/blah2.service << 'UNIT'
[Unit]
Description=blah2 passive radar processor
After=blah2-api.service sdrplay.service
Requires=blah2-api.service
Wants=sdrplay.service

[Service]
Type=simple
User=root
WorkingDirectory=/opt/blah2
ExecStart=/opt/blah2/bin/blah2 -c /opt/blah2/config/config.yml
Restart=on-failure
RestartSec=5
PrivateDevices=no
StandardOutput=journal
StandardError=journal
SyslogIdentifier=blah2

[Install]
WantedBy=multi-user.target
UNIT

systemctl daemon-reload
systemctl enable blah2-api.service blah2.service
info "  systemd units installed and enabled."

# Install watchdog script
mkdir -p "${INSTALL_DIR}/script"
cp "${SCRIPT_DIR}/blah2-watchdog.sh" "${INSTALL_DIR}/script/blah2-watchdog.sh"
chmod +x "${INSTALL_DIR}/script/blah2-watchdog.sh"

CRON_ENTRY="*/5 * * * * root ${INSTALL_DIR}/script/blah2-watchdog.sh >> /var/log/blah2-watchdog.log 2>&1"
if ! grep -qF "blah2-watchdog.sh" /etc/crontab 2>/dev/null; then
    echo "${CRON_ENTRY}" >> /etc/crontab
    info "  Watchdog cron entry added."
else
    info "  Watchdog cron entry already present — skipped."
fi

# =============================================================================
# Done — prompt for next step
# =============================================================================
echo ""
echo -e "${GREEN}============================================================${NC}"
echo -e "${GREEN} Step 1 complete.${NC}"
echo ""
echo " Next: install SDR drivers and build the blah2 binary:"
echo ""
echo -e "   ${YELLOW}sudo ${SCRIPT_DIR}/install-sdr.sh${NC}"
echo ""
echo -e "${GREEN}============================================================${NC}"
