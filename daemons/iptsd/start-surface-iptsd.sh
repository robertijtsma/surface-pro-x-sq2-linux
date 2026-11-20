#!/usr/bin/env bash
modprobe uinput 2>/dev/null || true

# Wait for the SPI HID device to report ready and keep an open fd so spi_hid stays in D0
for attempt in {1..30}; do
    if [ -r /sys/bus/spi/devices/spi0.0/ready ]; then
        if [ "$(cat /sys/bus/spi/devices/spi0.0/ready 2>/dev/null)" != "ready" ]; then
            sleep 0.25
            continue
        fi
    fi
    for devpath in /sys/bus/hid/devices/001C:045E:0C21.*; do
        [ -e "$devpath" ] || continue
        HIDRAW=$(basename "$devpath"/hidraw/hidraw* 2>/dev/null || true)
        if [ -n "$HIDRAW" ] && [ -e "/dev/$HIDRAW" ]; then
            exec 3< "/dev/$HIDRAW"
            sleep 0.35
            exec /usr/bin/iptsd "/dev/$HIDRAW"
        fi
    done
    if [ "$attempt" -eq 8 ] || [ "$attempt" -eq 18 ]; then
        modprobe -r spi_hid 2>/dev/null || true
        sleep 0.5
        modprobe spi_hid 2>/dev/null || true
    fi
    sleep 0.5
done
exit 0
