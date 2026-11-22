#!/usr/bin/env bash
# Unified user-space installer for Surface Pro X SQ2 (SC8180X / Arch Linux ARM64)
# Installs daemons, helper scripts, udev rules, systemd units/sleep hooks,
# libcamera SoftISP IPA tuning YAMLs, Mutter #5215 tablet cursor guard, and
# GNOME Shell extensions (surface-pen-dot & smart-osk).
#
# Usage:
#   ./scripts/install-sq2-userspace.sh --check   # Validate all source files (no root required)
#   sudo ./scripts/install-sq2-userspace.sh      # Install system + user components

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [[ "${1:-}" == "--check" ]]; then
    echo "[check] Validating shell & Python scripts..."
    while IFS= read -r -d '' f; do
        first_line="$(head -n 1 "$f" 2>/dev/null || true)"
        if [[ "$first_line" == *python* ]]; then
            python3 -m py_compile "$f"
        elif [[ "$first_line" == *bash* || "$first_line" == */sh* ]]; then
            bash -n "$f"
        fi
    done < <(find scripts daemons systemd -type f -print0)

    echo "[check] Compiling libmutter-tablet-nullguard.so..."
    gcc -shared -fPIC -O2 -Wall -Wextra \
        -o /tmp/libmutter-tablet-nullguard-test.so \
        daemons/pen/mutter-5215-nullguard.c -ldl
    rm -f /tmp/libmutter-tablet-nullguard-test.so

    echo "[check] All user-space components validated successfully."
    exit 0
fi

if [[ "$EUID" -ne 0 ]]; then
    echo "Please run with sudo: sudo $0 (or use --check for dry-run validation)" >&2
    exit 1
fi

TARGET_USER="${SUDO_USER:-$(logname 2>/dev/null || echo "")}"
TARGET_HOME=""
if [[ -n "$TARGET_USER" && "$TARGET_USER" != "root" ]]; then
    TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"
fi

echo "[1/6] Installing system daemons & helper scripts..."
install -Dm755 daemons/audio/surface-audio-boot-sync.sh /usr/local/sbin/surface-audio-boot-sync.sh
install -Dm755 daemons/bluetooth/surface-bt-mac.sh /usr/local/sbin/surface-bt-mac.sh
install -Dm755 daemons/gps/surface-qmi-gps-daemon.py /usr/local/sbin/surface-qmi-gps-daemon.py
install -Dm755 daemons/power/surface-auto-power-profile.sh /usr/local/sbin/surface-auto-power-profile.sh
install -Dm755 daemons/power/surface-power-profile /usr/local/bin/surface-power-profile
install -Dm755 daemons/display/surface-auto-brightness /usr/local/bin/surface-auto-brightness
install -Dm755 daemons/posture/surface-lid-daemon.sh /usr/local/bin/surface-lid-daemon.sh
install -Dm755 daemons/posture/surface-touch-rotate /usr/local/bin/surface-touch-rotate
install -Dm755 daemons/iptsd/start-surface-iptsd.sh /usr/local/bin/start-surface-iptsd.sh
install -Dm755 daemons/pen/surface-pen-blelc-setup /usr/local/bin/surface-pen-blelc-setup
install -Dm755 daemons/pen/surface-pen-haptics /usr/local/bin/surface-pen-haptics
install -Dm755 scripts/surface-pre-shutdown.sh /usr/local/bin/surface-pre-shutdown.sh
install -Dm755 scripts/camera/surface-camera-load /usr/local/bin/surface-camera-load
install -Dm755 scripts/camera/surface-camera-check /usr/local/bin/surface-camera-check
install -Dm755 scripts/camera/surface-camera-faceid-settings /usr/local/bin/surface-camera-faceid-settings
install -Dm755 scripts/camera/surface-camera-control /usr/local/bin/surface-camera-control
install -Dm755 scripts/video/surface-venus-load /usr/local/bin/surface-venus-load
install -Dm755 scripts/lte/surface-lte-load /usr/local/bin/surface-lte-load
install -Dm755 scripts/sensors/surface-wait-slpi-sensors /usr/local/bin/surface-wait-slpi-sensors

echo "[2/6] Building and installing libmutter-tablet-nullguard.so..."
gcc -shared -fPIC -O2 -Wall -Wextra \
    -o /usr/local/lib/libmutter-tablet-nullguard.so \
    daemons/pen/mutter-5215-nullguard.c -ldl
chmod 0755 /usr/local/lib/libmutter-tablet-nullguard.so

