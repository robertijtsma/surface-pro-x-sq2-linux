#!/usr/bin/env bash
# Surface Pro X (SQ2 / SC8180X) IPTS Touchscreen Suspend & Resume Hook
# Stops iptsd cleanly before spi_hid_suspend powers down the digitizer, and
# starts iptsd asynchronously after resume so spi_hid completes its D0 reset
# and user.slice is never blocked.

case "${1:-}/${2:-}" in
    pre/*)
        systemctl stop surface-iptsd.service >/dev/null 2>&1 || true
        ;;
    post/*)
        systemd-run --no-block --collect --on-active=1s \
            --description="Surface Pro X post-resume IPTS touch start" \
            systemctl start surface-iptsd.service >/dev/null 2>&1 || true
        ;;
esac
