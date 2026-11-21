# S2idle Deep Sleep on Surface Pro X SQ2 (SC8180X / Linux 7.3)

This document describes the kernel fixes, `systemd` sleep hooks, and hardware verification for **`s2idle` suspend and resume** (`linux-surface/surface-pro-x#29`) on the **Microsoft Surface Pro X SQ2 (Qualcomm SC8180X)** running Arch Linux ARM64 (`7.3.1-1-surface`, EL1 boot).

## Kernel Fixes Required for Reliable `s2idle`

Linux 7.3 already carries the SC8180X `gcc-sc8180x.c` `PWRSTS_RET_ON` flags on `usb30_{prim,sec}_gdsc` and `pcie_{0,1,2,3}_gdsc` as well as `qcom_pcie_{suspend,resume}_noirq`, so NVMe PCIe Gen.3 x2 and USB-C controllers suspend and resume cleanly. Three remaining subsystem gaps prevented clean `s2idle` on SQ2 and are fixed in `patches/0012..0014`:

1. **Display (`drm/msm` DPU & DP AUX) — [`patches/0012-drm-msm-sc8180x-dpu-dp-s2idle-resume.patch`](../patches/0012-drm-msm-sc8180x-dpu-dp-s2idle-resume.patch)**:
   - `dpu_kms.c`: extracts `dpu_kms_setup_dp_phy_intf_sel()` and calls it in `dpu_runtime_resume()` so `MDP_DP_PHY_INTF_SEL` is reprogrammed after `MDSS_GDSC` collapses during `s2idle`.
   - `dp_aux.c`: clears `REG_DP_AUX_TRANS_CTRL` in `msm_dp_aux_reset()`, enables `DP_DP_HPD_REFTIMER_ENABLE` in `msm_dp_aux_enable()`, and checks `aux->enable_xfers` when external HPD bridges (`surface_typec`) leave `DP_DP_HPD_CTRL_HPD_EN` clear.

2. **Qualcomm Venus EL1 TrustZone PM — [`patches/0013-media-qcom-venus-sq2-el1-tz-s2idle-pm.patch`](../patches/0013-media-qcom-venus-sq2-el1-tz-s2idle-pm.patch)**:
   - In standard EL1 boot (`no_tz=0`), PAS 9 (`venus.mbn`) cannot be shut down and reloaded safely across suspend without TrustZone teardown issues, and the baseline `venus-sq2-system-pm.h` rejected `!sq2_no_tz_active` with `-EOPNOTSUPP`.
   - Suspend sequence (`venus_sq2_pm_stop_tz`): `HFI PC_PREP` (`venus_hfi_prepare_system_stop`) → IRQ mask → `qcom_pas_set_remote_state(0, 0)` (TZ suspends Venus ARM9 without releasing PAS 9) → `venus_halt_axi()` → clocks off & ICC zeroed (`sq2_venus_platform_system_quiet_tz`), allowing `venus_gdsc`, `vcodec0_gdsc`, and `mmcx` to collapse cleanly in `genpd_suspend_noirq`.
   - Resume sequence (`venus_sq2_pm_start_tz`): `genpd_resume_noirq` restores domains → ICC restore + reset pulse clearing `WRAPPER_CPU_AXI_HALT` + 7 clocks @ 200 MHz (`sq2_venus_platform_system_restore_tz`) → explicit `WRAPPER_CPU_AXI_HALT_HALT` clear → `qcom_pas_set_remote_state(1, 0)` → `venus_run()` → IRQ unmask. Hardware 4K H.264/HEVC decode resumes immediately with zero firmware reload.

3. **Touchscreen & Slim Pen 2 (`spi-hid`) — [`patches/0014-hid-spi-hid-add-s2idle-system-pm-ops.patch`](../patches/0014-hid-spi-hid-add-s2idle-system-pm-ops.patch)**:
   - `spi-hid-core.c` had no `dev_pm_ops`. On resume, the unmasked digitizer IRQ fired before `spi0` finished resuming, failing with `Failed to receive header: -108` (`-ESHUTDOWN`) and leaving `iptsd` attached to a dead digitizer.
   - Adds `SIMPLE_DEV_PM_OPS(spi_hid_pm_ops, spi_hid_suspend, spi_hid_resume)`: masks IRQ, cancels pending work, clears `ready`, and powers down `vreg_ts_5p0` into `sleep` pinctrl on suspend; asserts reset, powers up rails, holds reset for 100 ms, clears stale pending IRQ state via `irq_set_irqchip_state(..., IRQCHIP_STATE_PENDING, false)`, unmasks IRQ, and deasserts reset on resume.

## Userspace & `systemd` Integration

