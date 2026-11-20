#!/usr/bin/env bash
# Surface Pro X SQ2 SLPI FastRPC & Backlight Suspend/Resume Hook
# Stops hexagonrpcd-sdsp before userspace freeze so SLPI sensor_process does not
# fault on an interrupted reverse-RPC session, and restarts it cleanly on resume.

case "${1:-}/${2:-}" in
    pre/*)
        systemctl stop hexagonrpcd-sdsp.service >/dev/null 2>&1 || true
        ;;
    post/*)
        systemctl start --no-block hexagonrpcd-sdsp.service >/dev/null 2>&1 || true
        if [ -w /sys/class/backlight/backlight/bl_power ]; then
            echo 0 > /sys/class/backlight/backlight/bl_power 2>/dev/null || true
        fi
        ;;
esac
