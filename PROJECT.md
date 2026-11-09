# Project: MainlineAtriOS for Yandex Station Max (Quasar 2 / SM1)

## Architecture
- **Target Hardware**: Yandex Station Max / Quasar 2, SoC Amlogic S905X3 / SM1, 2GB RAM, 32GB eMMC.
- **Branch Topology**:
  - `main`: Restored original stable AtriOS state under kernel 4.9.113.
  - `mainline`: Active development branch for MainlineAtriOS on Mainline Linux 7.0+ ('AtriOS-aml') and Mainline U-Boot v2025.01 ('AtriOS-aml by leftymods').
- **Manufacturer Identity**: `leftymods` (`leftymods <ggalab33@gmail.com>`).
- **Subsystems**:
  - **Kernel Drivers**: 13 hardware drivers under `drivers/atrios/` (`gowin_led_device`, `quasar_led_screen`, `quasar_led_ring`, `quasar_pcba_ids`, `rotary-volume`, `gpio_keypad`, `zigbee-control`, `ltr308als01`, `leds-is31fl32xx`, `snd-soc-sy6045s`, `snd-soc-es8156`, `snd-soc-es7210`, `snd-soc-tas5825m`).
  - **Device Tree**: `board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max.dts`.
  - **Display & Lighting**: Gowin FPGA 25x16 LED matrix via `/dev/fb0` and Dual IS31FL3236A 72-channel LED ring via `/dev/fb1`, orchestrated by `atri-displayd`.
  - **Audio Pipeline**: 8-channel PDM digital microphone array on GPIOZ via `amlogic,sm1-snd-pdm`, ES7210 2-channel AEC hardware loopback, SY6045S / TAS5825M / ES8156 multi-way output amplifiers, ALSA UCM + PipeWire/WirePlumber with WebRTC AEC3.
  - **Wireless Subsystems**: Realtek RTL8822CS Bluetooth via mainline `btrtl` with A2DP Sink mode; Silicon Labs EFR32 Zigbee coordinator (EZSP NCP on `/dev/ttyAML1` via `zigbee-control`).
  - **Bootloader**: Mainline U-Boot v2025.01 with aml_burn / DFU / Fastboot and v3 FIP secure signing.
  - **Repository & Origin**: WSL Ubuntu-22.04 `/home/lmods/MainlineAtriOS` tracked to `https://github.com/leftymods/MainlineAtriOS.git`.

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| F00 | Branch Management & Main Restoration | Create branch `mainline`, restore branch `main` to stable kernel 4.9.113, switch to `mainline` | M0 | user directive |
| F01 | Toolchain & Build Dependencies | Install `python3-pyelftools` and verify build tools for kernel/U-Boot | M1 | survey |
| F02 | Mainline DTS Repair | Fix missing `/hdmi-connector`, `/emmc-pwrseq`, pinctrl phandles, and DAI phandles | M1 | survey |
| F03 | Audio PDM & Loopback DTS Bindings | Add `&pdm`, `dmics`, FIFO DAIs `frddr_*`/`toddr_*`, and slot masks for AEC | M1 | survey |
| F04 | Kernel 7.0 Driver Modernization | Fix 6 compilation-breaking drivers (`gowin_led_device`, `gpio_keypad`, `ltr308als01`, `leds-is31fl32xx`, `snd-soc-es8156`, `snd-soc-es7210`) | M2 | survey |
| F05 | SY6045S DSP Firmware Decoupling | Extract EQ/DRC binary files to `/lib/firmware/atrios/` and load via `request_firmware` | M2 | survey |
| F06 | Rotary Volume Gray Code Sampling | Replace lossy deferred workqueue with synchronous quadrature state machine (verified live with `rotaryd`) | M2 | survey |
| F07 | LED Ring Backlight & Framebuffer Name | Fix backlight brightness scaling math and align `fix.id` to `"YNDX LED ring"` | M2 | survey |
| F08 | PCBA IDs & Driver Compatible Reconciliation | Support discrete EEPROM addresses and reconcile DTS compatible strings | M2 | survey |
| F09 | Driver Cleanliness & Error Handling | Fix `warn_unused_result` in `tas5825m`, remove unused functions in `quasar_led_screen` | M2 | survey |
| F10 | Gowin 25x16 Matrix & LED Ring Service | Create `atri-displayd` (`screend` + `rotaryd` + `light`) with clock, volume bar, mic mute, auto-brightness (verified live on hardware) | M3 | survey |
| F11 | PipeWire & ALSA UCM Audio Routing | Configure 8-channel mic capture, AEC echo cancellation, and speaker output routing | M3 | survey |
| F12 | Bluetooth A2DP Sink Mode | Set up RTL8822CS firmware path, BlueZ audio sink agent, and PipeWire A2DP Sink | M3 | survey |
| F13 | Zigbee Coordinator Integration | Configure EFR32 EZSP NCP on `/dev/ttyAML2` with `zigbee-control` reset/boot control (verified live on hardware) | M3 | survey |
| F14 | Mainline U-Boot Build & FIP Signing | Build U-Boot v2025.01 with aml_burn/DFU/Fastboot and sign with `aml_encrypt_g12a` | M4 | survey |
| F15 | Unified Verification Suite | Build `scripts/verify_all.sh` covering toolchain, DTS, drivers, U-Boot, and firmware | M4 | survey |
| F16 | Documentation Overhaul | Update `README.md` (`drivers/atrios/` paths), architecture guides, and build specs | M4 | survey |
| F17 | Git Commit & GitHub Push | Commit as `leftymods <ggalab33@gmail.com>` on `mainline` (and `main`), push both to GitHub | M4 | survey |
| F18 | Final E2E Validation & Adversarial Testing | Pass 100% E2E test suite (Tiers 1-4) and adversarial coverage hardening (Tier 5) | M5 | survey |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M0 | Git Branch Topology Setup | F00: Create branch `mainline`, restore `main` to 4.9.113, track development in `mainline` | none | DONE |
| M1 | DTS & Build Environment Stabilization | F01, F02, F03: Install pyelftools, fix all DTS phandles, bindings (HDMI, eMMC, PDM 8ch mics, Serdev BT), compile with dtc | M0 | DONE |
| M2 | 13 Drivers Modernization & DSP Firmware Extraction | F04, F05, F06, F07, F08, F09: Modernize 13 drivers for Linux 7.0, extract SY6045S firmware to `/lib/firmware/atrios/`, verify clean compilation | M1 | DONE |
| M3 | Display, Audio & Wireless Stack Setup | F10, F11, F12, F13: Implement display/ring visualizer daemon, ALSA UCM / PipeWire audio routing, BT A2DP sink, Zigbee | M2 | DONE |
| M4 | U-Boot FIP, Master Build, Docs & Git Push | F14, F15, F16, F17: Compile/sign U-Boot, create verify_all.sh, update README/docs, push commits | M3 | DONE |
| M5 | Final E2E Test Suite & Hardening | F18: 100% pass of E2E test suite (Tiers 1-5 in verify_all.sh) and live hardware verification | M4 | DONE |