echo "[3/6] Installing udev, iptsd, modprobe & logind/sleep configs..."
install -Dm644 configs/iptsd/iptsd.conf /etc/iptsd.conf
install -Dm644 configs/systemd/surface-s2idle.conf /etc/systemd/sleep.conf.d/surface-s2idle.conf
install -Dm644 configs/systemd/surface-lid.conf /etc/systemd/logind.conf.d/surface-lid.conf
for f in configs/udev/*.rules; do
    install -Dm644 "$f" "/etc/udev/rules.d/$(basename "$f")"
done
for f in configs/modprobe.d/*.conf; do
    install -Dm644 "$f" "/etc/modprobe.d/$(basename "$f")"
done
for f in configs/modules-load.d/*.conf; do
    install -Dm644 "$f" "/etc/modules-load.d/$(basename "$f")"
done

echo "[4/6] Installing libcamera SoftISP IPA tuning & WirePlumber configs..."
install -Dm644 libcamera/ipa/simple/ov5693.yaml /etc/libcamera/ipa/simple/ov5693.yaml
install -Dm644 libcamera/ipa/simple/ov13858.yaml /etc/libcamera/ipa/simple/ov13858.yaml
install -Dm644 libcamera/ipa/simple/ov5693.yaml /usr/share/libcamera/ipa/simple/ov5693.yaml
install -Dm644 libcamera/ipa/simple/ov13858.yaml /usr/share/libcamera/ipa/simple/ov13858.yaml
install -Dm644 libcamera/wireplumber/10-surface-libcamera.conf /etc/wireplumber/wireplumber.conf.d/10-surface-libcamera.conf

echo "[5/6] Installing systemd units, drop-ins & system-sleep hooks..."
install -Dm644 daemons/wifi-qrtr/10-modem-preboot.conf /etc/systemd/system/rmtfs.service.d/10-modem-preboot.conf
install -Dm644 systemd/ModemManager.service.d/10-after-surface-lte.conf /etc/systemd/system/ModemManager.service.d/10-after-surface-lte.conf
install -Dm644 systemd/iio-sensor-proxy.service.d/after-hexagonrpcd.conf /etc/systemd/system/iio-sensor-proxy.service.d/after-hexagonrpcd.conf
for f in systemd/*.service; do
    install -Dm644 "$f" "/etc/systemd/system/$(basename "$f")"
done
for f in systemd/system-sleep/*.sh; do
    install -Dm755 "$f" "/usr/lib/systemd/system-sleep/$(basename "$f")"
done

systemctl daemon-reload
udevadm control --reload-rules

if [[ -n "$TARGET_HOME" && -d "$TARGET_HOME" ]]; then
    echo "[6/6] Installing user session configs & GNOME Shell extensions for $TARGET_USER..."
    sudo -u "$TARGET_USER" mkdir -p \
        "$TARGET_HOME/.config/environment.d" \
        "$TARGET_HOME/.config/wireplumber/wireplumber.conf.d" \
        "$TARGET_HOME/.config/systemd/user/org.gnome.Shell@user.service.d" \
        "$TARGET_HOME/.local/share/gnome-shell/extensions/surface-pen-dot@linux-surface"
    install -o "$TARGET_USER" -g "$TARGET_USER" -m 644 \
        libcamera/environment.d/10-surface-libcamera.conf \
        "$TARGET_HOME/.config/environment.d/10-surface-libcamera.conf"
    install -o "$TARGET_USER" -g "$TARGET_USER" -m 644 \
        libcamera/environment.d/99-surface-gpu.conf \
        "$TARGET_HOME/.config/environment.d/99-surface-gpu.conf"
    install -o "$TARGET_USER" -g "$TARGET_USER" -m 644 \
        libcamera/wireplumber/51-surface-pro-x.conf \
        "$TARGET_HOME/.config/wireplumber/wireplumber.conf.d/51-surface-pro-x.conf"
    install -o "$TARGET_USER" -g "$TARGET_USER" -m 644 \
        configs/systemd/10-mutter-5215-nullguard.conf \
        "$TARGET_HOME/.config/systemd/user/org.gnome.Shell@user.service.d/10-mutter-5215-nullguard.conf"
    for f in extensions/surface-pen-dot@linux-surface/*; do
        install -o "$TARGET_USER" -g "$TARGET_USER" -m 644 \
            "$f" "$TARGET_HOME/.local/share/gnome-shell/extensions/surface-pen-dot@linux-surface/$(basename "$f")"
    done
fi

echo "[done] Surface Pro X SQ2 user-space stack installed."
