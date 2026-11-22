# Build & Installation Guide (Surface Pro X SQ2 / Arch Linux ARM64)

> **Firmware notice:** This repository contains **no** proprietary Qualcomm or Microsoft firmware blobs (`.mbn`, `.jsn`, `.qdb`, `.bin`). Extract them from your own Windows partition using `surface-pro-x` firmware extraction instructions and place them under `/lib/firmware/qcom/sc8180x/microsoft/surface-pro-x/`.

## 1. Kernel Patches & Out-of-Tree Drivers

Tested against Linux `7.3.1-1-surface` (requires `camcc-sc8180x` and Linaro's `phy-qcom-mipi-csi2` driver, as carried in `valpackett/linux-qclaptops` `uwu-next`).

### Option A: Applying Portable Patches Onto a Tree With SM8150/SC8180X CAMSS Already Present

```bash
git apply patches/0001-media-i2c-ov13858-dt-flips-and-gain-defaults.patch
git apply patches/0002-media-i2c-ov7251-continuous-clk-and-ir-strobe.patch
git apply patches/0003-media-qcom-camss-privacy-leds-and-rdi-y10-sfmt.patch
```

### Option B: Applying Full SC8180X / Titan 175 CAMSS Stack Onto Vanilla Linux v7.3

```bash
git apply patches/0001-media-i2c-ov13858-dt-flips-and-gain-defaults.patch
git apply patches/0002-media-i2c-ov7251-continuous-clk-and-ir-strobe.patch
git apply patches/0004-media-qcom-camss-sc8180x-titan175-full.patch
```

### Out-of-Tree Helper Modules (Automated via DKMS)

All 7 standalone out-of-tree modules (`spi-hid`, `spx_tcs3430`, `sq2_venus_overlay`, `surface_cam_spmi`, `surface_ldoe14_xob`, `surface_typec`, `tpm_qcom_qsee`) can be registered once with **DKMS** so future kernel upgrades automatically rebuild and install them into `/lib/modules/<kver>/updates/dkms/`:

```bash
sudo pacman -S --needed dkms
sudo ./scripts/install-sq2-dkms.sh
sudo cp configs/modules-load.d/surface-tpm.conf /etc/modules-load.d/
```

For rebuilding all patched in-tree kernel modules (`patches/0001..0014`) against a new Linux kernel source tree in one command:

```bash
./scripts/build-sq2-kernel-modules.sh /path/to/linux-source-tree --install
# Pass --vanilla-camss if your tree does not already carry SM8150/SC8180X CAMSS.
```

Or build individual out-of-tree modules manually:

```bash
# PMC8180-A camera SPMI LDO rails (1.8V / 2.856V / 1.2V)
make -C drivers/surface_cam_spmi && sudo make -C drivers/surface_cam_spmi install

# SQ2 LDO14E XOB speaker amplifier rail (avoids RPMh VRM TCS hang)
make -C drivers/surface_ldoe14_xob && sudo make -C drivers/surface_ldoe14_xob install

# Qualcomm QSEE fTPM 2.0 transport (/dev/tpm0, /dev/tpmrm0, tpm-rng-0)
make -C drivers/tpm_qcom_qsee && sudo make -C drivers/tpm_qcom_qsee install
sudo cp configs/modules-load.d/surface-tpm.conf /etc/modules-load.d/

# Optional: live Y10 S_FMT hook only if running an unpatched qcom-camss
# make -C drivers/surface_ir_sfmt && sudo make -C drivers/surface_ir_sfmt install
```

## 2. Device Tree

Include [`dts/sc8180x-surface-pro-x-sq2-cameras.dtsi`](../dts/sc8180x-surface-pro-x-sq2-cameras.dtsi) in your `sc8180x-surface-pro-x.dts` board file and rebuild the DTB. Always keep a known-good fallback DTB entry in your bootloader.

## 3. `libcamera`, WirePlumber & Browsers

```bash
sudo mkdir -p /etc/libcamera/ipa/simple
sudo cp libcamera/ipa/simple/ov5693.yaml libcamera/ipa/simple/ov13858.yaml /etc/libcamera/ipa/simple/
sudo cp libcamera/ipa/simple/ov5693.yaml libcamera/ipa/simple/ov13858.yaml /usr/share/libcamera/ipa/simple/

sudo mkdir -p /etc/wireplumber/wireplumber.conf.d
sudo cp libcamera/wireplumber/10-surface-libcamera.conf /etc/wireplumber/wireplumber.conf.d/

mkdir -p ~/.config/environment.d ~/.config/wireplumber/wireplumber.conf.d
cp libcamera/environment.d/10-surface-libcamera.conf ~/.config/environment.d/
cp libcamera/environment.d/99-surface-gpu.conf ~/.config/environment.d/
cp libcamera/wireplumber/51-surface-pro-x.conf ~/.config/wireplumber/wireplumber.conf.d/
```

For browser WebRTC access via PipeWire (`libcamera`):
- **Chromium / Brave / Electron**: add `--enable-features=WebRtcPipeWireCapturer` to `~/.config/chromium-flags.conf` (and equivalents).
- **Firefox / Zen Browser**: set `media.webrtc.camera.allow-pipewire = true` in `about:config` or `user.js`.

## 4. Camera Boot Scripts & Face ID (Howdy)

```bash
sudo install -m 755 scripts/camera/surface-camera-load /usr/local/bin/
sudo install -m 755 scripts/camera/surface-camera-check /usr/local/bin/
sudo install -m 755 scripts/camera/surface-camera-faceid-settings /usr/local/bin/
sudo cp configs/modprobe.d/blacklist-camera.conf /etc/modprobe.d/
sudo cp systemd/surface-camera-setup.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now surface-camera-setup.service
```

See [`faceid/README.md`](../faceid/README.md) for compiling the Howdy PAM wrappers and configuring `/etc/howdy/config.ini`.

## 5. Wi-Fi QRTR Fix, Audio, Bluetooth & System Daemons

```bash
# Kernel 7.3 Wi-Fi/Modem QRTR -EAGAIN workaround
sudo mkdir -p /etc/systemd/system/rmtfs.service.d
sudo cp daemons/wifi-qrtr/10-modem-preboot.conf /etc/systemd/system/rmtfs.service.d/

# Daemons & udev / systemd / iptsd configs
sudo install -m 755 daemons/audio/surface-audio-boot-sync.sh /usr/local/sbin/
sudo install -m 755 daemons/bluetooth/surface-bt-mac.sh /usr/local/sbin/
sudo install -m 755 daemons/gps/surface-qmi-gps-daemon.py /usr/local/sbin/
sudo install -m 755 daemons/power/surface-auto-power-profile.sh /usr/local/sbin/
sudo install -m 755 daemons/power/surface-power-profile /usr/local/bin/
sudo install -m 755 daemons/display/surface-auto-brightness /usr/local/bin/
sudo install -m 755 daemons/posture/surface-lid-daemon.sh /usr/local/bin/
sudo install -m 755 daemons/posture/surface-touch-rotate /usr/local/bin/
sudo install -m 755 daemons/pen/surface-pen-blelc-setup /usr/local/bin/
sudo install -m 755 systemd/system-sleep/surface-pen.sh /usr/lib/systemd/system-sleep/

sudo cp configs/udev/*.rules /etc/udev/rules.d/
sudo cp configs/iptsd/iptsd.conf /etc/iptsd.conf
sudo mkdir -p /etc/systemd/sleep.conf.d /etc/systemd/logind.conf.d
sudo cp configs/systemd/surface-s2idle.conf /etc/systemd/sleep.conf.d/
sudo cp configs/systemd/surface-lid.conf /etc/systemd/logind.conf.d/
sudo cp systemd/*.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo udevadm control --reload-rules
```

Enable the services you want (`surface-audio-boot-sync.service`, `surface-bluetooth-mac.service`, `surface-pen-blelc.service`, `surface-power-profile.service`, `surface-lid-daemon.service`, `surface-qmi-gps.service`). Keep the legacy `surface-pen-ble-listener.service` and `surface-pen-bluetooth-autoconnect.service` disabled: `StartDiscovery()` and blocking `Device1.Connect()` interfere with WCN3998's native hardware LE Accept-List passive scan.

For **native Surface Slim Pen 2 auto-reconnect** (1× top-button click, Type Cover charger lift, post-`s2idle`, and cold boot — fixing `linux-surface/linux-surface#1656`), merge [`configs/bluetooth/main.conf.snippet`](../configs/bluetooth/main.conf.snippet) (`Privacy = device`, `FastConnectable = true`, `AutoEnable = true`) into `/etc/bluetooth/main.conf`, restart `bluetooth.service`, and re-pair the Slim Pen 2 **once** so BlueZ distributes its Local IRK during SMP bonding. See [`docs/SLIM_PEN_2_BLUETOOTH.md`](SLIM_PEN_2_BLUETOOTH.md) for the full step-by-step pairing flow and firmware details.

## 6. SLPI Sensor Hub (Accelerometer, Compass & Auto-Rotation)

The LSM6DSO 6-axis IMU and AK0991x magnetometer are attached to the Qualcomm SLPI DSP (`remoteproc@2400000`). Bringing them up requires the device's `qcslpi8180.mbn` firmware and OEM sensor registry JSONs from Windows (or fetched from Windows Update with `wudrivers`), plus [`hexagonrpcd`](https://github.com/linux-msm/hexagonrpc), `libssc`, and `iio-sensor-proxy`.

1. **Stage firmware and OEM sensor configs** (from `surfaceprox_subextscss` and `SurfaceProX_sensorsconfig`):
   - `/lib/firmware/qcom/msft/surface/pro-x-sq2/qcslpi8180.mbn`
   - `/usr/share/qcom/sc8180x/microsoft/surface-pro-x/sensors/config/` (`*.json` and `config_list.txt`)
   - `/usr/share/qcom/sc8180x/microsoft/surface-pro-x/sensors/registry/` (writable empty directory)
   - `/usr/share/qcom/sc8180x/microsoft/surface-pro-x/socinfo/` (`hw_platform`, `platform_subtype`, `platform_subtype_id`, `platform_version`, `revision`, `soc_id`)

2. **Build and install `hexagonrpcd`**:

```bash
git clone https://github.com/linux-msm/hexagonrpc.git
cd hexagonrpc
meson setup build --prefix=/usr/local
ninja -C build
sudo ninja -C build install
sudo ldconfig
```

3. **Apply the SLPI DTB patch and install service/udev files**:

```bash
# Apply dts/slpi-against-cam-step5.patch (or include dts/sc8180x-surface-pro-x-sq2-slpi.dtsi)
# and compile the resulting DTB with dtc.

sudo pacman -S libssc iio-sensor-proxy
sudo install -m 755 scripts/sensors/surface-wait-slpi-sensors /usr/local/bin/
sudo install -m 644 configs/udev/81-surface-pro-x-sensors.rules /etc/udev/rules.d/
sudo install -m 644 systemd/hexagonrpcd-sdsp.service /etc/systemd/system/
sudo install -Dm644 systemd/iio-sensor-proxy.service.d/after-hexagonrpcd.conf \
    /etc/systemd/system/iio-sensor-proxy.service.d/after-hexagonrpcd.conf
sudo systemctl daemon-reload
sudo systemctl enable hexagonrpcd-sdsp.service
sudo udevadm control --reload-rules
```

After rebooting into the SLPI-enabled DTB, `monitor-sensor` reports live accelerometer orientation and compass heading, and GNOME automatically rotates the display when the Type Cover is folded back or detached.

## 7. USB-C Orientation & DisplayPort Alt Mode (`surface_typec`)

On the Surface Pro X, USB Type-C cable orientation and DisplayPort Alt Mode HPD events are managed by the Surface System Aggregator Module (SSAM, target category `USC = 0x1b`).

1. **Apply the SSAM registry patch and build `surface_typec`**:

```bash
# Apply patches/0005-platform-surface-aggregator-registry-spx-usc.patch to
# drivers/platform/surface/surface_aggregator_registry.c (preserving ssam_node_group_spx[]
# for Type Cover, touchpad, tablet mode switch, battery, RTC, and thermal sensors).

make -C drivers/surface_typec && sudo make -C drivers/surface_typec install
```

2. **Apply the USB-C / DisplayPort DTB nodes**:
   - Include [`dts/sc8180x-surface-pro-x-sq2-typec-dp.dtsi`](../dts/sc8180x-surface-pro-x-sq2-typec-dp.dtsi) (or apply [`dts/typec-dp-against-slpi.patch`](../dts/typec-dp-against-slpi.patch)) to wire `connector@0` and `connector@1` under `embedded-controller`, add `orientation-switch;` to `usb_prim_qmpphy` and `usb_sec_qmpphy`, and enable `mdss_dp0` (`ae90000`) and `mdss_dp1` (`ae98000`) with `data-lanes = <0 1>`.

3. **Early `initramfs` module ordering**:
   - When `mdss_dp0` and `mdss_dp1` are enabled in DT, `msm_dpu` waits for all three DisplayPort sub-components (`ae90000`, `ae98000`, `ae9a000`) before binding the internal eDP panel.
   - If `dispcc_sm8250` and `msm` are included in early `initramfs` (e.g. for Plymouth boot splash), include `surface_aggregator`, `surface_aggregator_registry`, and `surface_typec` before `dispcc_sm8250` and `msm` in `MODULES=(...)` so the DP HPD bridges are registered before `msm_dpu` probes.

## 8. `spi-hid` Cold-Boot Touchscreen Fix & Adreno 685 (`drm/msm`) Fixes

1. **`spi-hid` cold-boot reliability fix (`patches/0006` / `drivers/spi_hid/`)**:
   - Fixes the `spi-hid` cold-boot race (`linux-surface/surface-pro-x#32`) where a stale TLMM falling-edge latch during `pinctrl_sleep`/`pinctrl_reset` triggers a `00 00 00 00` header read (`-EINVAL`) before `spi_hid_deassert_reset()`, drops the real `RESET_RESP` interrupt, and power-cycles the digitizer into `D3` right as `udev`/`libinput`/`iptsd` open it:

```bash
make -C drivers/spi_hid && sudo make -C drivers/spi_hid install
sudo install -m 755 daemons/iptsd/start-surface-iptsd.sh /usr/local/bin/
sudo install -m 644 systemd/surface-iptsd.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now surface-iptsd.service
```

2. **Adreno 685 GPU, DPU, DisplayPort & DISPCC fixes (`patches/0007`)**:
   - Sets `gmem_range_min = 0` and `MSM_PARAM_GMEM_BASE = 0` on Adreno 680/685 (fixing Vulkan GMEM texture corruption), imports the `a680_hwcg` KGSL clock-gating sequence, routes GMU-offset `hwcg[]` entries via `gmu_write()`, counts `frame_pending` before `dpu_encoder_kickoff()`, fixes the DisplayPort runtime-PM reference leak on unplug, and uses `clk_rcg2_shared_no_init_park_ops` on `disp_cc_mdss_mdp_clk_src`.

3. **Hall-Sensor Lid Switch (`SW_LID`), SoundWire AHB Mutex (`patches/0008`), and `spx_tcs3430` Kernel IIO ALS (`drivers/spx_tcs3430/`)**:
   - Include [`dts/sc8180x-surface-pro-x-sq2-lid.dtsi`](../dts/sc8180x-surface-pro-x-sq2-lid.dtsi) (or apply [`dts/lid-against-typec.patch`](../dts/lid-against-typec.patch)) to enable `gpio-keys-lid` (`SW_LID` on TLMM GPIO 121) and set `als@39` `compatible = "ams,tcs3430"`.
   - Apply [`patches/0008-soundwire-qcom-serialize-wcd-ahb-bridge-and-validate-fifo.patch`](../patches/0008-soundwire-qcom-serialize-wcd-ahb-bridge-and-validate-fifo.patch) to `drivers/soundwire/qcom.c`.
   - Build and install the `spx_tcs3430` IIO ambient light sensor module:

```bash
make -C drivers/spx_tcs3430 && sudo make -C drivers/spx_tcs3430 install
```

## 9. Qualcomm Venus 4K Hardware Video Decode (`videocc-sm8150` + `venus-{core,dec}`)

See [`docs/VENUS_VIDEO_DECODE.md`](VENUS_VIDEO_DECODE.md) for architecture notes, EL1 PAS 9 details, and verified 4K SHA-256 test results.

1. **Stage `venus.mbn` firmware** (extracted from your Windows partition):
   - `/lib/firmware/qcom/venus-5.2/venus.mbn` (`VIDEO.IR.1.2-00042-PROD-1` or `VIDEO.IR.1.2-00079-PROD-2`).

2. **Apply kernel patches and build modules**:

```bash
# In your Linux v7.3 kernel tree:
git apply patches/0009-clk-qcom-videocc-sm8150-add-sc8180x-compatible.patch
git apply patches/0010-media-qcom-venus-sq2-iris1-hfi4-decoder.patch

# Build and install videocc-sm8150.ko, venus-core.ko, venus-dec.ko, venus-enc.ko
# (enable CONFIG_SM_VIDEOCC_8150=m and CONFIG_VIDEO_QCOM_VENUS=m).

# Build and install the runtime Venus DT overlay module:
make -C drivers/sq2_venus_overlay && sudo make -C drivers/sq2_venus_overlay install
```

3. **Apply the static VIDEOCC phandle DTB patch and install late-boot service**:

```bash
# Include dts/sc8180x-surface-pro-x-sq2-venus-videocc.dtsi (or apply
# dts/venus-videocc-against-lid.patch) and rebuild your DTB.

sudo cp configs/modprobe.d/venus-sq2-guard.conf /etc/modprobe.d/
sudo install -m 755 scripts/video/surface-venus-load /usr/local/bin/
sudo cp systemd/surface-venus-setup.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable surface-venus-setup.service
```

After rebooting, `/dev/video-venus-dec` exposes `qcom-venus-decoder` (V4L2 M2M) for GStreamer (`v4l2h264dec`, `v4l2h265dec`, `v4l2vp9dec`, `v4l2vp8dec`, `v4l2mpeg2dec`) and FFmpeg (`h264_v4l2m2m`, `hevc_v4l2m2m`, `vp9_v4l2m2m`).

## 10. Snapdragon X24 4G LTE Modem (`IPA v4.1` + `sq2_fsg_read` + `ModemManager`)

See [`docs/LTE_MODEM_IPA.md`](LTE_MODEM_IPA.md) for architecture notes and cold-reboot verification details.

1. **Extract your own unit's `modem_fsg` RF calibration** (one-time, directly from Linux EL1):

```bash
make -C drivers/sq2_fsg_read
sudo insmod drivers/sq2_fsg_read/sq2_fsg_read.ko allow=1
sudo dd if=/sys/kernel/debug/sq2_fsg of=/var/lib/rmtfs/modem_fsg bs=4096 status=none
sudo chmod 0600 /var/lib/rmtfs/modem_fsg
sudo rmmod sq2_fsg_read
```

2. **Stage signed `ipa_fws.elf` firmware** (from Windows `qcipa8180` or fetched via `wudrivers download --extract --only '*ipa*' --only '*fws*'`):
   - `/lib/firmware/qcom/sc8180x/sq2/ipa_fws.elf`

3. **Apply the IPA v4.1 kernel patch and build `ipa.ko`**:

```bash
# In your Linux v7.3 kernel tree (CONFIG_QCOM_IPA=m, CONFIG_RMNET=m):
git apply patches/0011-net-ipa-add-sc8180x-ipa-v4.1-support.patch
# Build and install ipa.ko into /lib/modules/$(uname -r)/updates/ && sudo depmod -a
```

4. **Apply the IPA DTB node and enable late-boot LTE & `ModemManager`**:

```bash
# Include dts/sc8180x-surface-pro-x-sq2-ipa.dtsi (or apply
# dts/ipa-against-venus.patch) and rebuild your DTB.

sudo cp configs/modprobe.d/ipa-sq2-guard.conf /etc/modprobe.d/
sudo install -m 755 scripts/lte/surface-lte-load /usr/local/bin/
sudo cp systemd/surface-lte-setup.service /etc/systemd/system/
sudo install -Dm644 systemd/ModemManager.service.d/10-after-surface-lte.conf \
    /etc/systemd/system/ModemManager.service.d/10-after-surface-lte.conf
sudo systemctl daemon-reload
sudo systemctl enable surface-lte-setup.service ModemManager.service
```

After rebooting, `mmcli -L` and NetworkManager (`qrtr0` / `rmnet_ipa0`) expose the Snapdragon X24 4G LTE modem with SIM/eSIM support and QMAP multiplexed bearers (`qmapmux0.0`).

## 11. S2idle Deep Sleep (`drm/msm`, Venus EL1 TZ PM, `spi-hid` PM & `systemd` Hooks)

See [`docs/S2IDLE_SUSPEND.md`](S2IDLE_SUSPEND.md) for architecture notes, wake sources, and verified post-resume hardware checks.

1. **Apply the `s2idle` kernel patches and rebuild `msm.ko`, `venus-core.ko`, and `spi-hid.ko`**:

```bash
# In your Linux v7.3 kernel tree (with patches/0006, 0007, 0010 already applied):
git apply patches/0012-drm-msm-sc8180x-dpu-dp-s2idle-resume.patch
git apply patches/0013-media-qcom-venus-sq2-el1-tz-s2idle-pm.patch
git apply patches/0014-hid-spi-hid-add-s2idle-system-pm-ops.patch

# Or build the updated out-of-tree spi-hid module directly:
make -C drivers/spi_hid && sudo make -C drivers/spi_hid install
sudo depmod -a

# If msm.ko is packed inside your early initramfs (e.g. Plymouth), regenerate initramfs
# after installing the updated msm.ko.
```

2. **Install `s2idle` configs, sleep hooks, and updated `iptsd` / `ModemManager` / lid scripts**:

```bash
sudo rm -f /etc/systemd/sleep.conf.d/surface-no-deep-suspend.conf
sudo install -Dm644 configs/systemd/surface-s2idle.conf /etc/systemd/sleep.conf.d/surface-s2idle.conf
sudo install -Dm644 configs/systemd/surface-lid.conf /etc/systemd/logind.conf.d/surface-lid.conf
sudo install -m 755 systemd/system-sleep/*.sh /usr/lib/systemd/system-sleep/
sudo install -m 755 daemons/iptsd/start-surface-iptsd.sh /usr/local/bin/
sudo install -m 755 daemons/posture/surface-lid-daemon.sh /usr/local/bin/
sudo install -Dm644 systemd/ModemManager.service.d/10-after-surface-lte.conf \
    /etc/systemd/system/ModemManager.service.d/10-after-surface-lte.conf
sudo systemctl daemon-reload
sudo systemctl restart ModemManager.service surface-lid-daemon.service
```

After rebooting, verify `busctl get-property org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager CanSuspend` returns `s "yes"`, and close/open the Type Cover (or press the power button) to test `s2idle` suspend and instant Hall-sensor wake.

## 12. Surface Slim Pen 2 Tactile Haptics (`UsagePage 0x0E`), White Hover Dot & Pre-Shutdown Teardown

See [`docs/SLIM_PEN_2_BLUETOOTH.md`](SLIM_PEN_2_BLUETOOTH.md) for architecture notes and BLE Link-Layer traces.

1. **Install the real-time tactile haptics daemon and udev/systemd hooks**:

```bash
sudo install -m 755 daemons/pen/surface-pen-haptics /usr/local/bin/
sudo install -m 644 configs/udev/70-surface-pen-haptics.rules /etc/udev/rules.d/
sudo install -m 644 configs/udev/99-surface-input-clean.rules /etc/udev/rules.d/
sudo install -m 644 systemd/surface-pen-haptics.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo udevadm control --reload-rules
sudo systemctl enable --now surface-pen-haptics.service
```

2. **Compile and enable the GNOME Mutter 51.1 `#5215` null-guard & tablet cursor suppressor**:

```bash
gcc -shared -fPIC -O2 -Wall -Wextra \
    -o libmutter-tablet-nullguard.so daemons/pen/mutter-5215-nullguard.c -ldl
sudo install -Dm755 libmutter-tablet-nullguard.so /usr/local/lib/libmutter-tablet-nullguard.so
rm -f libmutter-tablet-nullguard.so

mkdir -p ~/.config/systemd/user/org.gnome.Shell@user.service.d
cp configs/systemd/10-mutter-5215-nullguard.conf \
    ~/.config/systemd/user/org.gnome.Shell@user.service.d/
systemctl --user daemon-reload
```

3. **Install and enable the crisp white pen hover dot GNOME Shell extension**:

```bash
mkdir -p ~/.local/share/gnome-shell/extensions/surface-pen-dot@linux-surface
cp -r extensions/surface-pen-dot@linux-surface/* \
    ~/.local/share/gnome-shell/extensions/surface-pen-dot@linux-surface/
gnome-extensions enable surface-pen-dot@linux-surface
```

4. **Install the clean pre-shutdown Venus VPU & IPA v4.1 teardown service**:

```bash
sudo install -m 755 scripts/surface-pre-shutdown.sh /usr/local/bin/
sudo install -m 644 systemd/surface-pre-shutdown.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now surface-pre-shutdown.service
```

