#!/usr/bin/env bash
# =============================================================================
# blah2 headless node — bare-metal installer (Step 1 of 2)
# Tested on: Ubuntu 22.04 LTS (x86_64 and aarch64 / Raspberry Pi)
#
# Run from the root of the blah2 repo:
#   sudo ./bare-metal/install.sh
#
# This script installs build tools and C++ dependencies.
# After this, run install-sdr.sh to install SDR drivers and build.
# =============================================================================

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

[[ $EUID -ne 0 ]] && error "This script must be run as root (use sudo)."

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
INSTALL_DIR="/opt/blah2"

info "Repo directory : ${REPO_DIR}"
info "Install target : ${INSTALL_DIR}"
info "Architecture   : $(uname -m)"

# =============================================================================
# STEP 1 — System build tools
# =============================================================================
info "Step 1: Installing system packages..."

apt-get update
DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get install -y \
    g++ make cmake git curl zip unzip \
    pkg-config gfortran \
    libfftw3-dev libfftw3-bin \
    libusb-dev libusb-1.0-0-dev \
    liblapack-dev libblas-dev \
    rsync

apt-get autoremove -y
apt-get clean -y
info "  System packages installed."

# =============================================================================
# STEP 2 — vcpkg and C++ library dependencies
# =============================================================================
info "Step 2: Installing vcpkg and C++ dependencies..."

VCPKG_ROOT="/opt/vcpkg"
export VCPKG_ROOT
export PATH="${VCPKG_ROOT}:${PATH}"

if [[ ! -d "${VCPKG_ROOT}" ]]; then
    git clone https://github.com/microsoft/vcpkg "${VCPKG_ROOT}"
fi

[[ "$(uname -m)" == "aarch64" ]] && export VCPKG_FORCE_SYSTEM_BINARIES=1

if [[ ! -f "${VCPKG_ROOT}/vcpkg" ]]; then
    "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
fi

cd "${REPO_DIR}/lib"
vcpkg integrate install
vcpkg install --clean-after-build
info "  vcpkg dependencies installed."

# =============================================================================
# STEP 3 — Deploy config and create directories
# =============================================================================
info "Step 3: Setting up ${INSTALL_DIR}..."

mkdir -p "${INSTALL_DIR}/bin"
rsync -a --delete "${REPO_DIR}/config/" "${INSTALL_DIR}/config/"
info "  Config deployed."

# =============================================================================
# STEP 4 — systemd unit file
# =============================================================================
info "Step 4: Installing systemd unit..."

cat > /etc/systemd/system/blah2.service << 'UNIT'
[Unit]
Description=blah2 headless passive radar node
After=network.target sdrplay.service
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
systemctl enable blah2.service
info "  systemd unit installed and enabled."

# =============================================================================
# Done
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
