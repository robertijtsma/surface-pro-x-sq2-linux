# Surface Pro X SQ1 vs. SQ2 Compatibility Guide

While all patches, DKMS modules, and daemons in this repository are verified live on **Microsoft Surface Pro X SQ2** (`SC8180XP`, Adreno 685, 16 GB RAM), the vast majority of subsystems share identical hardware between the **SQ1** and **SQ2** models.

## Subsystem Compatibility Matrix

| Subsystem | SQ2 (Verified Live) | SQ1 (`SC8180X` / Adreno 680) | Notes / Differences |
|---|---|---|---|
| **Front 5 MP (`OV5693`), Rear 13 MP (`OV13858`), IR VGA (`OV7251`) & Privacy LEDs** | Works | Compatible | Identical sensors, CSIPHY wiring (`0`/`2`/`3`), SMMU Stream IDs, and `PMC8180-A` SPMI rails (`surface_cam_spmi`). |
| **850 nm IR Strobe & Windows Hello IR Face ID (Howdy)** | Works | Compatible | Identical `OV7251` strobe registers (`patches/0002`) and Howdy single-thread PAM wrappers (`faceid/`). |
| **Touchscreen & IPTS Digitizer (`spi-hid` + `iptsd`)** | Works | Compatible | Identical `045E:0C21` SPI-HID digitizer. `patches/0006` (cold-boot D0 fix) and `patches/0014` (`s2idle` PM) apply directly to SQ1. |
| **Surface Slim Pen 2 BLE Auto-Connect, Tactile Haptics (`0x0E`) & White Hover Dot** | Works | Compatible | 100% hardware-independent across SQ1, SQ2, and even Intel Surface models (`Privacy = device` + Local IRK, `surface-pen-haptics`, `surface-pen-dot@linux-surface`, `mutter-5215-nullguard.c`). |
| **4G LTE Modem (`Snapdragon X24` / `IPA v4.1`) & `sq2_fsg_read`** | Works | Compatible | Both SQ1 and SQ2 use the Snapdragon X24 LTE modem and TrustZone SPI NOR `modem_fsg` (`0x02001a01`). Works on LTE-equipped SQ1 & SQ2 units. |
| **Hardware GNSS / GPS (`QMI LOC`)** | Works | Compatible | Identical MPSS QMI LOC service over QRTR (`daemons/gps/surface-qmi-gps-daemon.py`). |
| **Wi-Fi (`WCN3998` / `ath10k`) & Bluetooth** | Works | Compatible | Identical `WCN3998` wireless chip and Linux 7.3 `qcom_pd_mapper` / `rmtfs` QRTR boot-ordering drop-in. |
| **USB-C Orientation & DisplayPort Alt Mode (`surface_typec`)** | Works | Compatible | Identical Surface Aggregator Module (`SAM`, `TC=0x09`/`CID=0x0d`) and QMP combo PHYs (`mdss_dp0`/`mdss_dp1`). |
| **Ambient Light Sensor (`TCS3430`), SLPI IMU/Compass & Hall-Sensor Lid (`SW_LID`)** | Works | Compatible | Identical `TCS3430` on `i2c-0` (`0x39`), SLPI `LSM6DSO`/`AK0991x`, and TLMM GPIO 121 Hall sensor. |
| **Hardware TPM 2.0 (`tpm_qcom_qsee`)** | Works | Compatible | Identical `qcom.tz.tpm` TrustZone app over QSEECOM. |
| **`s2idle` Deep Sleep & `sq2_s2idle_pm`** | Works | Compatible | Identical `18200000.rsc` RPMh `SLEEP` set, unused `/mmcx-reg` node, and `drm/msm` `MDP_DP_PHY_INTF_SEL` resume behavior (`patches/0012`, `0014`). |
| **Stereo Speakers (`WCD934x` + `WSA8815`)** | Works (`surface_ldoe14_xob`) | Check `cmd-db` for `ldoe14` | On **SQ2**, `cmd-db` exposes `ldoe14` as an enable-only XOB rail (requiring `surface_ldoe14_xob`). On some **SQ1** firmware revisions, `ldoe14` is handled via VRM. Check `dmesg` for RPMh TCS errors before loading `surface_ldoe14_xob`. `patches/0008` (SoundWire AHB mutex) benefits both. |
| **GPU (`Adreno 685` vs `Adreno 680`)** | Works (`patches/0007`) | Upstream `a680` | **SQ2** uses Adreno 685 (`6.3.5.0`) requiring `patches/0007` GMEM/HWCG/MMIO guards; **SQ1** uses Adreno 680 (`6.3.0.0`), which is natively matched upstream (though `patches/0007` preserves `a680` compatibility). |
| **4K Hardware Video Decode (`Qualcomm Venus` HFI4)** | Works (`patches/0009..0010`, `0013`) | Experimental / Firmware-dependent | Uses `VIDEO.IR.1.2-00079-PROD-2` signed for SQ2 EL1 PAS 9. SQ1 units should extract their own unit's Windows `qcvss8180.mbn` (`venus.mbn`) and verify `PAS 9` authentication before enabling `surface-venus-setup.service`. |

## Quick Recommendation for SQ1 Owners

1. Use the **SQ1 Device Tree** (`sc8180x-microsoft-surface-pro-x.dtb`) as your base and include the shared `.dtsi` overlays (`cameras`, `slpi`, `typec-dp`, `lid`, `ipa`).
2. Apply `patches/0001..0006`, `0008`, `0011`, `0012`, and `0014`.
3. Install the user-space stack via `sudo ./scripts/install-sq2-userspace.sh` (all Slim Pen 2 haptics, hover dot, auto-brightness, lid/posture, Wi-Fi QRTR, and `s2idle` hooks work identically on SQ1).
4. Run `./scripts/surface-sq2-diag.sh` to verify every subsystem.
