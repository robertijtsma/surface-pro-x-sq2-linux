#!/usr/bin/env bash
# Cleanly unbind Qualcomm SC8180X dynamic PAS/GSI devices (Venus VPU & IPA v4.1)
# before kernel_restart() / device_shutdown() so SMMU context banks and PAS
# firmware tear down cleanly while RPMh clocks are active (prevents warm-reboot
# display/SMMU fault flicker on Boot 1).

set +e

if [ -e /sys/bus/platform/drivers/qcom-venus-decoder/aa00000.venus-sq2:video-decoder ]; then
    echo "aa00000.venus-sq2:video-decoder" > /sys/bus/platform/drivers/qcom-venus-decoder/unbind 2>/dev/null
fi

if [ -e /sys/bus/platform/drivers/qcom-venus/aa00000.venus-sq2 ]; then
    echo "aa00000.venus-sq2" > /sys/bus/platform/drivers/qcom-venus/unbind 2>/dev/null
fi

if [ -e /sys/bus/platform/drivers/ipa/1e40000.ipa ]; then
    ip link set wwan0 down 2>/dev/null
    echo "1e40000.ipa" > /sys/bus/platform/drivers/ipa/unbind 2>/dev/null
fi

exit 0
