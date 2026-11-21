#!/usr/bin/env bash
# Apply Surface Pro X SQ2 in-tree kernel patches (patches/0001..0014) onto a
# clean Linux v7.3+ kernel source tree and build the patched in-tree modules.
#
# Usage:
#   ./scripts/build-sq2-kernel-modules.sh /path/to/linux-src [--install] [--vanilla-camss]
#
# By default, uses patches/0003 (for trees that already carry SM8150/SC8180X
# CAMSS, such as valpackett/linux-qclaptops or linux-surface). Pass
# --vanilla-camss to apply patches/0004 instead of patches/0003.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL_DIR="${1:-}"
shift || true

DO_INSTALL=0
VANILLA_CAMSS=0
for arg in "$@"; do
    case "$arg" in
        --install) DO_INSTALL=1 ;;
        --vanilla-camss) VANILLA_CAMSS=1 ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

if [ -z "${KERNEL_DIR}" ] || [ ! -f "${KERNEL_DIR}/Makefile" ]; then
    echo "Usage: $0 /path/to/linux-source-tree [--install] [--vanilla-camss]" >&2
    exit 1
fi

CAMSS_PATCH="0003-media-qcom-camss-privacy-leds-and-rdi-y10-sfmt.patch"
if [ "${VANILLA_CAMSS}" -eq 1 ]; then
    CAMSS_PATCH="0004-media-qcom-camss-sc8180x-titan175-full.patch"
fi

PATCHES=(
    "0001-media-i2c-ov13858-dt-flips-and-gain-defaults.patch"
    "0002-media-i2c-ov7251-continuous-clk-and-ir-strobe.patch"
    "${CAMSS_PATCH}"
    "0005-platform-surface-aggregator-registry-spx-usc.patch"
    "0006-hid-spi-hid-fix-cold-boot-reset-and-d0-ready-race.patch"
    "0007-drm-msm-a680-gmem-hwcg-mmio-and-dpu-dp-fixes.patch"
    "0008-soundwire-qcom-serialize-wcd-ahb-bridge-and-validate-fifo.patch"
    "0009-clk-qcom-videocc-sm8150-add-sc8180x-compatible.patch"
    "0010-media-qcom-venus-sq2-iris1-hfi4-decoder.patch"
    "0011-net-ipa-add-sc8180x-ipa-v4.1-support.patch"
    "0012-drm-msm-sc8180x-dpu-dp-s2idle-resume.patch"
    "0013-media-qcom-venus-sq2-el1-tz-s2idle-pm.patch"
    "0014-hid-spi-hid-add-s2idle-system-pm-ops.patch"
)

echo "=== [1/3] Checking & applying SQ2 patches in ${KERNEL_DIR} ==="
cd "${KERNEL_DIR}"
for p in "${PATCHES[@]}"; do
    pf="${REPO_ROOT}/patches/${p}"
    if git apply --reverse --check "${pf}" >/dev/null 2>&1; then
        echo "  [skip] already applied: ${p}"
    else
        echo "  [apply] ${p}"
        git apply "${pf}"
    fi
done

KVER="$(make -s kernelrelease)"
JOBS="$(nproc)"
echo "=== [2/3] Building patched in-tree modules for ${KVER} (-j${JOBS}) ==="

MOD_DIRS=(
    "drivers/media/i2c"
    "drivers/media/platform/qcom/camss"
    "drivers/platform/surface"
    "drivers/gpu/drm/msm"
    "drivers/clk/qcom"
    "drivers/soundwire"
    "drivers/media/platform/qcom/venus"
    "drivers/net/ipa"
)

for d in "${MOD_DIRS[@]}"; do
    echo "  [build] ${d}"
    make -j"${JOBS}" M="${d}" modules
done

BUILT_KOS=(
    "drivers/media/i2c/ov13858.ko"
    "drivers/media/i2c/ov7251.ko"
    "drivers/media/platform/qcom/camss/qcom-camss.ko"
    "drivers/platform/surface/surface_aggregator_registry.ko"
    "drivers/gpu/drm/msm/msm.ko"
    "drivers/clk/qcom/dispcc-sm8250.ko"
    "drivers/clk/qcom/videocc-sm8150.ko"
    "drivers/soundwire/soundwire-qcom.ko"
    "drivers/media/platform/qcom/venus/venus-core.ko"
    "drivers/media/platform/qcom/venus/venus-dec.ko"
    "drivers/media/platform/qcom/venus/venus-enc.ko"
    "drivers/net/ipa/ipa.ko"
)

echo "=== [3/3] Built modules ==="
for ko in "${BUILT_KOS[@]}"; do
    if [ -f "${ko}" ]; then
        echo "  [ok] ${ko}"
    else
        echo "  [warn] missing (check .config): ${ko}"
    fi
done

if [ "${DO_INSTALL}" -eq 1 ]; then
    echo "=== Installing into /lib/modules/${KVER}/updates/ ==="
    for ko in "${BUILT_KOS[@]}"; do
        [ -f "${ko}" ] && sudo install -Dm644 "${ko}" "/lib/modules/${KVER}/updates/$(basename "${ko}")"
    done
    sudo depmod -a "${KVER}"
    echo "[done] Installed patched modules for ${KVER}. Remember to rebuild initramfs if msm.ko is packed inside it."
fi
