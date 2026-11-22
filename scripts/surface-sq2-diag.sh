#!/usr/bin/env bash
# Surface Pro X SQ2 (SC8180X) Complete 15-Subsystem Hardware Health Check
set -euo pipefail

ok()   { printf "  [\033[1;32m OK \033[0m] %-36s %s\n" "$1" "$2"; }
warn() { printf "  [\033[1;33mWARN\033[0m] %-36s %s\n" "$1" "$2"; }
has_mod() { grep -q "^$1 " /proc/modules; }

echo "=== Surface Pro X SQ2 (SC8180X) Hardware Status ($(uname -r)) ==="

# 1. Cameras (OV5693, OV13858, OV7251)
if [ -e /dev/video-front ] && [ -e /dev/video-rear ] && [ -e /dev/video-ir ]; then
    ok "1. Cameras (Front/Rear/IR)" "$(readlink /dev/video-front), $(readlink /dev/video-rear), $(readlink /dev/video-ir)"
else
    warn "1. Cameras (Front/Rear/IR)" "Missing /dev/video-{front,rear,ir} symlinks"
fi

# 2. Windows Hello IR Face ID (Howdy)
if [ -e /usr/lib/security/pam_howdy.so ] || command -v howdy >/dev/null 2>&1; then
    ok "2. Windows Hello IR Face ID" "pam_howdy.so active (/dev/video-ir Y10)"
else
    warn "2. Windows Hello IR Face ID" "Howdy PAM module not installed"
fi

# 3. Audio (WCD934x + WSA8815 + surface_ldoe14_xob)
if has_mod surface_ldoe14_xob && [ -d /proc/asound/card0 ]; then
    ok "3. Stereo Speakers & Mics" "surface_ldoe14_xob + ALSA card0 active"
else
    warn "3. Stereo Speakers & Mics" "surface_ldoe14_xob or card0 missing"
fi

# 4. Wi-Fi (WCN3998 / ath10k_snoc)
if [ -d /sys/class/net/wlan0 ]; then
    ok "4. Wi-Fi (WCN3998 ath10k)" "wlan0 ($(cat /sys/class/net/wlan0/operstate 2>/dev/null || echo unknown))"
else
    warn "4. Wi-Fi (WCN3998 ath10k)" "wlan0 interface not found"
fi

# 5. Bluetooth & Privacy = device (Local IRK)
if systemctl is-active --quiet bluetooth.service && grep -qE "^[[:space:]]*Privacy[[:space:]]*=[[:space:]]*device" /etc/bluetooth/main.conf 2>/dev/null; then
    ok "5. Bluetooth (WCN3998 + IRK)" "bluetooth.service active, Privacy = device"
else
    warn "5. Bluetooth (WCN3998 + IRK)" "Check bluetooth.service or Privacy = device in /etc/bluetooth/main.conf"
fi

# 6. Touchscreen & Stylus Digitizer (spi-hid + iptsd)
if [ -f /sys/bus/spi/devices/spi0.0/ready ] && systemctl is-active --quiet surface-iptsd.service; then
    ok "6. Touchscreen & IPTS Digitizer" "spi0.0 ready=$(cat /sys/bus/spi/devices/spi0.0/ready 2>/dev/null), surface-iptsd active"
else
    warn "6. Touchscreen & IPTS Digitizer" "spi0.0 or surface-iptsd.service not active"
fi

# 7. Slim Pen 2 BLE-LC, Tactile Haptics & Hover Dot
if systemctl is-active --quiet surface-pen-haptics.service && [ -f /usr/local/lib/libmutter-tablet-nullguard.so ]; then
    ok "7. Slim Pen 2 Haptics & Hover Dot" "surface-pen-haptics active + libmutter-tablet-nullguard.so loaded"
else
    warn "7. Slim Pen 2 Haptics & Hover Dot" "surface-pen-haptics or libmutter-tablet-nullguard.so missing"
fi

# 8. Display & Adreno 685 GPU
if [ -e /sys/class/drm/card0-eDP-1/status ]; then
    ok "8. Display & Adreno 685 GPU" "card0-eDP-1 ($(cat /sys/class/drm/card0-eDP-1/status))"
else
    warn "8. Display & Adreno 685 GPU" "card0-eDP-1 not found"
fi

# 9. USB-C Orientation & DisplayPort Alt Mode (surface_typec)
if has_mod surface_typec; then
    ok "9. USB-C PD & DP Alt Mode" "surface_typec module active (DP-1/DP-2)"
else
    warn "9. USB-C PD & DP Alt Mode" "surface_typec module not loaded"
fi

# 10. Ambient Light Sensor (spx_tcs3430)
if has_mod spx_tcs3430; then
    LUX="$(cat /sys/bus/iio/devices/iio:device*/in_illuminance_input 2>/dev/null | head -n1 || echo '?')"
    ok "10. Ambient Light Sensor (TCS3430)" "spx_tcs3430 active (${LUX} lux)"
else
    warn "10. Ambient Light Sensor (TCS3430)" "spx_tcs3430 module not loaded"
fi

# 11. SLPI IMU Accelerometer & Compass
if systemctl is-active --quiet hexagonrpcd-sdsp.service; then
    ok "11. SLPI Sensors (IMU & Compass)" "hexagonrpcd-sdsp.service active"
else
    warn "11. SLPI Sensors (IMU & Compass)" "hexagonrpcd-sdsp.service inactive"
fi

# 12. Hardware TPM 2.0 (QSEE fTPM)
if [ -e /dev/tpm0 ] && [ -e /dev/tpmrm0 ]; then
    ok "12. Hardware TPM 2.0 (QSEE fTPM)" "/dev/tpm0 & /dev/tpmrm0 active"
else
    warn "12. Hardware TPM 2.0 (QSEE fTPM)" "/dev/tpm0 not present"
fi

# 13. Hardware Video Decode (Qualcomm Venus 4K HFI4)
if [ -e /dev/video-venus-dec ]; then
    ok "13. Venus 4K Video Decoder" "/dev/video-venus-dec -> $(readlink /dev/video-venus-dec)"
else
    warn "13. Venus 4K Video Decoder" "/dev/video-venus-dec missing"
fi

# 14. 4G LTE Modem (Snapdragon X24 + IPA v4.1)
if [ -d /sys/class/net/rmnet_ipa0 ] || has_mod ipa; then
    ok "14. 4G LTE Modem (X24 / IPA v4.1)" "rmnet_ipa0 & ModemManager active"
else
    warn "14. 4G LTE Modem (X24 / IPA v4.1)" "rmnet_ipa0 interface not found"
fi

# 15. S2idle Deep Sleep & Pre-Shutdown Teardown
if has_mod sq2_s2idle_pm; then
    warn "15. S2idle Suspend & Shutdown" "DEPRECATED sq2_s2idle_pm is loaded — remove it (causes GENI/SPI/BT -110 hangs)!"
elif grep -q s2idle /sys/power/mem_sleep 2>/dev/null && systemctl is-enabled --quiet surface-pre-shutdown.service 2>/dev/null; then
    ok "15. S2idle Suspend & Shutdown" "mem_sleep=s2idle + surface-pre-shutdown enabled"
else
    warn "15. S2idle Suspend & Shutdown" "s2idle or surface-pre-shutdown.service not configured"
fi
