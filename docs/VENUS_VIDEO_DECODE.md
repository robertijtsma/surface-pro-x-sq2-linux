# Qualcomm Venus (Iris1 / HFI4) Hardware Video Decoding on Surface Pro X SQ2

This document describes the verified Linux 7.3 hardware video decoding stack for the **Microsoft Surface Pro X SQ2 (Qualcomm SC8180X)** using the Iris1 VPU (`0xaa00000`, hardware version `0x5010002f`) under EL1 TrustZone SCM PAS 9.

## Architecture Overview

1. **Clock & GDSC Provider (`videocc-sm8150`)**:
   - The SC8180X video clock controller (`clock-controller@ab00000`, MMCX domain `<&rpmhpd SC8180X_MMCX>`) registers `venus_gdsc`, `vcodec0_gdsc`, `vcodec1_gdsc`, `VIDEO_CC_PLL0`, and the `video_cc_{mvsc,mvs0,mvs1}_core_clk` + `video_cc_iris_ahb_clk` branches (`patches/0009`).
   - Like `camcc@ad00000`, `clock-controller@ab00000` sits in `MMCX`. Probing it during early `systemd-udevd` coldplug while display (`mdss_dp0`/`mdss_dp1`/`eDP`), SPMI, and RPMh are still settling can trigger an MMCX/MM0 hardware watchdog reset. `configs/modprobe.d/venus-sq2-guard.conf` blacklists `videocc_sm8150` and `venus_*` from early coldplug so they are brought up cleanly after `surface-camera-setup.service` via `surface-venus-setup.service`.

2. **Runtime Device Tree Overlay (`sq2_venus_overlay`)**:
   - Static boot DT exports only a `phandle = <0xf5>` on `clock-controller@ab00000` (`dts/sc8180x-surface-pro-x-sq2-venus-videocc.dtsi`), keeping early boot identical to the base DTB and leaving all fallback DTBs untouched.
   - After `videocc_sm8150` binds, `sq2_venus_overlay` (`drivers/sq2_venus_overlay/`, loaded with `allow=1`) validates all live phandles (`gcc`, `apps_smmu`, `videocc`, `config_noc`, `gem_noc`, `mmss_noc`, `mc_virt`, `interrupt-parent`) and attaches `/soc@0/venus-sq2@aa00000` (`compatible = "microsoft,sq2-venus-lab"`, SMMU SIDs `0x2300`, `0x2320`, `0x2340`, SPI 174, child `video-decoder`).

