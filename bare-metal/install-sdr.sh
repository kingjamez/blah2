#!/usr/bin/env bash
# =============================================================================
# blah2 SDR driver installer — Step 2 of 2
# Tested on: Ubuntu 22.04 LTS (x86_64 and aarch64 / Raspberry Pi)
#
# Run from the root of the blah2 repo AFTER install.sh:
#   sudo ./bare-metal/install-sdr.sh
#
# This script:
#   1. Asks which SDR hardware you have
#   2. Installs the appropriate driver for each selected SDR
#   3. Installs stub libraries for any unselected SDRs so the build succeeds
#      (blah2's CMakeLists.txt links against all four SDR libraries regardless
#      of which hardware is configured — selecting only your hardware here
#      skips optional heavy steps like USRP firmware downloads, but the
#      libraries themselves are always installed for compilation)
#   4. Builds the blah2 binary with cmake
#   5. Deploys the binary to /opt/blah2/bin/
# =============================================================================

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()    { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }
section() { echo -e "\n${CYAN}──────────────────────────────────────────────${NC}"; \
             echo -e "${CYAN} $*${NC}"; \
             echo -e "${CYAN}──────────────────────────────────────────────${NC}"; }

[[ $EUID -ne 0 ]] && error "This script must be run as root (use sudo)."

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
INSTALL_DIR="/opt/blah2"
ARCH=$(uname -m)

VCPKG_ROOT="/opt/vcpkg"
export VCPKG_ROOT
export PATH="${VCPKG_ROOT}:${PATH}"
[[ "${ARCH}" == "aarch64" ]] && export VCPKG_FORCE_SYSTEM_BINARIES=1

# Flags set by the menu — default all to false
DO_HACKRF=false
DO_SDRPLAY=false
DO_RTLSDR=false
DO_USRP=false
DO_USRP_FIRMWARE=false

# =============================================================================
# MENU
# =============================================================================
section "SDR hardware selection"

echo ""
echo " blah2 supports four SDR receiver types."
echo " Select the hardware you have so the appropriate drivers are fully"
echo " set up. All four SDR libraries will be installed regardless of your"
echo " selection — this is required by the blah2 build system."
echo " Your selection controls which optional extras run (e.g. the USRP"
echo " firmware image download is ~500 MB and skipped if you have no USRP)."
echo ""
echo "  1)  HackRF One"
echo "  2)  SDRplay RSPduo / RSP1A / RSP2 / RSPdx"
echo "  3)  RTL-SDR / KrakenRF (coherent multi-channel)"
echo "  4)  Ettus USRP  (B200, B210, N2xx, X3xx ...)"
echo "      Note: also downloads ~500 MB of FPGA firmware images"
echo "  a)  All of the above"
echo ""
echo " Enter your choice(s) separated by spaces — e.g.  1  for HackRF only,"
echo "   2 3  for SDRplay + RTL-SDR,  1 2 4  for all but KrakenRF, etc."
echo " Press Enter with no input to install all:"
echo ""
read -rp " > " RAW_INPUT

# Default to all if the user just pressed Enter
[[ -z "${RAW_INPUT// }" ]] && RAW_INPUT="a"

for TOKEN in ${RAW_INPUT}; do
    case "${TOKEN,,}" in   # ,, = lowercase
        1) DO_HACKRF=true ;;
        2) DO_SDRPLAY=true ;;
        3) DO_RTLSDR=true ;;
        4) DO_USRP=true; DO_USRP_FIRMWARE=true ;;
        a) DO_HACKRF=true; DO_SDRPLAY=true; DO_RTLSDR=true
           DO_USRP=true; DO_USRP_FIRMWARE=true ;;
        *) warn "Unrecognised option '${TOKEN}' — ignored." ;;
    esac
done

echo ""
echo " Selected:"
${DO_HACKRF}  && echo "   HackRF One"
${DO_SDRPLAY} && echo "   SDRplay RSP"
${DO_RTLSDR}  && echo "   RTL-SDR / KrakenRF"
${DO_USRP}    && echo "   Ettus USRP  (with firmware download)"
! ${DO_HACKRF} && ! ${DO_SDRPLAY} && ! ${DO_RTLSDR} && ! ${DO_USRP} && \
    error "No valid hardware selected. Please re-run the script."
echo ""
read -rp " Proceed? [Y/n] " CONFIRM
[[ "${CONFIRM,,}" == "n" ]] && exit 0

