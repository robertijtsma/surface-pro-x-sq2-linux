#!/bin/bash
# Surface Pro X (SQ2 / SC8180X) Surface Pen BLE-LC & Hardware Accept-List Resume Hook
# Re-arms WCN3998 LE hardware Accept-List and re-syncs UsagePage 0xFFF4 Host BT MAC
# after s2idle resume, deferred via systemd-run so user.slice thaws immediately.

case "${1:-}/${2:-}" in
    post/*)
        systemd-run --no-block --collect --on-active=2s \
            --description="Surface Pro X post-resume pen BLE-LC & Accept-List sync" \
            /usr/local/bin/surface-pen-blelc-setup --rearm >/dev/null 2>&1 || true
        ;;
esac
