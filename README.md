# Surface Pro X SQ2 (SC8180X / ARM64) Linux Enablement

Kernel patches, Device Tree definitions, out-of-tree rail controllers, `libcamera` GPU SoftISP tuning, Windows Hello IR Face ID (Howdy), and system daemons bringing up the **Microsoft Surface Pro X SQ2 (Qualcomm SC8180X)** on Arch Linux ARM64 (`7.3.1-1-surface`).

## Hardware Status (Verified on Live SQ2 Hardware)

| Subsystem | Status | Implementation / Notes |
|---|---|---|
| **Front RGB Camera (5 MP `OV5693`)** | Working | `csiphy0` (2-lane) → `csid0` → `vfe0_rdi0`; 2592×1944, `rotation = <180>`, PipeWire/libcamera GPU `DebayerEGL` + symmetric CCM |
| **Rear RGB Camera (13 MP `OV13858`)** | Working | `csiphy2` (4-lane) → `csid1` → `vfe1_rdi0`; 4224×3136, DT + reset GPIO + flips + gain defaults (`patches/0001`), PipeWire/libcamera |
| **Front IR Camera (VGA `OV7251`)** | Working | `csiphy3` (1-lane) → `csid3` → `vfe3_rdi0`; 640×480 hardware-unpacked `Y10`, 90° portrait mount (`rotate = 2`) |
| **Privacy LEDs (Front & Rear)** | Working | Automatic refcounted `qcom-camss` control (`GPIO 25` front shared by RGB+IR, `GPIO 11` rear) |
| **850 nm IR Strobe Illuminator** | Working | Automatic hardware `STROBE` pin (`0x3b81`/`0x3b8e..8f`/`0x3005`) on `OV7251` `s_stream` (`patches/0002`) |
| **Windows Hello IR Face ID** | Working | Howdy (`YuNet` + `SFace`) on GDM, Polkit, `sudo`, `su` (~0.9 s match) via single-thread OpenMP/OpenBLAS wrappers |
| **Stereo Speakers (`WCD934x` + `WSA8815`) & Mics** | Working | `surface_ldoe14_xob` (LDO14E XOB RPMh rail, avoids VRM TCS hang) + SoundWire sync & 8.5 V Smart Boost |
| **Wi-Fi (`WCN3998` / `ath10k`, 866 Mbps)** | Working | Linux 7.3 `rmtfs.service` QRTR pre-boot drop-in (`daemons/wifi-qrtr/10-modem-preboot.conf`) |
| **Bluetooth (`WCN3998`)** | Working | Deterministic `btmgmt` public BD_ADDR setup (`daemons/bluetooth/surface-bt-mac.sh`) |
| **Touchscreen & Slim Pen 2 (IPTS)** | Working | `iptsd` 3.1.1 calibrated MPP2 thresholds (`configs/iptsd/iptsd.conf`), tilt, pressure, side button, palm rejection |
| **Slim Pen 2 BLE Auto-Connect** | Experimental | Best-effort BlueZ LE/contact daemons (`daemons/pen/`); core IPTS drawing/hover works independently without BLE |
| **Display & Adreno 685 GPU** | Working | `eDP` 2880×1920, Freedreno/Turnip with `noubwc` (`libcamera/environment.d/99-surface-gpu.conf`) |
| **Ambient Light Sensor (`TCS3430`)** | Working | Direct `/dev/i2c-0` `0x39` adaptive brightness daemon (`daemons/display/surface-auto-brightness`) |
| **Accelerometer, Compass & Auto-Rotation (`SLPI`)** | Working | `remoteproc_slpi` + FastRPC (`dts/sc8180x-surface-pro-x-sq2-slpi.dtsi`), `hexagonrpcd`, `libssc`, `iio-sensor-proxy` (`LSM6DSO` + `AK0991x`) |
| **Type Cover, Posture & Lid Handling** | Working | SAM posture daemon (`closed`/`laptop`/`tablet`), eDP blank + lock on close (`daemons/posture/surface-lid-daemon.sh`) |
| **CPU Power Profiles (`SC8180X`)** | Working | Native UPower/hadess D-Bus `PowerProfiles` manager (`daemons/power/surface-power-profile`) |
| **Hardware GNSS (`QMI LOC`)** | Working | On-demand QRTR QMI LOC → NMEA Unix socket bridge (`/run/surface-gps.sock`) |
| **Hardware TPM 2.0 (`QSEE fTPM`)** | Working | `tpm_qcom_qsee` QSEECOM transport (`drivers/tpm_qcom_qsee/`); `/dev/tpm0`, `/dev/tpmrm0`, `tpm-rng-0`, SHA-1/256/384 PCR banks |
| **Suspend (`S2idle` / `S3`)** | Not working | Disabled via `surface-no-deep-suspend.conf`; lid close uses screen-off + session lock + `power-saver` |
| **Hardware Video Decode (`Venus`)** | Disabled | `LIBVA_DRIVER_NAME=none` in desktop environment to avoid browser stability issues |

