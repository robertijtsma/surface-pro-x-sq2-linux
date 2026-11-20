#!/usr/bin/env bash
# Surface Pro X (SQ2 / SC8180X) Audio Suspend & Resume Hook
# Defer post-resume sync via systemd-run so systemd-sleep can thaw user.slice
# before surface-audio-boot-sync.sh queries PipeWire/WirePlumber.

case "${1:-}/${2:-}" in
    post/*)
        systemd-run --no-block --collect --on-active=2s \
            --description="Surface Pro X post-resume audio sync" \
            /usr/local/sbin/surface-audio-boot-sync.sh >/dev/null 2>&1 || true
        ;;
esac
