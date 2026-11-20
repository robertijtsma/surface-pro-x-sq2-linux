# Windows Hello IR Face ID (Howdy + OV7251) on Surface Pro X SQ2

The Surface Pro X SQ2 front IR sensor (`ovti,ov7251`, 640×480 `Y10` + automatic 850 nm hardware `STROBE` LED) works with Howdy (YuNet + SFace / OpenCV) across GDM, Polkit, `sudo`, and `su`.

## Hardware Notes

1. **90° portrait bezel mount**: The `OV7251` die is mounted rotated 90° on the Surface Pro X bezel. Setting `rotate = 2` (90° CCW) in `/etc/howdy/config.ini` produces upright 480×640 IR frames where YuNet consistently detects faces (~0.90 confidence, ~0.97 SFace self-consistency; `rotate = 0` or `1` detects 0 faces).
2. **CSID3 hardware Y10 unpacking**: `surface-camera-check` configures `msm_csid3:1` to `Y10_2X8_PADHI_LE` (`V4L2_PIX_FMT_Y10` on `/dev/video-ir` -> `/dev/video12`), so OpenCV `CAP_V4L2` reads unpacked 16-bit greyscale directly without a userspace unpacker.
3. **ARM64 OpenMP/OpenBLAS crash inside PAM**: Inside `sudo` / `gdm-password` / `polkit`, multi-threaded OpenBLAS/OpenMP initialization in OpenCV/libgomp frequently aborts or stalls. Wrapping `howdy-compare` and `howdy` with `OMP_NUM_THREADS=1`, `OPENCV_FOR_THREADS_NUM=1`, `OPENBLAS_NUM_THREADS=1`, and `OPENCV_VIDEOIO_PRIORITY_GSTREAMER=0` makes PAM recognition deterministic (~0.85–0.95 s per match).

## Installation

```bash
make -C faceid
sudo mv /usr/lib/howdy/howdy-compare /usr/lib/howdy/howdy-compare.bin
sudo install -m 755 faceid/howdy-compare-wrapper /usr/lib/howdy/howdy-compare
sudo mv /usr/bin/howdy /usr/bin/howdy.bin
sudo install -m 755 faceid/howdy-cli-wrapper /usr/bin/howdy
sudo cp faceid/config.ini.example /etc/howdy/config.ini
```

Enroll your face under normal seating posture:

```bash
sudo howdy add
sudo howdy test
```

## PAM Integration

Add the following line at the top of `/etc/pam.d/sudo`, `/etc/pam.d/su`, `/etc/pam.d/polkit-1`, and `/etc/pam.d/gdm-password`:

```pam
auth sufficient pam_howdy.so
```

Note: Make sure `surface-camera-setup.service` is enabled (`Before=display-manager.service gdm.service`) so `/dev/video-ir` and the media-controller pipeline are initialized before GDM starts.
