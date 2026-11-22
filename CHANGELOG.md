# Changelog

All notable hardware enablement milestones for **Surface Pro X SQ2 (`SC8180X` / Linux 7.3 ARM64)** are documented in this file.

## [v1.8.0] — Surface Slim Pen 2 Tactile Haptics (`0x0E`), White Hover Dot, RPMh Deep Sleep & Arch Packaging

### Added
- **Surface Slim Pen 2 Real-Time Tactile Haptics (`UsagePage 0x0E`)**:
  - Added `daemons/pen/surface-pen-haptics`, `systemd/surface-pen-haptics.service`, and `configs/udev/70-surface-pen-haptics.rules` streaming continuous writing (`0x1004`/`0x1005`), erasing (`0x1006`), and discrete tap/click (`0x1003`) waveforms over BLE HOGP.
  - Configured `MinConnectionInterval=6` / `MaxConnectionInterval=6` (`7.5 ms`) and native GATT Write With Response (`ATT_OP_WRITE_REQ 0x12`) with 15 ms MPP tip-boost stagger and 150 ms / 3.6 mm tap-to-drag transition for zero BLE disconnects.
- **Crisp White Stylus Hover Dot & GNOME Mutter 51.1 `#5215` Guard**:
  - Added GNOME Shell extension `extensions/surface-pen-dot@linux-surface/` using synchronous `GPollableInputStream` on the IPTS Stylus evdev node.
  - Added `daemons/pen/mutter-5215-nullguard.c` (`libmutter-tablet-nullguard.so`) hiding the default mouse arrow strictly for `CLUTTER_SPRITE_ROLE_TABLET` and guarding `wl_resource_get_client(NULL)` against `GNOME/mutter#5215`.
- **RPMh `s2idle` Deep-Sleep Driver (`drivers/sq2_s2idle_pm/`) & ALS Power Tuning**:
  - Detaches the unused `/mmcx-reg` dummy fixed-domain regulator, converts all 152 non-display interconnect requests to `QCOM_ICC_TAG_ACTIVE_ONLY` (`tag = 3`), and lowers APSS `18200000.rsc` `RPMH_SLEEP_STATE` votes (`cx.lvl`/`mx.lvl`/`mmcx.lvl` → `RETENTION`, `xo.lvl`/`lnbclka2`/`rfclkd2` → `0`) during `s2idle`.
  - Added `DEFINE_SIMPLE_DEV_PM_OPS` suspend/resume callbacks to `drivers/spx_tcs3430/spx_tcs3430.c` and tuned `surface-auto-brightness` polling to `1.5 s` (`alpha = 0.35`).
- **Clean Pre-Shutdown Teardown & Power-Key Suspend**:
  - Added `scripts/surface-pre-shutdown.sh` and `systemd/surface-pre-shutdown.service` to cleanly stop `rmnet_ipa` and `venus_dec`/`venus_core` before `qrtr-ns`/`rmtfs` shutdown.
  - Added `configs/systemd/surface-lid.conf` (`HandlePowerKey=suspend`, `HandlePowerKeyLongPress=poweroff`).
- **Packaging, Diagnostics, Documentation & CI**:
  - Added unified 1-command user-space installer (`scripts/install-sq2-userspace.sh`), 15-subsystem health check (`scripts/surface-sq2-diag.sh`), native Arch Linux ARM `PKGBUILD` (`packaging/arch/PKGBUILD`), SQ1 vs. SQ2 compatibility guide (`docs/SQ1_VS_SQ2.md`), GitHub Actions CI (`.github/workflows/ci.yml`), automated release packaging (`.github/workflows/release.yml`), and GitHub Pages documentation site.

## [v1.7.0] — Surface Slim Pen 2 Native Bluetooth Auto-Reconnect (`linux-surface#1656`)
- Root-caused missing Slim Pen 2 advertising on 1× top-button click & charger lift under default BlueZ (`Privacy = off`): enabled `Privacy = device` (`Local IRK` exchange) + `daemons/pen/surface-pen-blelc-setup` (`UsagePage 0xFFF4` `0x70`/`0x56` + WCN3998 `ll-privacy` Accept-List) for <1 s auto-reconnect with zero `StartDiscovery()` polling.
- Upgraded DKMS packaging (`scripts/install-sq2-dkms.sh`) and added `scripts/build-sq2-kernel-modules.sh`.

## [v1.6.0] — Complete `s2idle` Deep Sleep Across All 12 Subsystems (`surface-pro-x#29`)
- Added `patches/0012` (`drm/msm` DPU `MDP_DP_PHY_INTF_SEL` & DP AUX resume), `patches/0013` (Venus EL1 TrustZone `qcom_pas_set_remote_state` PM), and `patches/0014` (`spi-hid` `SIMPLE_DEV_PM_OPS`).
- Added non-blocking `systemd/system-sleep/` hooks preventing `user.slice` freeze deadlocks and SLPI FastRPC faults; verified ~0.9 s suspend / ~1.2 s resume with Hall-sensor lid and power-button wake.

## [v1.5.0] — Snapdragon X24 4G LTE Modem & Qualcomm `IPA v4.1` (`surface-pro-x#22`)
- Added `patches/0011` (SC8180X `IPA v4.1` support), `dts/sc8180x-surface-pro-x-sq2-ipa.dtsi`, and read-only Linux EL1 SCM (`0x02001a01`) SPI NOR `modem_fsg` RF calibration extractor (`drivers/sq2_fsg_read/`).
- Verified live 4G LTE carrier registration and `qmapmux0.0` data packets with `ModemManager` (`qcom-soc`).

## [v1.4.0] — Qualcomm Venus (Iris1 / HFI4) 4K Hardware Video Decode
- Added `patches/0009` (`videocc-sm8150` SC8180X compatible), `patches/0010` (SQ2 HFI4 `venus-core`/`venus-dec` ported to Linux 7.3), and `drivers/sq2_venus_overlay/`.
- Verified bit-exact SHA-256 hardware decode on 4K H.264, 4K HEVC, and 1080p VP9 via EL1 PAS 9.

## [v1.0.0 – v1.3.0] — Core SQ2 Bring-Up (Cameras, IR Face ID, Audio, GPU, USB-C DP, Sensors, TPM 2.0, Wi-Fi)
- Brought up all 3 cameras (`OV5693`, `OV13858`, `OV7251`) on Titan 175 CAMSS (`patches/0001..0004`, `drivers/surface_cam_spmi/`) with symmetric `DebayerEGL` CCMs, privacy LEDs, 850 nm IR strobe, and Howdy Windows Hello IR Face ID (~0.9 s match).
- Added `drivers/surface_ldoe14_xob/` + `patches/0008` (stereo speakers & SoundWire AHB mutex), `drivers/surface_typec/` + `patches/0005` (USB-C orientation & DisplayPort Alt Mode), `patches/0006` (`spi-hid` cold-boot D0 fix), `patches/0007` (Adreno 685 GMEM/HWCG/MMIO fixes), `drivers/tpm_qcom_qsee/` (QSEE fTPM 2.0), `drivers/spx_tcs3430/` (ALS), SLPI IMU/compass, Hall-sensor `SW_LID`, QMI LOC GPS, and Linux 7.3 `WCN3998` Wi-Fi QRTR boot ordering.
