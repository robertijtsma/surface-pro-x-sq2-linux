#!/bin/bash
MID=$(cat /etc/machine-id)
MAC="28:${MID:0:2}:${MID:2:2}:${MID:4:2}:${MID:6:2}:${MID:8:2}"

for i in $(seq 1 40); do
    if bluetoothctl list 2>/dev/null | grep -q "Controller"; then
        bluetoothctl power on >/dev/null 2>&1 || true
        exit 0
    fi
    if btmgmt < /dev/null --timeout 2 extinfo 2>/dev/null | grep -q "missing options: public-address"; then
        btmgmt < /dev/null --timeout 3 --index 0 public-addr "$MAC" >/dev/null 2>&1 || true
        sleep 0.5
        btmgmt < /dev/null --timeout 3 --index 0 power on >/dev/null 2>&1 || true
        systemctl restart bluetooth.service || true
    fi
    sleep 1
done