## Interface Contracts
### Kernel Drivers ↔ Device Tree
- `leftymods,zigbee-control` (`meson-sm1-yandex-station-max.dts:89`) binds to `drivers/atrios/zigbee-control.c`.
- `amlogic,gpio_keypad` (`meson-sm1-yandex-station-max.dts:77`) binds to `drivers/atrios/gpio_keypad.c`.
- `quasar_pcba_ids` probes discrete properties `top-eeprom-addr`, `main-eeprom-addr`, `led-eeprom-addr` under `i2c-bus`.
- Audio DAI links connect `&tdmif_b` (playback) and `&pdm` / `&tdmif_c` (capture) to `amlogic,axg-sound-card`.

### DSP Firmware ↔ Driver
- SY6045S driver requests `atrios/sy6045s-tweeters-settings.txt` and `atrios/sy6045s-woofer-settings.txt`.
- Firmware files reside in `/lib/firmware/atrios/`. Fallback looks in `/lib/firmware/`.

### Framebuffer ↔ Userspace Display Daemon
- `/dev/fb0`: Gowin 25x16 screen (`fix.id = "YNDX LED screen"`).
- `/dev/fb1`: Quasar LED ring (`fix.id = "YNDX LED ring"`).
- `atri-displayd` communicates through standard `/dev/fb*` ioctls and mmap.

## Code Layout
- `drivers/atrios/`: 13 kernel drivers, Makefile, and Kconfig.
- `board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max.dts`: Mainline DTS.
- `board/yandex/station2/mainline/linux_7.0_station_max_defconfig`: Kernel defconfig.
- `board/yandex/station2/mainline/uboot_station_max_defconfig`: U-Boot defconfig.
- `board/yandex/station2/rootfs_overlay/`: Rootfs configuration, PipeWire profiles, firmware (`/lib/firmware/atrios/`).
- `scripts/build_mainline_uboot.sh`: U-Boot fetch, compile, and FIP signing.
- `scripts/verify_all.sh`: Automated comprehensive validation script.
- `build_atrios.sh`: Master build script.
- `README.md`: Project documentation.
