# Surface Slim Pen 2 Bluetooth Auto-Connect & BLE-LC (Linux 7.3)

This document details the root cause and complete Linux fix for **Surface Slim Pen 2 Bluetooth auto-reconnection** (`linux-surface/linux-surface#1656`) on the **Microsoft Surface Pro X SQ2 (`WCN3998`, Linux `7.3.1-1-surface`, BlueZ `5.87`)**, achieving native Windows-grade auto-connect on **1× short top-button click**, **Type Cover charger lift (no button press)**, **post-`s2idle` resume**, and **cold reboot** — with **zero** background `StartDiscovery()` or polling.

> **Note:** IPTS stylus writing, hover, 4096-level pressure, tilt, eraser, and side button operate over SPI-HID (`045E:0C21` via `iptsd`) independently of Bluetooth. Bluetooth provides the top button (`Super+F20` / double-click / long-press), live battery reporting (`UPower`), and HID-over-GATT telemetry.

## Root Cause (`linux-surface/linux-surface#1656`)

On default Linux BlueZ installations, `/etc/bluetooth/main.conf` defaults to `Privacy = off`.

1. During SMP pairing (`BLE_GAP_EVT_AUTH_STATUS`), the Surface Slim Pen 2 firmware inspects the host's distributed key bitmask (`kdist_peer.id`, bit 1 = Host Local IRK).
2. **With `Privacy = off` (default Linux)**: BlueZ does not distribute a Local IRK (`kdist_peer.id = 0`) and uses a public identity address. The pen leaves its internal bond-slot validity bitmask unset (`0xFF`) in flash.
3. On subsequent **1× top-button clicks** or **Type Cover charger lifts**, the pen's reconnect routine checks the bond-slot flag bits before advertising, treats `0xFF` as "no valid privacy-capable bond", and **aborts immediately without transmitting any BLE advertising packets**. Only holding the top button for 5–7 s (pairing mode) bypasses this check.
4. **With `Privacy = device` (Windows behavior)**: BlueZ generates and persists a 16-byte Local IRK in `/var/lib/bluetooth/<HOST_MAC>/identity`, enables WCN3998 controller privacy (`privacy` + `ll-privacy`), and distributes the Local IRK (`kdist_peer.id = 1`) during SMP pairing. The pen marks the bond valid (`0xFE`) and immediately transmits rotating-RPA `ADV_IND` bursts on every 1× top-button click and every charger lift.

## Architecture

1. **BlueZ Device Privacy & Hardware Resolving List** ([`configs/bluetooth/main.conf.snippet`](../configs/bluetooth/main.conf.snippet)):
   - `Privacy = device` enables Local IRK generation/persistence and WCN3998 Link-Layer Privacy (`ll-privacy`) while remaining fully compatible with legacy Bluetooth devices that advertise identity addresses.
   - WCN3998 resolves the Slim Pen 2's rotating Resolvable Private Addresses (RPAs) directly in controller hardware using the Peer IRK + Local IRK loaded by BlueZ.
2. **Microsoft `UsagePage 0xFFF4` BLE-LC Handshake & Hardware Accept-List** ([`daemons/pen/surface-pen-blelc-setup`](../daemons/pen/surface-pen-blelc-setup)):
   - Dynamically parses the HID report descriptors of all Microsoft (`045E:*`) `/dev/hidraw*` devices regardless of enumeration order, locating `UsagePage 0xFFF4` Feature reports (`Usage 0x10`/`0x22` capability flags and `Usage 0x03`/`0x09` 7-byte Host Bluetooth Address).
   - Sends **write-only** `HIDIOCSFEATURE` (`0x70` + `0x56`) to both the Type Cover Slim Pen Charger (`045E:09B0`, activating live NFC `0x6E`/`0x14`/`0x5C` reports) and the SPI-HID digitizer (`045E:0C21`).
   - **Never** calls `HIDIOCGFEATURE` (`GET_FEATURE`) on report `0x56`: report `0x56` is write-only on `045E:0C21`, and reading it triggers a `spi_hid` timeout and hardware reset.
   - Sets BlueZ `Trusted = true` and `WakeAllowed = true`, and arms the WCN3998 hardware LE Accept-List (`btmgmt add-device -a 2`, `set-flags -f 7`) so the controller passively listens and connects in <1 s with `Discovering: no`.
