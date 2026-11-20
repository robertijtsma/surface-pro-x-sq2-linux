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

### Out-of-Tree Helper Modules

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
sudo install -m 755 daemons/pen/surface-pen-ble-listener /usr/local/bin/
sudo install -m 755 daemons/pen/surface-pen-bluetooth-autoconnect /usr/local/bin/

sudo cp configs/udev/*.rules /etc/udev/rules.d/
sudo cp configs/iptsd/iptsd.conf /etc/iptsd.conf
sudo mkdir -p /etc/systemd/sleep.conf.d /etc/systemd/logind.conf.d
sudo cp configs/systemd/surface-no-deep-suspend.conf /etc/systemd/sleep.conf.d/
sudo cp configs/systemd/surface-lid.conf /etc/systemd/logind.conf.d/
sudo cp systemd/*.service /etc/systemd/system/
sudo systemctl daemon-reload
```

Enable the services you want (`surface-audio-boot-sync.service`, `surface-bluetooth-mac.service`, `surface-power-profile.service`, `surface-lid-daemon.service`, `surface-qmi-gps.service`, and optionally the experimental pen BLE daemons).

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