# =============================================================================
# HELPER: Add the Ettus UHD PPA (needed for UHD 4.9.0.0 on Ubuntu)
# =============================================================================
_ensure_uhd_ppa() {
    if ! grep -r "ettusresearch" /etc/apt/sources.list \
         /etc/apt/sources.list.d/ &>/dev/null; then
        info "  Adding Ettus Research PPA..."
        apt-get install -y software-properties-common
        add-apt-repository -y ppa:ettusresearch/uhd
        apt-get update
    fi
}

# =============================================================================
# DRIVER 1 — HackRF
# =============================================================================
install_hackrf() {
    section "HackRF driver"
    info "Installing libhackrf-dev..."
    apt-get install -y libhackrf-dev
    info "  HackRF library installed."
}

install_hackrf_stub() {
    # libhackrf-dev is a standard apt package — always trivially fast.
    # Install it even if the user has no HackRF so the linker is satisfied.
    section "HackRF library (build dependency — no HackRF hardware selected)"
    info "Installing libhackrf-dev (required for compilation)..."
    apt-get install -y libhackrf-dev
    info "  libhackrf-dev installed."
}

# =============================================================================
# DRIVER 2 — SDRplay API
# =============================================================================
install_sdrplay() {
    section "SDRplay API"
    local MAJVER="3.15"
    local MINVER="2"
    local VER="${MAJVER}.${MINVER}"
    local SDRPLAY_DIR="${REPO_DIR}/lib/sdrplay-${VER}"
    local SDRPLAY_RUN="${SDRPLAY_DIR}/SDRplay_RSP_API-Linux-${VER}.run"

    [[ ! -f "${SDRPLAY_RUN}" ]] && \
        error "SDRplay installer not found at ${SDRPLAY_RUN}"

    # Map uname -m to the subdirectory name inside the .run archive
    local SDRPLAY_ARCH
    if   [[ "${ARCH}" == "x86_64"  ]]; then SDRPLAY_ARCH="amd64"
    elif [[ "${ARCH}" == "aarch64" ]]; then SDRPLAY_ARCH="aarch64"
    else error "Unsupported architecture for SDRplay: ${ARCH}"; fi

    info "Extracting SDRplay API ${VER}..."
    chmod +x "${SDRPLAY_RUN}"
    "${SDRPLAY_RUN}" --tar -xvf -C "${SDRPLAY_DIR}"

    cp "${SDRPLAY_DIR}/${SDRPLAY_ARCH}/libsdrplay_api.so.${MAJVER}" \
        /usr/local/lib/libsdrplay_api.so
    cp "${SDRPLAY_DIR}/${SDRPLAY_ARCH}/libsdrplay_api.so.${MAJVER}" \
        "/usr/local/lib/libsdrplay_api.so.${MAJVER}"
    cp "${SDRPLAY_DIR}"/inc/* /usr/local/include/
    chmod 644 /usr/local/lib/libsdrplay_api.so \
              "/usr/local/lib/libsdrplay_api.so.${MAJVER}"
    ldconfig

    info "  SDRplay API library installed."
    echo ""
    echo -e "${YELLOW}  ┌─ SDRplay service daemon ─────────────────────────────────────┐${NC}"
    echo -e "${YELLOW}  │  The sdrplay_apiService background daemon is NOT installed    │${NC}"
    echo -e "${YELLOW}  │  by this script — its installer is interactive only.          │${NC}"
    echo -e "${YELLOW}  │                                                                │${NC}"
    echo -e "${YELLOW}  │  To install it, run this once on the target machine:          │${NC}"
    echo -e "${YELLOW}  │    ${SDRPLAY_RUN}  │${NC}"
    echo -e "${YELLOW}  │  Then: systemctl enable --now sdrplay.service                 │${NC}"
    echo -e "${YELLOW}  └────────────────────────────────────────────────────────────────┘${NC}"
    echo ""
}

# =============================================================================
# DRIVER 3 — RTL-SDR (KrakenRF fork)
# =============================================================================
install_rtlsdr() {
    section "RTL-SDR / KrakenRF (librtlsdr)"
    local LIBRTLSDR_DIR="/opt/librtlsdr"

    if [[ ! -d "${LIBRTLSDR_DIR}" ]]; then
        info "Cloning KrakenRF librtlsdr..."
        git clone https://github.com/krakenrf/librtlsdr "${LIBRTLSDR_DIR}"
    else
        info "  ${LIBRTLSDR_DIR} already exists — skipping clone."
    fi

    info "Building librtlsdr..."
    mkdir -p "${LIBRTLSDR_DIR}/build"
    cmake -S "${LIBRTLSDR_DIR}" \
          -B "${LIBRTLSDR_DIR}/build" \
          -DINSTALL_UDEV_RULES=ON \
          -DDETACH_KERNEL_DRIVER=ON
    make -C "${LIBRTLSDR_DIR}/build" -j"$(nproc)"
    make -C "${LIBRTLSDR_DIR}/build" install
    ldconfig
    udevadm control --reload-rules && udevadm trigger
    info "  librtlsdr installed."
}

install_rtlsdr_stub() {
    # Build the KrakenRF librtlsdr even without KrakenRF hardware; the linker
    # needs librtlsdr.so regardless. The standard kernel rtl2832 module won't
    # cause problems as long as the udev rules from -DDETACH_KERNEL_DRIVER=ON
    # are in place.
    section "RTL-SDR library (build dependency — no KrakenRF hardware selected)"
    install_rtlsdr
}

# =============================================================================
# DRIVER 4 — Ettus UHD
# =============================================================================
install_usrp() {
    section "Ettus UHD (USRP)"
    _ensure_uhd_ppa
    info "Installing libuhd-dev and uhd-host (pinned to 4.9.0.0)..."
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        libuhd-dev=4.9.0.0-0ubuntu1~jammy3 \
        uhd-host=4.9.0.0-0ubuntu1~jammy3
    info "Downloading UHD FPGA/firmware images (~500 MB)..."
    uhd_images_downloader
    info "  UHD installed with firmware images."
}

install_usrp_stub() {
    # Install the UHD library (needed by the linker and by find_package(UHD)
    # in CMakeLists.txt) but skip the large firmware download since no USRP
    # hardware is present.
    section "Ettus UHD library (build dependency — no USRP hardware selected)"
    _ensure_uhd_ppa
    info "Installing libuhd-dev (required for compilation, no firmware download)..."
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        libuhd-dev=4.9.0.0-0ubuntu1~jammy3 \
        uhd-host=4.9.0.0-0ubuntu1~jammy3
    info "  libuhd-dev installed (firmware images skipped)."
}

# =============================================================================
# Run selected installs (full) and unselected installs (stubs)
# =============================================================================
section "Installing SDR drivers"

if ${DO_HACKRF};  then install_hackrf;      else install_hackrf_stub;  fi
if ${DO_SDRPLAY}; then install_sdrplay;     else install_sdrplay;      fi  # always installs library
if ${DO_RTLSDR};  then install_rtlsdr;      else install_rtlsdr_stub;  fi
if ${DO_USRP};    then install_usrp;        else install_usrp_stub;    fi

# =============================================================================
# Build blah2 binary
# =============================================================================
section "Building blah2"

# Locate the vcpkg-installed package share directory for cmake prefix path
VCPKG_PREFIX=$(echo "${REPO_DIR}"/lib/vcpkg_installed/*/share 2>/dev/null | tr ' ' '\n' | head -1)
[[ -z "${VCPKG_PREFIX}" || ! -d "${VCPKG_PREFIX}" ]] && \
    error "vcpkg_installed not found. Did you run install.sh first?"