## Key Technical Highlights

- **All 3 cameras + all 3 hardware LEDs verified end-to-end (20/20 frames each)**:
  - Proven physical CSIPHY wiring (`OV5693` on `csiphy0`, `OV13858` on `csiphy2`, `OV7251` on `csiphy3`), SMMU Stream IDs (`0xa00..0xae0`, `0xe00..0xee0`), and `PMC8180-A` SPMI LDO rails (`LDO14_A` 1.8 V, `LDO17_A` 2.856 V, `LDO1_A` 1.2 V).
  - Titan 175 / VFE Lite fixes in `qcom-camss`: guards `VFE_BUS_WM_DEBUG_STATUS_CFG` (`0x226c`) off VFE Lite, fixes 4-client `WM_ADDR_NO_SYNC_LITE_VAL`, corrects the 2-deep `VFE_BUS_WM_IMAGE_ADDR` FIFO & single `WM_CLIENT_BUF_DONE` IRQ subscription, and prevents post-stream SC8180X NoC deadlocks.
- **`libcamera` `DebayerEGL` magenta-highlight root cause**:
  - OpenGL ES `mat3` is column-major, transposing row-major IPA YAML CCMs during `ccm * rgb`. Symmetric row- and column-normalized CCMs in [`libcamera/ipa/simple/`](libcamera/ipa/simple/) + `blackLevel: 1536` eliminate magenta highlight clipping on Adreno 685.
- **SQ2 `ldoe14` XOB speaker rail driver**:
  - On SQ2, `cmd-db` exposes `ldoe14` (`0x43900`) as an enable-only XOB resource (`+0x4`), whereas upstream `qcom-rpmh-regulator` treats `pmc8180 ldo14` as VRM and hangs AMC TCS slots. [`drivers/surface_ldoe14_xob/`](drivers/surface_ldoe14_xob/) votes `ldoe14` cleanly via `rpmh_write`.
- **Linux 7.3 Wi-Fi/Modem QRTR race fix**:
  - Unloading duplicate in-kernel `qcom_pd_mapper` and starting `remoteproc` modem 0.7 s before `rmtfs` prevents `-EAGAIN` (`failed to announce new service`) so `ath10k` brings up `wlan0` reliably on boot.

## Repository Layout

```text
patches/    Clean Linux v7.3 patches (ov13858, ov7251, portable camss LEDs/RDI, full SC8180X camss)
dts/        Symbolic SC8180X SQ2 camera DTSI + raw reference diff against audio-only base DTB
drivers/    Out-of-tree kernel modules (surface_cam_spmi, surface_ldoe14_xob, tpm_qcom_qsee, surface_ir_sfmt)
libcamera/  Symmetric DebayerEGL IPA YAMLs, WirePlumber rules, Adreno 685 env configs
faceid/     Howdy PAM/CLI single-thread wrappers, OV7251 config.ini.example, PAM guide
scripts/    Camera boot loader, media-ctl topology script, GTK4/Libadwaita camera control panel
daemons/    Wi-Fi QRTR drop-in, audio sync, BT MAC, power profiles, ALS, lid/posture, pen, GPS
configs/    udev rules, iptsd.conf, systemd sleep/logind configs, smart-osk GNOME extension
systemd/    Systemd service units
docs/       Hardware reference notes & build/installation guide
```

## Documentation

- [Camera Hardware Notes (CSIPHY, SMMU, SPMI, Titan 175, DebayerEGL)](docs/CAMERA_HARDWARE.md)
- [Build & Installation Guide](docs/BUILD_AND_INSTALL.md)
- [Howdy IR Face ID Setup](faceid/README.md)

## Credits

This work builds directly on upstream Qualcomm/Linux efforts and the `linux-surface` community:

- **Maximilian Luz (`qzed`)** — Surface Aggregator Module (SAM) & `linux-surface` ecosystem
- **Val Packett (`valpackett`)** — `linux-qclaptops` (`uwu-next`), `camcc-sc8180x`, and SC8180X laptop bring-up
- **Alex Jacocks (`ajacocks`)** & **Tawheed (`tawheedmm`)** — `linux-surface/surface-pro-x` SQ1/SQ2 kernel, audio, and documentation groundwork
- **Linaro Ltd.** — `qcom-camss` and `phy-qcom-mipi-csi2` driver stack
- **No proprietary firmware blobs are included in this repository.**

## License

Kernel patches and out-of-tree kernel modules are licensed under `GPL-2.0-only` (see [`LICENSE`](LICENSE)). `libcamera` IPA tuning YAML files are `CC0-1.0`.
