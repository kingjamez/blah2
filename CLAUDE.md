# blah2 — Project Context for Claude

## What This Is

blah2 is a real-time passive radar system. It captures IQ samples from a two-channel SDR (reference + surveillance), computes delay-Doppler ambiguity maps, runs detection and tracking, and serves results via a web frontend. The upstream repo is `github.com/30hours/blah2`; this fork is `github.com/kingjamez/blah2`.

## Owner

- Name: Jim
- GitHub: kingjamez
- Email: mcjamez@gmail.com
- Primary dev machine: Raspberry Pi running bare-metal branch
- Training machine (not this device): Ubuntu 24, RTX 5090, Threadripper 9960, 128 GB ECC

## Repository Layout

```
src/                    C++ radar processor
  blah2.cpp             Main entry point — capture loop, processing pipeline
  capture/              SDR capture drivers (RspDuo, Usrp, HackRf, Kraken)
  process/
    ambiguity/          Delay-Doppler cross-ambiguity function
    clutter/            Wiener-Hopf clutter filter
    detection/          CfarDetector1D, Centroid, Interpolate, CvDetector
    tracker/            Track initiation/maintenance/deletion
    spectrum/           Spectrum analyser
  data/                 Core data types: IqData, Map, Detection, Track
api/                    Node.js API middleware (server.js)
                        Reads blah2 TCP streams, serves REST API, proxies adsb2dd
html/                   Web frontend (vanilla JS, no build step)
  js/                   plot_map.js, plot_detection.js, etc.
config/                 YAML config files (SDR params, processing, network, truth)
bare-metal/             Install scripts for non-Docker deployment
  install.sh            Step 1: system packages, vcpkg, Node.js, nginx, systemd units
  install-sdr.sh        Step 2: SDR drivers, cmake build, binary deploy to /opt/blah2
  start-blah2.sh        Start services (blah2-api then blah2)
  stop-blah2.sh         Stop services
  blah2-watchdog.sh     Cron-based health check, auto-restart
lib/                    vcpkg manifest and SDRplay API installer
test/                   Catch2 unit tests
CMakeLists.txt          Build system (vcpkg toolchain via CMakePresets.json)
CMakePresets.json       Build presets: dev-debug, dev-release, prod-release
Dockerfile              Docker build (Ubuntu 22.04 base)
docker-compose.yml      Docker deployment (blah2 + api + web)
```

## Branches

| Branch | Based on | Purpose | Status |
|---|---|---|---|
| `main` | — | Docker-based deployment, upstream PRs | Stable |
| `feature/baremetal` | main | Bare-metal (no Docker) deployment for Pi/Ubuntu | Active, deployed on Jim's Pi |
| `feature/adsb-overlay` | main | Server-side ADS-B truth overlay on radar display | Merged into Jim's Pi via cherry-pick |
| `feature/cv-detection` | baremetal | CenterNet ML detector as CFAR alternative | Built, not yet compiled/tested |
| `feature/headless` | main | Stripped-down remote sensor node (no web UI) | Experimental |
| `feature/multi-node` | main | Multiple SDR instances on one host | Experimental |
| `feature/cr8-support` | main | Dragon Labs CR-8 8-channel coherent SDR | Experimental |

Jim's Pi currently runs `feature/baremetal` with `40ed151` (adsb-overlay) cherry-picked onto it.

## Build Instructions

### Docker (main branch)
```bash
sudo docker network create blah2
sudo docker compose up -d --build
```

### Bare-metal (feature/baremetal)
```bash
git checkout feature/baremetal
sudo ./bare-metal/install.sh      # packages, vcpkg, Node.js, nginx, systemd
sudo ./bare-metal/install-sdr.sh  # SDR drivers, cmake build, deploy to /opt/blah2
```

The cmake build uses presets. The install-sdr.sh script handles this, but for manual builds:
```bash
mkdir -p build && cd build
cmake -S .. --preset prod-release \
  -DCMAKE_PREFIX_PATH=$(echo /path/to/vcpkg_installed/*/share)
cd prod-release && make -j$(nproc)
```

### With CV detection (feature/cv-detection)
```bash
cmake -DUSE_CVDETECTOR=ON -DONNXRUNTIME_ROOT=/opt/onnxruntime ...
```
This is optional. Without `-DUSE_CVDETECTOR=ON`, CvDetector compiles as a no-op stub.

## Runtime Architecture (Bare-Metal)

Three systemd services:
1. `sdrplay.service` — SDRplay API daemon (if using RSPduo)
2. `blah2-api.service` — Node.js API middleware (`node /opt/blah2/api/server.js /opt/blah2/config/config.yml`)
3. `blah2.service` — C++ radar processor (`/opt/blah2/bin/blah2 -c /opt/blah2/config/config.yml`)

