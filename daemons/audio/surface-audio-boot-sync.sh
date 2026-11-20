#!/usr/bin/env bash
# ==============================================================================
# Surface Pro X (SQ2 / SC8180X) Audio & DSP Boot Synchronization & Recovery
# Handles ADSP remoteproc sync, SLIMbus NGD QMI link recovery, SoundWire WSA8815
# amplifier attachment, LDO14E XOB rail enablement, and hardware Smart Boost.
# ==============================================================================
set -euo pipefail

check_health() {
    # 1. Check ADSP state
    if [ -e /sys/class/remoteproc/remoteproc2/state ]; then
        local adsp_state
        adsp_state=$(cat /sys/class/remoteproc/remoteproc2/state 2>/dev/null || true)
        if [ "$adsp_state" != "running" ]; then
            return 1
        fi
    fi

    # 2. Check SLIMbus NGD controller
    local last_slim_msg
    last_slim_msg=$(dmesg | grep "171c0000.slim-ngd" | tail -n 1 || true)
    if echo "$last_slim_msg" | grep -qE "QMI wait timeout|timeout"; then
        return 2
    fi

    # 3. Check SoundWire WSA8815 slave amp statuses
    local s3 s4
    s3=$(cat /sys/bus/soundwire/devices/sdw:0:0:0217:2110:00:3/status 2>/dev/null || true)
    s4=$(cat /sys/bus/soundwire/devices/sdw:0:0:0217:2110:00:4/status 2>/dev/null || true)
    if [ "$s3" != "Attached" ] || [ "$s4" != "Attached" ]; then
        return 3
    fi

    # 4. Check Sound Card presence
    if ! grep -q "sdm845" /proc/asound/cards 2>/dev/null; then
        return 4
    fi

    return 0
}

apply_hardware_boost() {
    amixer -D hw:0 cset "name='SpkrLeft BOOST Switch'" 1 >/dev/null 2>&1 || true
    amixer -D hw:0 cset "name='SpkrRight BOOST Switch'" 1 >/dev/null 2>&1 || true
    amixer -D hw:0 cset "name='SpkrLeft Smart Boost Level'" "8.500 V" >/dev/null 2>&1 || true
    amixer -D hw:0 cset "name='SpkrRight Smart Boost Level'" "8.500 V" >/dev/null 2>&1 || true
    amixer -D hw:0 cset "name='SpkrLeft PA Volume'" 8 >/dev/null 2>&1 || true
    amixer -D hw:0 cset "name='SpkrRight PA Volume'" 8 >/dev/null 2>&1 || true
    amixer -D hw:0 cset "name='RX7 Digital Volume'" 84 >/dev/null 2>&1 || true
    amixer -D hw:0 cset "name='RX8 Digital Volume'" 84 >/dev/null 2>&1 || true
    amixer -D hw:0 cset "name='COMP7 Switch'" 1 >/dev/null 2>&1 || true
    amixer -D hw:0 cset "name='COMP8 Switch'" 1 >/dev/null 2>&1 || true
    alsactl store 0 >/dev/null 2>&1 || true
}

ensure_wireplumber_levels() {
    for u in $(loginctl list-users --no-legend 2>/dev/null | awk '{print $2}'); do
        local uid
        uid=$(id -u "$u" 2>/dev/null || true)
        if [ -n "$uid" ] && [ -d "/run/user/$uid" ]; then
            local spk_id
            spk_id=$(sudo -u "$u" XDG_RUNTIME_DIR="/run/user/$uid" wpctl status 2>/dev/null | grep -iE "Speaker" | grep -oE "[0-9]+" | head -n 1 || true)
            if [ -n "$spk_id" ]; then
                sudo -u "$u" XDG_RUNTIME_DIR="/run/user/$uid" wpctl set-volume "$spk_id" 1.0 >/dev/null 2>&1 || true
            fi
        fi
    done
}

