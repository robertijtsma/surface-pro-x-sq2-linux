#!/usr/bin/env bash
modprobe uinput 2>/dev/null || true

# Wait for the SPI HID device and keep an open fd so spi_hid stays in D0 ready state
for attempt in {1..30}; do
    for devpath in /sys/bus/hid/devices/001C:045E:0C21.*; do
        [ -e "$devpath" ] || continue
        HIDRAW=$(basename "$devpath"/hidraw/hidraw* 2>/dev/null || true)
        if [ -n "$HIDRAW" ] && [ -e "/dev/$HIDRAW" ]; then
            # Hold /dev/$HIDRAW open on fd 3 so spi_hid transitions d3 -> d0 and stays in d0
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