3. **EL1 TrustZone PAS 9 Firmware Loading (`venus-core` / `venus-dec`)**:
   - Ported from [`lhf1a/googlebookos_surface_pro_x`](https://github.com/lhf1a/googlebookos_surface_pro_x) to Linux 7.3 (`patches/0010-media-qcom-venus-sq2-iris1-hfi4-decoder.patch`).
   - Queries the read-only PIL memory bounds via `TZ_PIL_GET_MEM_AREA` (`0x8b700000..0x9ab00000`), claims a 6 MiB unused window at `0x8c000000..0x8c600000` inside the UEFI `reserved` region (outside all `/reserved-memory` subnodes), loads `/lib/firmware/qcom/venus-5.2/venus.mbn` via `qcom_mdt_load(..., 9, ...)`, authenticates and starts the Iris1 ARM9 via `qcom_scm_pas_auth_and_reset(9)`, and programs the video protection window via `qcom_scm_mem_protect_video_var(0, 0x25800000, 0x1000000, 0x24800000)`.
   - Accepts both known Surface Pro X SQ2 firmware releases:
     - `VIDEO.IR.1.2-00042-PROD-1` (`1,159,200` bytes)
     - `VIDEO.IR.1.2-00079-PROD-2` (`1,142,816` bytes, 5 MiB relocatable ELF span)
   - **Linux 7.3 `vdec_close()` teardown ordering**: Upstream Linux 7.3 folded `v4l2_m2m_ctx_release()`, `v4l2_m2m_release()`, and `hfi_session_destroy()` into `venus_close_common()`. On SQ2, the fallback `hfi_session_deinit()` close guard must run **after** `v4l2_m2m_ctx_release()` (which triggers `vdec_session_release()` → `STOP` + `UNLOAD_RES` + `SESSION_END`) and **before** `hfi_session_destroy()`. Running `hfi_session_deinit()` before `v4l2_m2m_ctx_release()` causes the firmware to reject `SESSION_END` on a still-loaded session and latch `core->sys_error`.

## Capabilities Reported by Hardware Firmware

Exposes `/dev/video16` (symlinked at `/dev/video-venus-dec`, V4L2 M2M multiplanar, `NV12` & `Q08C` UBWC, up to 16 concurrent sessions):

| Codec | V4L2 FourCC | Max Resolution | Max Frame Rate | Max Bitrate |
|---|---|---|---|---|
| **H.264 (AVC)** | `H264` | 8192×8192 | 480 fps | 220 Mbps |
| **HEVC (H.265)** | `HEVC` | 8192×8192 (8-bit NV12 / 10-bit P010) | 480 fps | 220 Mbps |
| **VP9** | `VP90` | 8192×8192 (8-bit NV12 / 10-bit P010) | 480 fps | 220 Mbps |
| **VP8** | `VP80` | 4096×4096 | 240 fps | 100 Mbps |
| **MPEG-2** | `MPG2` | 1920×1920 | 30 fps | 40 Mbps |

## Cold-Reboot Hardware Verification (`7.3.1-1-surface`)

Verified on live SQ2 hardware across 11 consecutive decode sessions on cold boot (`sq2_pm_status`: `error=0 sys_error=0 hfi_error=0 insts=0 vb2_bytes=0`, zero SMMU faults):

- **Unit CRC tests**: Back-to-back H.264 → HEVC → H.264 (`6/6` frames bit-exact CRC match on all 3 runs, clean session close and reuse).
- **GStreamer (`v4l2h264dec` / `v4l2h265dec` / `v4l2vp9dec`) vs FFmpeg software reference (SHA-256 of full raw YUV420P output)**:
  - **4K H.264 (`3840×2160`, 60 frames, 746,496,000 bytes)**: bit-for-bit identical SHA-256 (`42297124180ca83e8e44f292563a7aef1998ccc7e6868be9fe98c9faf0c94891`)
  - **4K HEVC (`3840×2160`, 60 frames, 746,496,000 bytes)**: bit-for-bit identical SHA-256 (`1143d4d7bb4a5e20b9ecab890206bc1be174ebf2cd0934df102c7ef1476c8592`)
  - **1080p VP9 (`1920×1080`, 60 frames, 186,624,000 bytes)**: bit-for-bit identical SHA-256 (`5c7b8897af1f083f391f18c3d50514d8d5d5bff171bd8094cb0ea4943ebebba1`)
- **FFmpeg V4L2 M2M throughput**: 4K H.264 (`h264_v4l2m2m`) ~52 fps, 4K HEVC (`hevc_v4l2m2m`) ~60 fps, 1080p VP9 (`vp9_v4l2m2m`) ~175 fps.

## Limitations

- **EL1 PAS 9 one-shot lifecycle**: Qualcomm TrustZone PAS 9 permits a single Venus firmware start per boot; `venus-core` intentionally keeps the firmware resident until reboot and disables automatic firmware restart on fatal errors (`sq2_no_tz=1` EL2 suspend/resume requires booting Linux at EL2).
- **VA-API / Browsers**: Venus exposes standard Linux **V4L2 M2M** (used by GStreamer `v4l2*dec`, FFmpeg `*_v4l2m2m`, and `mpv` via [`configs/mpv/mpv.conf`](../configs/mpv/mpv.conf)), not VA-API; `LIBVA_DRIVER_NAME=none` remains set for desktop stability. Firefox/Zen's `gfxtest v4l2` probes `/dev/video16` cleanly (`V4L2_SUPPORTED=TRUE` for `H264`/`HEVC`/`VP9`/`VP8`/`MPG2`) and initializes `h264_v4l2m2m`, but falls back to software until FFmpeg's V4L2-M2M wrapper outputs `AV_PIX_FMT_DRM_PRIME` frames.
