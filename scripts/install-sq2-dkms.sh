#!/usr/bin/env bash
# Stage and install all 7 standalone Surface Pro X SQ2 out-of-tree kernel
# modules into DKMS so future kernel upgrades rebuild them automatically.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKG_NAME="surface-pro-x-sq2"
PKG_VER="1.8.0"
KVER="${1:-$(uname -r)}"
DEST="/usr/src/${PKG_NAME}-${PKG_VER}"

MODULES=(
    spi_hid
    spx_tcs3430
    sq2_s2idle_pm
    sq2_venus_overlay
    surface_cam_spmi
    surface_ldoe14_xob
    surface_typec
    tpm_qcom_qsee
)

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root (e.g. sudo $0 [kernel-version])" >&2
    exit 1
fi

if ! command -v dkms >/dev/null 2>&1; then
    echo "dkms is not installed. Install it first (e.g. pacman -S dkms)." >&2
    exit 1
fi

if [ ! -d "/lib/modules/${KVER}/build" ]; then
    echo "Kernel headers missing at /lib/modules/${KVER}/build" >&2
    exit 1
fi

# Remove any previous registration of this package cleanly
for old_ver in 1.7.0 "${PKG_VER}"; do
    if dkms status -m "${PKG_NAME}" -v "${old_ver}" | grep -q "${PKG_NAME}"; then
        dkms remove -m "${PKG_NAME}" -v "${old_ver}" --all || true
    fi
    rm -rf "/usr/src/${PKG_NAME}-${old_ver}"
done

rm -rf "${DEST}"
mkdir -p "${DEST}"
cp "${REPO_ROOT}/dkms/dkms.conf" "${REPO_ROOT}/dkms/Makefile" "${DEST}/"

for mod in "${MODULES[@]}"; do
    cp -a "${REPO_ROOT}/drivers/${mod}" "${DEST}/${mod}"
    make -C "${DEST}/${mod}" KDIR="/lib/modules/${KVER}/build" clean >/dev/null 2>&1 || true
done

dkms add -m "${PKG_NAME}" -v "${PKG_VER}"
dkms build -m "${PKG_NAME}" -v "${PKG_VER}" -k "${KVER}"
dkms install -m "${PKG_NAME}" -v "${PKG_VER}" -k "${KVER}" --force

# Clean up any legacy manual copies of the 7 DKMS-managed modules so modprobe
# resolves unambiguously to /lib/modules/${KVER}/updates/dkms/
for ko in spi-hid.ko spx_tcs3430.ko sq2_s2idle_pm.ko sq2_venus_overlay.ko surface_cam_spmi.ko surface_ldoe14_xob.ko surface_typec.ko tpm_qcom_qsee.ko; do
    rm -f "/lib/modules/${KVER}/updates/${ko}" "/lib/modules/${KVER}/extra/${ko}"
done

depmod -a "${KVER}"
echo ""
echo "=== DKMS Status ==="
dkms status -m "${PKG_NAME}"