info "Running cmake (preset: prod-release)..."
cd "${REPO_DIR}"
mkdir -p build

cmake -S . --preset prod-release \
    -DCMAKE_PREFIX_PATH="${VCPKG_PREFIX}"

cmake --build --preset prod-release -- -j"$(nproc)"
chmod +x "${REPO_DIR}/bin/blah2"

# Deploy the freshly built binary to the install location
cp "${REPO_DIR}/bin/blah2" "${INSTALL_DIR}/bin/blah2"
info "  blah2 binary built and deployed to ${INSTALL_DIR}/bin/blah2"

# =============================================================================
# Done
# =============================================================================
echo ""
echo -e "${GREEN}============================================================${NC}"
echo -e "${GREEN} blah2 installation complete!${NC}"
echo ""
echo " Start the system:"
echo "   sudo systemctl start blah2-api blah2"
echo ""
echo " View logs:"
echo "   journalctl -fu blah2"
echo "   journalctl -fu blah2-api"
echo ""
echo " Web UI:  http://$(hostname -I | awk '{print $1}')"
echo ""

if ${DO_SDRPLAY}; then
echo -e "${YELLOW} Reminder: run the SDRplay .run installer interactively to${NC}"
echo -e "${YELLOW} install sdrplay_apiService, then enable the service:${NC}"
echo -e "${YELLOW}   systemctl enable --now sdrplay${NC}"
echo ""
fi

echo -e "${GREEN}============================================================${NC}"
