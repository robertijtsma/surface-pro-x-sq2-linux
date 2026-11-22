#!/usr/bin/env bash
# Cleanly unbind Qualcomm SC8180X dynamic PAS/GSI devices (Venus VPU & IPA v4.1)
# before kernel_restart() / device_shutdown() so SMMU context banks and PAS
# firmware tear down cleanly while RPMh clocks are active (prevents warm-reboot
# display/SMMU fault flicker on Boot 1).

set +e

logger -t surface-pre-shutdown "Unbinding Venus VPU and IPA v4.1 before shutdown/reboot..."

if [ -e /sys/bus/platform/drivers/qcom-venus-decoder/aa00000.venus-sq2:video-decoder ]; then
    echo "aa00000.venus-sq2:video-decoder" > /sys/bus/platform/drivers/qcom-venus-decoder/unbind 2>/dev/null
fi

if [ -e /sys/bus/platform/drivers/qcom-venus/aa00000.venus-sq2 ]; then
    echo "aa00000.venus-sq2" > /sys/bus/platform/drivers/qcom-venus/unbind 2>/dev/null
fi

if [ -e /sys/bus/platform/drivers/ipa/1e40000.ipa ]; then
    ip link set rmnet_ipa0 down 2>/dev/null
    ip link set wwan0 down 2>/dev/null
    echo "1e40000.ipa" > /sys/bus/platform/drivers/ipa/unbind 2>/dev/null
fi

logger -t surface-pre-shutdown "Clean Venus/IPA pre-shutdown teardown complete."
exit 0