do_recovery() {
    # Serialize boot-sync and watchdog so sound card unbind never races (prevents snd_soc_jack_report oops)
    exec 9>/run/surface-audio-recovery.lock
    if ! flock -n 9; then
        echo "surface-audio: Recovery already running elsewhere; skipping."
        exec 9>&-
        return 0
    fi
    trap 'flock -u 9 2>/dev/null || true; exec 9>&- 2>/dev/null || true' RETURN
    echo "surface-audio: Starting audio hardware sync and recovery sequence..."

    # 1. Ensure SQ2 LDO14E XOB speaker rail module is loaded
    if ! lsmod | grep -q "^surface_ldoe14_xob"; then
        modprobe surface_ldoe14_xob 2>/dev/null || true
    fi

    # 2. Wait up to 15s for ADSP to reach running state
    for _ in $(seq 1 15); do
        if [ -e /sys/class/remoteproc/remoteproc2/state ] && \
           [ "$(cat /sys/class/remoteproc/remoteproc2/state 2>/dev/null)" = "running" ]; then
            break
        fi
        sleep 1
    done

    # 3. Cycle SLIMbus NGD and sound card driver to establish clean QMI link
    echo sound > /sys/bus/platform/drivers/msm-snd-sdm845/unbind 2>/dev/null || true
    echo 171c0000.slim-ngd > /sys/bus/platform/drivers/qcom,slim-ngd-ctrl/unbind 2>/dev/null || true
    sleep 1
    echo 171c0000.slim-ngd > /sys/bus/platform/drivers/qcom,slim-ngd-ctrl/bind 2>/dev/null || true
    sleep 2
    echo sound > /sys/bus/platform/drivers/msm-snd-sdm845/bind 2>/dev/null || true
    sleep 1

    # 4. Set up ALSA UCM symlinks if needed
    local ln_name
    ln_name=$(awk 'NR==2 { $1 = $1; print }' /proc/asound/cards 2>/dev/null || true)
    if [ -n "$ln_name" ]; then
        mkdir -p /usr/share/alsa/ucm2/conf.d/sdm845
        ln -sf "../../Microsoft/Surface-Pro-X/Surface-Pro-X.conf" "/usr/share/alsa/ucm2/conf.d/sdm845/$ln_name.conf" 2>/dev/null || true
        ln -sf sdm845 /usr/share/alsa/ucm2/conf.d/SurfaceProX 2>/dev/null || true
        ln -sf sdm845 /usr/share/alsa/ucm2/conf.d/Surface-Pro-X 2>/dev/null || true
    fi

    # 5. Pin SoundWire master and WSA8815 speaker amps to 'on'
    for f in /sys/bus/platform/devices/wcd934x-soundwire.*/power/control \
             /sys/bus/soundwire/devices/sdw:*:0217:2110:*/power/control; do
        [ -e "$f" ] && echo on > "$f" 2>/dev/null || true
    done

    # 6. Wait until both SoundWire slaves report "Attached"
    for _ in $(seq 1 30); do
        local s3 s4
        s3=$(cat /sys/bus/soundwire/devices/sdw:0:0:0217:2110:00:3/status 2>/dev/null || true)
        s4=$(cat /sys/bus/soundwire/devices/sdw:0:0:0217:2110:00:4/status 2>/dev/null || true)
        if [ "$s3" = "Attached" ] && [ "$s4" = "Attached" ]; then
            break
        fi
        sleep 0.2
    done

    # 7. Apply hardware boost rail & amplifier gain
    apply_hardware_boost

    # 8. Restore ALSA mixer state
    alsactl restore 0 >/dev/null 2>&1 || true

    # 9. Refresh user PipeWire / WirePlumber sessions
    for u in $(loginctl list-users --no-legend 2>/dev/null | awk '{print $2}'); do
        local uid
        uid=$(id -u "$u" 2>/dev/null || true)
        if [ -n "$uid" ] && [ -d "/run/user/$uid" ]; then
            sudo -u "$u" XDG_RUNTIME_DIR="/run/user/$uid" systemctl --user restart pipewire wireplumber >/dev/null 2>&1 || true
        fi
    done

    sleep 2
    ensure_wireplumber_levels

    echo "surface-audio: Hardware sync and recovery complete."
    flock -u 9 2>/dev/null || true
    exec 9>&- 2>/dev/null || true
}

case "${1:-}" in
    --check)
        if check_health; then
            echo "HEALTHY"
            exit 0
        else
            echo "DEGRADED"
            exit 1
        fi
        ;;
    --recover|--force)
        do_recovery
        exit 0
        ;;
    --watchdog)
        echo "surface-audio: Watchdog started (20s interval, initial delay 60s)..."
        sleep 60
        while true; do
            if ! check_health; then
                echo "surface-audio: Anomaly detected; triggering recovery..."
                do_recovery || true
            fi
            sleep 20
        done
        ;;
    *)
        if check_health; then
            apply_hardware_boost
            ensure_wireplumber_levels
            echo "surface-audio: Audio healthy; boost and mixer verified."
            exit 0
        else
            do_recovery
            exit 0
        fi
        ;;
esac
