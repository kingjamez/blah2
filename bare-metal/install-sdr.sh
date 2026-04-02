#!/usr/bin/env bash
# =============================================================================
# blah2 headless node — SDR driver installer (Step 2 of 2)
# Tested on: Ubuntu 22.04 LTS (x86_64 and aarch64 / Raspberry Pi)
#
# Run from the root of the blah2 repo AFTER install.sh:
#   sudo ./bare-metal/install-sdr.sh
#
# Installs SDR drivers based on your hardware selection, then builds
# the blah2 headless binary and deploys it to /opt/blah2/bin/.
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

[[ -z "${RAW_INPUT// }" ]] && RAW_INPUT="a"

for TOKEN in ${RAW_INPUT}; do
    case "${TOKEN,,}" in
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
    error "No valid hardware selected."
echo ""
read -rp " Proceed? [Y/n] " CONFIRM
[[ "${CONFIRM,,}" == "n" ]] && exit 0

# =============================================================================
# SDR install functions (identical to full blah2 — shared logic)
# =============================================================================
_ensure_uhd_ppa() {
    # PPAs only work on Ubuntu; Debian/Raspbian use standard repos
    if [[ -f /etc/os-release ]] && grep -qi 'ubuntu' /etc/os-release; then
        if ! grep -r "ettusresearch" /etc/apt/sources.list \
             /etc/apt/sources.list.d/ &>/dev/null; then
            info "  Adding Ettus Research PPA (Ubuntu detected)..."
            apt-get install -y software-properties-common
            add-apt-repository -y ppa:ettusresearch/uhd
            apt-get update
        fi
    fi
}

install_hackrf() {
    section "HackRF driver"
    apt-get install -y libhackrf-dev
    info "  HackRF library installed."
}

install_sdrplay() {
    section "SDRplay API"
    local MAJVER="3.15" MINVER="2"
    local VER="${MAJVER}.${MINVER}"
    local SDRPLAY_DIR="${REPO_DIR}/lib/sdrplay-${VER}"
    local SDRPLAY_RUN="${SDRPLAY_DIR}/SDRplay_RSP_API-Linux-${VER}.run"
    [[ ! -f "${SDRPLAY_RUN}" ]] && error "SDRplay installer not found at ${SDRPLAY_RUN}"

    local SDRPLAY_ARCH
    if   [[ "${ARCH}" == "x86_64"  ]]; then SDRPLAY_ARCH="amd64"
    elif [[ "${ARCH}" == "aarch64" ]]; then SDRPLAY_ARCH="arm64"
    else error "Unsupported architecture: ${ARCH}"; fi

    chmod +x "${SDRPLAY_RUN}"
    "${SDRPLAY_RUN}" --tar -xvf -C "${SDRPLAY_DIR}"
    cp "${SDRPLAY_DIR}/${SDRPLAY_ARCH}/libsdrplay_api.so.${MAJVER}" /usr/local/lib/libsdrplay_api.so
    cp "${SDRPLAY_DIR}/${SDRPLAY_ARCH}/libsdrplay_api.so.${MAJVER}" "/usr/local/lib/libsdrplay_api.so.${MAJVER}"
    cp "${SDRPLAY_DIR}"/inc/* /usr/local/include/
    chmod 644 /usr/local/lib/libsdrplay_api.so "/usr/local/lib/libsdrplay_api.so.${MAJVER}"
    ldconfig
    info "  SDRplay API library installed."

    echo -e "${YELLOW}  Run the .run installer interactively to install sdrplay_apiService.${NC}"
}

install_rtlsdr() {
    section "RTL-SDR / KrakenRF (librtlsdr)"
    local LIBRTLSDR_DIR="/opt/librtlsdr"
    if [[ ! -d "${LIBRTLSDR_DIR}" ]]; then
        git clone https://github.com/krakenrf/librtlsdr "${LIBRTLSDR_DIR}"
    fi
    mkdir -p "${LIBRTLSDR_DIR}/build"
    cmake -S "${LIBRTLSDR_DIR}" -B "${LIBRTLSDR_DIR}/build" \
          -DINSTALL_UDEV_RULES=ON -DDETACH_KERNEL_DRIVER=ON
    make -C "${LIBRTLSDR_DIR}/build" -j"$(nproc)"
    make -C "${LIBRTLSDR_DIR}/build" install
    ldconfig
    udevadm control --reload-rules && udevadm trigger
    info "  librtlsdr installed."
}

install_usrp() {
    section "Ettus UHD (USRP)"
    _ensure_uhd_ppa
    DEBIAN_FRONTEND=noninteractive apt-get install -y libuhd-dev uhd-host
    uhd_images_downloader
    info "  UHD installed with firmware images."
}

install_usrp_stub() {
    section "Ettus UHD library (build dependency — no USRP hardware selected)"
    _ensure_uhd_ppa
    DEBIAN_FRONTEND=noninteractive apt-get install -y libuhd-dev uhd-host
    info "  libuhd-dev installed (firmware images skipped)."
}

# =============================================================================
# Run installs
# =============================================================================
section "Installing SDR drivers"

# HackRF — always trivial, just an apt package
install_hackrf
# SDRplay — always need the .so for linking
install_sdrplay
# RTL-SDR — always build from source (krakenrf fork)
install_rtlsdr
# USRP — full install or stub depending on selection
if ${DO_USRP}; then install_usrp; else install_usrp_stub; fi

# =============================================================================
# Build blah2 headless binary
# =============================================================================
section "Building blah2 (headless)"

VCPKG_PREFIX=$(echo "${REPO_DIR}"/lib/vcpkg_installed/*/share 2>/dev/null | tr ' ' '\n' | head -1)
[[ -z "${VCPKG_PREFIX}" || ! -d "${VCPKG_PREFIX}" ]] && \
    error "vcpkg_installed not found. Did you run install.sh first?"

# Patch rapidjson GCC 14 bug: GenericStringRef::operator= assigns to const member.
# rapidjson 1.1.0 has no fix release; patch the installed header in-place.
RJDOC=$(find "${REPO_DIR}/lib/vcpkg_installed" -path '*/rapidjson/document.h' 2>/dev/null | head -1)
if [[ -n "${RJDOC}" ]] && grep -q 'length = rhs.length' "${RJDOC}"; then
    info "Patching rapidjson document.h for GCC 14 compatibility..."
    sed -i 's/length = rhs.length/const_cast<SizeType\&>(length) = rhs.length/' "${RJDOC}"
fi

cd "${REPO_DIR}"
mkdir -p build
cmake -S . --preset prod-release -DCMAKE_PREFIX_PATH="${VCPKG_PREFIX}"
cmake --build --preset prod-release -- -j"$(nproc)"
chmod +x "${REPO_DIR}/bin/blah2"

[[ "${REPO_DIR}/bin/blah2" != "${INSTALL_DIR}/bin/blah2" ]] && \
    cp "${REPO_DIR}/bin/blah2" "${INSTALL_DIR}/bin/blah2"
info "  blah2 binary built and deployed to ${INSTALL_DIR}/bin/blah2"

# =============================================================================
# Done
# =============================================================================
echo ""
echo -e "${GREEN}============================================================${NC}"
echo -e "${GREEN} blah2 headless node installation complete!${NC}"
echo ""
echo " Edit your config first:"
echo "   nano ${INSTALL_DIR}/config/config.yml"
echo "   (set network.ip to your aggregator's address)"
echo ""
echo " Start the node:"
echo "   sudo systemctl start blah2"
echo ""
echo " View logs:"
echo "   journalctl -fu blah2"
echo ""
echo -e "${GREEN}============================================================${NC}"