- **Enable `s2idle`**: [`configs/systemd/surface-s2idle.conf`](../configs/systemd/surface-s2idle.conf) (`AllowSuspend=yes`, `SuspendState=mem`, `MemorySleepMode=s2idle`, hibernation disabled) replaces the legacy `surface-no-deep-suspend.conf`.
- **Lid & Power Button handling**: [`configs/systemd/surface-lid.conf`](../configs/systemd/surface-lid.conf) enables suspend on lid switch (ignored when docked) and suspend key; [`daemons/posture/surface-lid-daemon.sh`](../daemons/posture/surface-lid-daemon.sh) checks `/sys/class/drm/card*-DP-*/status` on SAM `closed` posture, blanking internal eDP only if docked or locking + invoking `systemctl suspend` otherwise.
- **SLPI FastRPC freeze guard** ([`systemd/system-sleep/surface-slpi.sh`](../systemd/system-sleep/surface-slpi.sh)): stops `hexagonrpcd-sdsp.service` on `pre/*` so Qualcomm SLPI `sensor_process` never faults (`EX:sensor_process:0x1:frpc_dsp`) while userspace is frozen, and restarts it with `--no-block` + `bl_power=0` on `post/*`.
- **IPTS touch restart** ([`systemd/system-sleep/surface-touch.sh`](../systemd/system-sleep/surface-touch.sh) & [`daemons/iptsd/start-surface-iptsd.sh`](../daemons/iptsd/start-surface-iptsd.sh)): stops `surface-iptsd.service` before `spi_hid_suspend`, waits for `/sys/bus/spi/devices/spi0.0/ready == ready`, and starts `iptsd` asynchronously 1 s after resume via `systemd-run --no-block --collect`.
- **Avoiding `user.slice` freeze deadlocks** ([`systemd/system-sleep/surface-audio.sh`](../systemd/system-sleep/surface-audio.sh), [`systemd/system-sleep/surface-power.sh`](../systemd/system-sleep/surface-power.sh), [`systemd/system-sleep/surface-pen.sh`](../systemd/system-sleep/surface-pen.sh)): `systemd-sleep` thaws `user.slice` (`gnome-shell`, `pipewire`) only after all `/usr/lib/systemd/system-sleep/*` hooks return. Any hook synchronously invoking `wpctl` or D-Bus calls against a frozen user session deadlocks resume with a black screen. These hooks therefore schedule their post-resume sync (including `surface-pen-blelc-setup --rearm` for WCN3998 Accept-List + `UsagePage 0xFFF4` Host BT MAC sync) via `systemd-run --no-block --collect --on-active=2s`.
- **4G LTE Modem persistence** ([`systemd/ModemManager.service.d/10-after-surface-lte.conf`](../systemd/ModemManager.service.d/10-after-surface-lte.conf)): launches `ModemManager --test-quick-suspend-resume` so `qcom-soc` (`qrtr0` / `rmnet_ipa0`) stays registered across `s2idle` without re-probing.

## Cold-Reboot & Post-`s2idle` Hardware Verification (`7.3.1-1-surface`)

Verified after a clean cold boot followed by closing and reopening the Type Cover (`success=1`, `fail=0`):

- **Timing**: device suspend `0.904 s`, device resume `1.236 s`, NVMe PCIe Gen.3 x2 link restored cleanly (`8/0/0 queues`), zero SLPI crashes.
- **Verified wake sources**:
  - **Type Cover open (Hall sensor)**: `IRQ 161` (`msmgpio 121 Edge Lid`, `gpio-keys-lid`) — wakes immediately when opening the cover.
  - **Power Button**: `IRQ 231` (`pm8941_pwrkey`) — verified across multi-minute `s2idle` cycles.
- **All 12 subsystems verified working post-resume**:
  - Internal eDP display + Adreno 685 GPU (GNOME desktop thaws immediately)
  - Touchscreen & Slim Pen 2 (`IPTSD Virtual Touchscreen` / `Stylus` recreated ~530 ms after resume, live touch verified)
  - Type Cover keyboard, touchpad & Hall-sensor lid wake
  - Wi-Fi (`WCN3998`, `wlan0 UP`) & Bluetooth (`Privacy = device` + WCN3998 hardware `ll-privacy`/Accept-List persists across `s2idle`; Slim Pen 2 auto-connects natively post-resume without scanning)
  - 4G LTE (`Snapdragon X24` + `IPA v4.1`: `ModemManager` remains `registered` on `lte`, `PS: attached`)
  - Stereo speakers (`WCD934x` + `WSA8815`) & microphones
  - All 3 cameras (`OV5693`, `OV13858`, `OV7251`) & Howdy IR Face ID
  - SLPI IMU/Compass (`monitor-sensor`) & ambient light sensor (`spx_tcs3430`)
  - USB-C orientation / DisplayPort Alt Mode (`port0` / `port1`)
  - Hardware TPM 2.0 (`/dev/tpm0`, `/dev/tpmrm0`)
  - Qualcomm Venus 4K EL1 TZ (`SQ2_TZ_PM set_remote_state(0, 0) ret=0`, `resume axi_halt=0x0 arg0=0x0->0x40000001 cpu=0x1 remote=0 run=0`, live GStreamer H.264 & HEVC hardware decode verified post-wake)

## Known Limitations

- **SoC `CXSD` / `AOSD` power collapse (`/sys/kernel/debug/qcom_stats/`)**: during `s2idle`, the Hexagon DSPs (`modem`, `cdsp`, `slpi_island`) enter low-power sleep cleanly, while SoC-wide `cxsd`/`aosd`/`ddr` sleep counters remain `0` pending upstream SC8180X PCIe/NVMe D3cold support (tracked in `linux-surface/surface-pro-x#29`).
- **Warm reboot after `s2idle`**: performing an immediate warm `reboot` right after `s2idle` can occasionally hit an early `systemd-udevd` coldplug watchdog reset on `remoteproc@2400000` (SLPI / AOSS). A full cold power cycle boots cleanly.
