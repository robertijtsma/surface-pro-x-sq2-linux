#!/usr/bin/env bash
# ==============================================================================
# Surface Pro X Dynamic Power Profile Switcher
# Automatically adjusts power profile based on AC state and battery percentage.
# ==============================================================================
set -euo pipefail

CONFIG_FILE="/etc/surface-power.conf"
MIN_BAT_FOR_PERFORMANCE=50
LOW_BAT_THRESHOLD=20
AC_AUTO_PROFILE="balanced"

if [ -f "$CONFIG_FILE" ]; then
    # shellcheck source=/dev/null
    source "$CONFIG_FILE"
fi

AC_ONLINE=0
BAT_CAPACITY=100

if [ -f /sys/class/power_supply/ADP1/online ]; then
    AC_ONLINE=$(cat /sys/class/power_supply/ADP1/online 2>/dev/null || echo 0)
fi

if [ -f /sys/class/power_supply/BAT1/capacity ]; then
    BAT_CAPACITY=$(cat /sys/class/power_supply/BAT1/capacity 2>/dev/null || echo 100)
fi

CURRENT_PROFILE=$(powerprofilesctl get 2>/dev/null || echo "balanced")
TARGET_PROFILE="balanced"

if [ "$AC_ONLINE" -eq 1 ]; then
    if [ "$AC_AUTO_PROFILE" = "performance" ] && [ "$BAT_CAPACITY" -ge "$MIN_BAT_FOR_PERFORMANCE" ]; then
        TARGET_PROFILE="performance"
    else
        TARGET_PROFILE="balanced"
    fi
else
    if [ "$BAT_CAPACITY" -le "$LOW_BAT_THRESHOLD" ]; then
        TARGET_PROFILE="power-saver"
    else
        TARGET_PROFILE="balanced"
    fi
fi

if [ "$CURRENT_PROFILE" != "$TARGET_PROFILE" ]; then
    echo "surface-auto-power: Switching profile $CURRENT_PROFILE -> $TARGET_PROFILE (AC=$AC_ONLINE, Bat=${BAT_CAPACITY}%)"
    powerprofilesctl set "$TARGET_PROFILE" 2>/dev/null || true

    case "$TARGET_PROFILE" in
        performance)
            NOTIF_TITLE="Performance Mode Active"
            NOTIF_MSG="AC connected and battery at ${BAT_CAPACITY}%."
            ICON="speedometer-symbolic"
            ;;
        power-saver)
            NOTIF_TITLE="Power Saver Active"
            NOTIF_MSG="Battery at ${BAT_CAPACITY}%. Power saving enabled."
            ICON="battery-level-20-symbolic"
            ;;
        *)
            NOTIF_TITLE="Balanced Mode Active"
            NOTIF_MSG="Battery at ${BAT_CAPACITY}%. Dynamic frequency scaling enabled."
            ICON="power-profile-balanced-symbolic"
            ;;
    esac

    for u in $(loginctl list-users --no-legend 2>/dev/null | awk '{print $2}'); do
        uid=$(id -u "$u" 2>/dev/null || true)
        if [ -n "$uid" ] && [ -d "/run/user/$uid" ]; then
            sudo -u "$u" XDG_RUNTIME_DIR="/run/user/$uid" notify-send -i "$ICON" -u low "$NOTIF_TITLE" "$NOTIF_MSG" 2>/dev/null || true
        fi
    done
else
    echo "surface-auto-power: Profile already at $CURRENT_PROFILE (AC=$AC_ONLINE, Bat=${BAT_CAPACITY}%)"
fi
