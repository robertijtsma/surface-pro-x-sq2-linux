# Snapdragon X24 4G LTE Modem & Qualcomm IPA v4.1 on Surface Pro X SQ2

This document describes the Linux 7.3 cellular enablement for the integrated **Qualcomm Snapdragon X24 LTE modem** (`remoteproc@4080000`, `qcmpss8180.mbn`) and **Qualcomm IPA v4.1** packet offload engine (`ipa@1e40000`) on the **Microsoft Surface Pro X SQ2 (SC8180X)**.

## Why LTE Fails Out of the Box on Linux

Two independent gaps keep the integrated X24 modem offline on mainline/Arch Linux ARM64:

1. **Empty `/var/lib/rmtfs/modem_fsg` leaves the modem in `factory-test` mode**:
   - On the Surface Pro X, each unit's factory RF calibration and IMEI live in the `fsg` partition (`638ff8e2-22c9-e33b-8f5d-0e81686a68cb`, 2 MiB) inside the onboard 32 MiB SPI NOR flash owned by TrustZone — not on NVMe.
   - When `rmtfs` serves an all-zero `modem_fsg`, MPSS boots into `Mode: 'factory-test'` with `IMEI: 'unknown'` and refuses cellular registration.
   - [`drivers/sq2_fsg_read/`](../drivers/sq2_fsg_read/) performs strictly read-only Qualcomm TrustZone SCM storage calls (`SMC 0x02001a01`, `GET_INFO` + bounded 4 KiB `READ`) directly from Linux EL1 via `qcom_tzmem_alloc()` + `qcom_scm_call()`, CRC-validates the primary GPT, and exports the device's own `fsg` partition temporarily to `/sys/kernel/debug/sq2_fsg` (`0400`, wiped on unload) so the operator can populate `/var/lib/rmtfs/modem_fsg` (`0600`) without Windows or a custom UEFI loader.
   - **Never share or commit `modem_fsg`** — it contains per-device RF calibration and hardware identity.

2. **Upstream Linux `drivers/net/ipa` lacks SC8180X (`IPA v4.1`)**:
   - Mainline Linux 7.3 `ipa.ko` includes IPA v3.1, v3.5.1, v4.2, v4.5, v4.7, v4.9, v4.11, v5.0, v5.2, and v5.5, omitting SC8180X/SM8150 **IPA v4.1** (`0x40010000`).
   - [`patches/0011-net-ipa-add-sc8180x-ipa-v4.1-support.patch`](../patches/0011-net-ipa-add-sc8180x-ipa-v4.1-support.patch) adds `reg/ipa_reg-v4.1.c`, `data/ipa_data-v4.1.c` (adapted from [`lhf1a/googlebookos_surface_pro_x`](https://github.com/lhf1a/googlebookos_surface_pro_x) to Linux 7.3), GSI v4.0 register dispatch, and the `qcom,sc8180x-ipa` match entry.
   - [`dts/sc8180x-surface-pro-x-sq2-ipa.dtsi`](../dts/sc8180x-surface-pro-x-sq2-ipa.dtsi) reserves 384 KiB of firmware staging RAM at `0x9b000000` (`no-map`, outside the Venus PIL gap at `0x8c000000..0x8c600000`) and defines `/soc@0/ipa@1e40000` (AP/uC SMMU SIDs `0x520`/`0x522`, GIC SPI `311` IPA / SPI `432` GSI, `ipa-ap-to-modem` & `ipa-modem-to-ap` SMP2P, `RPMH_IPA_CLK`, `aoss_qmp`, and `memory`/`imem`/`config` interconnects).

## Boot Ordering & Coexistence

- `configs/modprobe.d/ipa-sq2-guard.conf` blacklists `ipa` from early `systemd-udevd` coldplug.
- `systemd/surface-lte-setup.service` runs `/usr/local/bin/surface-lte-load` after `rmtfs.service`, `surface-camera-setup.service`, and `surface-venus-setup.service`, loading `ipa` (`rmnet_ipa0`) and `rmnet`. On fallback DTBs lacking `1e40000.ipa`, the script exits cleanly.
- `systemd/ModemManager.service.d/10-after-surface-lte.conf` orders `ModemManager.service` after `rmtfs.service` and `surface-lte-setup.service`, avoiding any QRTR race with `ath10k` Wi-Fi (`wlan0`) and ensuring `rmnet_ipa0` is present when `ModemManager`'s `qcom-soc` plugin probes `qrtr0`.

## Cold-Reboot Hardware Verification (`7.3.1-1-surface`)

Verified cold-boot on Surface Pro X SQ2 alongside Wi-Fi (`wlan0`), Venus 4K decode (`/dev/video-venus-dec`), all 3 cameras, Howdy IR Face ID, and audio:

- `surface-lte-setup.service`: `status=0/SUCCESS` in 88 ms (`ipa 1e40000.ipa: IPA driver setup completed successfully`, `version=4.1`, `tx_endpoint_id=2`, `rx_endpoint_id=11`, `MAPv4`).
- `ModemManager`: `qcom-soc` plugin binds `qrtr0 (qmi)` + `rmnet_ipa0 (net)`, detects active USIM in slot 1 (and slot 2), reports real IMEI, registers on home 4G LTE network (`T-Mobile NL`, 62–76% signal / `-66 dBm`, `packet service state: attached`).
- `NetworkManager`: exposes `qrtr0` (`gsm`), negotiates multiplexed `qmapmux0.0@rmnet_ipa0` IPv4/30 bearer and passes hardware-offloaded TX/RX packets over `rmnet_ipa0` with zero link errors or drops.