3. **Boot, Hotplug, Input Hygiene & `s2idle` Hooks**:
   - [`systemd/surface-pen-blelc.service`](../systemd/surface-pen-blelc.service): runs `surface-pen-blelc-setup --rearm` after `bluetooth.service` and `surface-bluetooth-mac.service` (~300 ms, no ordering cycle with `surface-iptsd.service`).
   - [`configs/udev/98-surface-pen-blelc.rules`](../configs/udev/98-surface-pen-blelc.rules): re-syncs the Type Cover charger (`045E:09B0`) via `--hid-only` whenever the keyboard is detached and reattached.
   - [`configs/udev/99-surface-input-clean.rules`](../configs/udev/99-surface-input-clean.rules): sets `LIBINPUT_IGNORE_DEVICE="1"` on the dummy Bluetooth evdev nodes (`Surface Slim Pen 2 Pen` and `Surface Slim Pen 2`), preventing GNOME/Mutter 51 `SIGSEGV` crashes in `meta_wayland_tablet_tool_set_focus()` when BLE sleep/wake hotplugs coincide with `iptsd` stylus contact — while keeping `Surface Slim Pen 2 Keyboard` (`Super+F20`) and `/dev/hidraw*` (`UsagePage 0x0E` Simple Haptics Controller) fully active.
   - [`systemd/system-sleep/surface-pen.sh`](../systemd/system-sleep/surface-pen.sh): schedules a non-blocking `systemd-run --on-active=2s` `--rearm` after `s2idle` resume.

## Setup Instructions

1. **Enable BlueZ Device Privacy** in `/etc/bluetooth/main.conf` (see [`configs/bluetooth/main.conf.snippet`](../configs/bluetooth/main.conf.snippet)):

```ini
[General]
FastConnectable = true
Privacy = device

[Policy]
AutoEnable = true
```

2. **Install the BLE-LC & hardware Accept-List setup and disable legacy polling services**:

```bash
sudo install -m 755 daemons/pen/surface-pen-blelc-setup /usr/local/bin/
sudo install -m 644 systemd/surface-pen-blelc.service /etc/systemd/system/
sudo install -m 644 configs/udev/98-surface-pen-blelc.rules /etc/udev/rules.d/
sudo install -m 755 systemd/system-sleep/surface-pen.sh /usr/lib/systemd/system-sleep/

sudo systemctl daemon-reload
sudo udevadm control --reload-rules
sudo systemctl disable --now \
    surface-pen-ble-listener.service \
    surface-pen-bluetooth-autoconnect.service 2>/dev/null || true
sudo systemctl enable surface-pen-blelc.service
sudo systemctl restart bluetooth.service
```

Verify `btmgmt info` shows both `privacy` and `ll-privacy` in `current settings`.

3. **One-time IRK re-pair of Surface Slim Pen 2** (mandatory if the pen was previously paired while `Privacy = off`):

```bash
# Remove the old non-IRK bond (if present)
bluetoothctl remove <PEN_MAC>

# Enable pairing mode on Linux (with Privacy = device, BlueZ defaults Pairable to no until enabled)
bluetoothctl pairable on

# Hold the Slim Pen 2 top button for 5–7 s until the LED blinks white, then pair & trust:
bluetoothctl --timeout 15 scan on
bluetoothctl pair <PEN_MAC>
bluetoothctl trust <PEN_MAC>
bluetoothctl pairable off

# Reload WCN3998 hardware Resolving List + arm Accept-List + BLE-LC HID sync
sudo systemctl restart bluetooth.service
sudo /usr/local/bin/surface-pen-blelc-setup --rearm
```

## Expected Runtime Behavior

- **1× short top-button click**: connects automatically in <1 s (`Discovering: no`), exposes `Surface Slim Pen 2 Keyboard` (`uhid`) + battery level (`UPower`), and delivers `Super+F20`.
- **Type Cover charger dock → lift (no button press)**: connects automatically in <1 s upon lifting the pen from the magnetic charging tray.
- **Native battery-saving BLE sleep**: identical to Windows, the Slim Pen 2 firmware intentionally closes its BLE link (`Disconnect Reason 0x13`) after ~10–15 s of BLE inactivity to conserve its small battery, and wakes instantly on the next button press or charger lift.
- **Cold reboot & `s2idle` verified (`7.3.1-1-surface`)**:
  - `btmgmt info` retains `privacy ll-privacy` across cold boot and `s2idle`.
  - `surface-pen-blelc.service` completes in ~300 ms on cold boot (`29 ms` post-`s2idle`) with 0 failed systemd units, 0 ordering cycles, and `spi_hid` `bus_error_count=0, logic_error_count=0`.
  - `IPTSD Virtual Touchscreen` and `IPTSD Virtual Stylus` remain clean (`active_keys=[]`) with no latched stylus cursor.
