#!/usr/bin/env bash
# ==============================================================================
# Surface Pro X Type Cover Posture & Lid Daemon
# Reads SAM platform hub posture (closed / laptop / tablet), blanks eDP
# backlight and locks session when closed (avoiding unstable S2idle), and
# synchronizes GNOME on-screen keyboard + tablet switch state.
# ==============================================================================

get_active_users() {
    if [ -n "${SURFACE_USER:-}" ]; then
        echo "$SURFACE_USER"
        return
    fi
    loginctl list-users --no-legend 2>/dev/null | awk '{print $2}'
}

run_user_gsettings_if_changed() {
    local schema="$1"
    local key="$2"
    local target="$3"
    local u uid dbus_addr cur
    for u in $(get_active_users); do
        uid=$(id -u "$u" 2>/dev/null || true)
        [ -n "$uid" ] && [ -S "/run/user/${uid}/bus" ] || continue
        dbus_addr="unix:path=/run/user/${uid}/bus"
        cur=$(sudo -u "$u" DBUS_SESSION_BUS_ADDRESS="$dbus_addr" gsettings get "$schema" "$key" 2>/dev/null || true)
        if [ "$cur" != "$target" ]; then
            sudo -u "$u" DBUS_SESSION_BUS_ADDRESS="$dbus_addr" gsettings set "$schema" "$key" "$target" 2>/dev/null || true
        fi
    done
}

run_user_notify() {
    local u uid dbus_addr
    for u in $(get_active_users); do
        uid=$(id -u "$u" 2>/dev/null || true)
        [ -n "$uid" ] && [ -S "/run/user/${uid}/bus" ] || continue
        dbus_addr="unix:path=/run/user/${uid}/bus"
        (sudo -u "$u" DBUS_SESSION_BUS_ADDRESS="$dbus_addr" timeout 2 notify-send -a "Surface Pro X" -t 2000 "$@" >/dev/null 2>&1) &
    done
}

set_tablet_switch_inhibit() {
    local val="$1"
    for f in /sys/devices/platform/surface_aggregator_platform_hub.0/*/input/input*/inhibited; do
        [ -w "$f" ] && echo "$val" > "$f" 2>/dev/null || true
    done
}

LAST_APPLIED_STATE=""
CANDIDATE_STATE=""
CANDIDATE_COUNT=0
REQUIRED_STABLE_TICKS=2

while true; do
    RAW_STATE=""
    STATE_FILE=""
    for _sf in /sys/devices/platform/surface_aggregator_platform_hub.0/*/state; do
        if [ -r "$_sf" ]; then
            STATE_FILE="$_sf"
            break
        fi
    done
    if [ -n "$STATE_FILE" ]; then
        IFS= read -r RAW_STATE < "$STATE_FILE" || RAW_STATE=""
        RAW_STATE="${RAW_STATE//[[:space:]]/}"
    else
        RAW_STATE="disconnected"
        for _hd in /sys/bus/hid/devices/*045E:09AE* /sys/bus/hid/devices/*045E:09B0*; do
            if [ -e "$_hd" ]; then
                RAW_STATE="laptop"
                break
            fi
        done
    fi

    if [ "$RAW_STATE" = "closed" ]; then
        MODE="closed"
    elif [ "$RAW_STATE" = "laptop" ]; then
        MODE="laptop"
    else
        MODE="tablet"
    fi

    if [ "$MODE" = "$CANDIDATE_STATE" ]; then
        CANDIDATE_COUNT=$((CANDIDATE_COUNT + 1))
    else
        CANDIDATE_STATE="$MODE"
        CANDIDATE_COUNT=1
    fi

    # Wake immediately (1 tick) when opening lid; keep 2-tick debounce for laptop <-> tablet
    NEEDED_TICKS="$REQUIRED_STABLE_TICKS"
    if [ "$LAST_APPLIED_STATE" = "closed" ] && [ "$CANDIDATE_STATE" != "closed" ]; then
        NEEDED_TICKS=1
    fi

    if [ "$CANDIDATE_COUNT" -ge "$NEEDED_TICKS" ] && [ "$CANDIDATE_STATE" != "$LAST_APPLIED_STATE" ]; then
        CUR_STATE="$CANDIDATE_STATE"
        if [ "$CUR_STATE" = "closed" ]; then
            logger -t surface-lid-daemon "Posture: closed -> Screen off, lock session, power-saver"
            set_tablet_switch_inhibit 0
            run_user_gsettings_if_changed org.gnome.desktop.a11y.applications screen-keyboard-enabled false
            loginctl lock-sessions 2>/dev/null || true
            powerprofilesctl set power-saver 2>/dev/null || true
            if [ -w /sys/class/backlight/backlight/bl_power ]; then
                echo 4 > /sys/class/backlight/backlight/bl_power 2>/dev/null || true
            fi
        elif [ "$CUR_STATE" = "laptop" ]; then
            logger -t surface-lid-daemon "Posture: laptop -> Laptop Mode active"
            if [ -w /sys/class/backlight/backlight/bl_power ]; then
                echo 0 > /sys/class/backlight/backlight/bl_power 2>/dev/null || true
            fi
            set_tablet_switch_inhibit 0
            if [ -n "$LAST_APPLIED_STATE" ]; then
                powerprofilesctl set balanced 2>/dev/null || true
                run_user_gsettings_if_changed org.gnome.desktop.a11y.applications screen-keyboard-enabled false
                if [ "$LAST_APPLIED_STATE" != "closed" ]; then
                    run_user_notify -i input-keyboard-symbolic "Laptop Mode" "Type Cover active — on-screen keyboard disabled."
                fi
            fi
        else
            logger -t surface-lid-daemon "Posture: tablet -> Tablet Mode active"
            if [ -w /sys/class/backlight/backlight/bl_power ]; then
                echo 0 > /sys/class/backlight/backlight/bl_power 2>/dev/null || true
            fi
            powerprofilesctl set balanced 2>/dev/null || true
            set_tablet_switch_inhibit 0
            run_user_gsettings_if_changed org.gnome.desktop.a11y.applications screen-keyboard-enabled true
            run_user_gsettings_if_changed org.gnome.settings-daemon.peripherals.touchscreen orientation-lock false
            if [ -n "$LAST_APPLIED_STATE" ]; then
                run_user_notify -i tablet-symbolic "Tablet Mode" "Tablet mode active — on-screen keyboard enabled."
            fi
        fi
        LAST_APPLIED_STATE="$CUR_STATE"
    fi

    sleep 1.5
done