Plus nginx serving static HTML on port 80 and proxying `/api/*` to Node.js on port 3000.

Useful commands:
```bash
sudo systemctl status blah2 blah2-api          # check status
journalctl -u blah2 -f                          # C++ processor logs
journalctl -u blah2-api -f                      # Node.js API logs
sudo bash /opt/blah2/bare-metal/start-blah2.sh  # start both services
sudo bash /opt/blah2/bare-metal/stop-blah2.sh   # stop both services
```

## Config File Structure

Config is YAML, parsed by ryml (C++) and js-yaml (Node.js). Key sections:

```yaml
capture:
  fs: 2000000               # Sample rate (Hz)
  fc: 204640000              # Center frequency (Hz)
  device:
    type: "RspDuo"           # RspDuo, Usrp, HackRf, Kraken
    # SDR-specific params...
  replay:
    state: false             # true to replay saved IQ data

process:
  ambiguity:
    delayMin/Max, dopplerMin/Max   # Map bounds
  detection:
    enable: true
    method: "cfar"           # "cfar" or "cv" (cv-detection branch only)
    pfa, nGuard, nTrain, minDelay, minDoppler, nCentroid
    cv:                      # Only used when method: "cv"
      model_path: "models/auto"
      confidence_threshold: 0.5
  tracker:
    enable: true

truth:
  adsb:
    enabled: true            # Enable ADS-B overlay (needs adsb-overlay cherry-pick)
    tar1090: '192.168.x.x'  # tar1090 host (no http://)
    adsb2dd: '192.168.x.x:3000'  # adsb2dd host:port

location:
  rx: { latitude, longitude, altitude, name }
  tx: { latitude, longitude, altitude, name }
```

## Detection Pipeline (C++)

In `blah2.cpp`, each CPI cycle runs:

1. **Ambiguity** — cross-ambiguity function → delay-Doppler `Map<complex<double>>`
2. **Clutter** — Wiener-Hopf filter
3. **Detection** (one of):
   - CFAR path: `CfarDetector1D::process()` → `Centroid::process()` → `Interpolate::process()`
   - CV path: `CvDetector::process()` (single call, returns same Detection format)
4. **Tracker** — track initiation/maintenance/deletion

All detectors output `Detection` objects with parallel vectors: `delay` (bins), `doppler` (Hz), `snr` (dB).

## ADS-B Truth Overlay

The adsb-overlay feature (commit `40ed151`) adds server-side polling of adsb2dd. The Node.js API proxies it at `GET /api/adsb` so the browser avoids CORS issues. Requires `truth.adsb.enabled: true` in config plus valid `tar1090` and `adsb2dd` addresses. The API logs `"ADS-B truth enabled, polling: ..."` at startup when active.

## Related Repositories

| Repo | Purpose |
|---|---|
| `github.com/kingjamez/adsb2dd-JM` | ADS-B truth server — converts tar1090 aircraft positions to delay-Doppler coordinates |
| `github.com/kingjamez/blah2-recorder` | Records delay-Doppler frames + ADS-B truth for ML training data |
| `github.com/kingjamez/PCRCV` | ML training pipeline (CenterNet) — runs on training machine, not Pi |
| `github.com/30hours/blah2` | Upstream blah2 repo |
| `github.com/30hours/3lips` | Multi-node geometric fusion for geographic tracking |

## Code Conventions

- C++: no explicit standard set on baremetal (compiler default, typically C++17 on GCC 11+). C++20 on main via CMakePresets. feature/cv-detection explicitly sets C++17.
- Frontend: vanilla JavaScript, no framework, no build step. Files served directly by nginx.
- Config parsing: ryml (C++ side), js-yaml (Node.js side). Access pattern: `tree["section"]["key"] >> variable`
- Error handling: stdout/stderr logging, systemd journal capture.
- SDR libraries: all four are always linked (stub libs for uninstalled hardware). The `device.type` config field selects which driver actually runs.

## Common Tasks

**Rebuild after code changes (bare-metal):**
```bash
cd /opt/blah2/build/prod-release && make -j$(nproc)
sudo systemctl restart blah2
```

**Update config and restart:**
```bash
sudo vim /opt/blah2/config/config.yml
sudo systemctl restart blah2 blah2-api
```

**Check if ADS-B overlay is working:**
```bash
curl http://localhost:3000/api/adsb
journalctl -u blah2-api | grep -i adsb
```

**Pull upstream changes:**
```bash
git remote add upstream https://github.com/30hours/blah2.git
git fetch upstream
git merge upstream/main  # or cherry-pick specific commits
```
