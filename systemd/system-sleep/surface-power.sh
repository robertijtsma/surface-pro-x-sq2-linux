#!/usr/bin/env bash
# Surface Pro X (SQ2 / SC8180X) Power Profile Suspend & Resume Hook
# Defer via systemd-run so systemd-sleep thaws user.slice before D-Bus notifications.

case "${1:-}/${2:-}" in
    post/*)
        systemd-run --no-block --collect --on-active=2s \
            --description="Surface Pro X post-resume power profile sync" \
            /usr/local/sbin/surface-auto-power-profile.sh >/dev/null 2>&1 || true
        ;;
esac
