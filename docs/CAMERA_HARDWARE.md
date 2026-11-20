# Surface Pro X SQ2 (SC8180X) Camera Hardware Notes

All routing, GPIOs, SMMU Stream IDs, SPMI regulators, and sensor registers below were verified on live Microsoft Surface Pro X SQ2 hardware (`arch/arm64`, Linux `7.3.1-1-surface`, `libcamera`, PipeWire/WirePlumber, Howdy).

## 1. Physical Topology & Media Graph

| Camera | Sensor | CCI Bus / Addr | MCLK | Reset / Enable GPIO | Physical CSIPHY | Lanes | CSID / VFE Path | Capture Node | Native Format |
|---|---|---|---|---|---|---|---|---|---|
| **Front RGB (5 MP)** | `ovti,ov5693` | `cci0` bus 1 (`4-0036`) | `MCLK2` (GPIO 15) | TLMM GPIO 12 (active-low) | `csiphy0` (`0xac65000`) | 2 | `csid0` → `vfe0_rdi0` | `/dev/video0` (`/dev/video-front`) | `2592×1944` `SBGGR10P` (`rotation = <180>`) |
| **Rear RGB (13 MP)** | `ovti,ov13858` | `cci0` bus 0 (`3-0010`) | `MCLK0` (GPIO 13) | TLMM GPIO 22 (active-high) | `csiphy2` (`0xac67000`) | 4 | `csid1` → `vfe1_rdi0` | `/dev/video4` (`/dev/video-rear`) | `4224×3136` `SGRBG10P` (`rotation = <0>`) |
| **Front IR (VGA)** | `ovti,ov7251` | `cci0` bus 0 (`3-0060`) | `MCLK3` (GPIO 16) | TLMM GPIO 23 (active-high) | `csiphy3` (`0xac68000`) | 1 | `csid_lite1` (`csid3`) → `vfe_lite1_rdi0` (`vfe3`) | `/dev/video12` (`/dev/video-ir`) | `640×480` `Y10` (CSID3 unpacked `Y10_2X8_PADHI_LE`, 90° portrait mount) |

`csiphy1` (`0xac66000`) is unused on SQ2 (`status = "disabled"`).

## 2. Power Rails (PMC8180-A SPMI)

Camera power rails live on `PMC8180-A` (SPMI SID 1, `/soc@0/spmi@c440000/pmic@1`) and are controlled by [`drivers/surface_cam_spmi/surface_cam_spmi.c`](../drivers/surface_cam_spmi/surface_cam_spmi.c):

| Rail | SPMI Base | Voltage | Function |
|---|---|---|---|
| `LDO14_A` | `0x4d00` | `1.800 V` | Camera I/O + CCI I2C pull-ups |
| `LDO17_A` | `0x5000` | `2.856 V` | Sensor analog (`AVDD`) |
| `LDO1_A` | `0x4000` | `1.200 V` | Sensor digital core (`DVDD`) |

## 3. Privacy LEDs & 850 nm IR Strobe

- **Front white privacy LED**: TLMM `GPIO 25` (`GPIO_ACTIVE_HIGH`), shared and reference-counted in `qcom-camss` across `csiphy0` (front RGB) and `csiphy3` (front IR).
- **Rear white privacy LED**: TLMM `GPIO 11` (`GPIO_ACTIVE_HIGH`), driven by `csiphy2` (rear RGB).
- **Front 850 nm IR illuminator**: Driven directly by the `OV7251` hardware `STROBE` pin in [`patches/0002-media-i2c-ov7251-continuous-clk-and-ir-strobe.patch`](../patches/0002-media-i2c-ov7251-continuous-clk-and-ir-strobe.patch):
  - `0x3b81 = 0xff` (every-frame strobe pattern mask)
  - `0x3b8e..0x3b8f = 0x0200` (`strobe_frame_span`)
  - `0x3027 = 0x00`, `0x3005 = 0x08` on `s_stream(1)`
  - `0x3005 = 0x00` on `s_stream(0)`

## 4. SC8180X / Titan 175 CAMSS Findings

Key root causes fixed in [`patches/0004-media-qcom-camss-sc8180x-titan175-full.patch`](../patches/0004-media-qcom-camss-sc8180x-titan175-full.patch):

1. **SMMU Stream IDs**: Live DMA requires explicit Stream IDs `0xa00..0xae0` (step `0x20`) and `0xe00..0xee0` (step `0x20`) with mask `0x0` on `&apps_smmu`.
2. **VFE Lite (`vfe_lite0`/`vfe_lite1`) register map**: VFE Lite has only 4 bus clients (`WM_ADDR_NO_SYNC_LITE_VAL = 0xf`) and does **not** implement `VFE_BUS_WM_DEBUG_STATUS_CFG` (`0x226c`); writing `0x226c` on Lite faults.
3. **2-deep WM `IMAGE_ADDR` FIFO**: `VFE_BUS_WM_IMAGE_ADDR` is a 2-deep hardware FIFO. Pre-loading a dummy IOVA before `buf[0]` shifts every returned frame by 1, and subscribing to both `COMP_GRP_0` and `STATUS1_WM_CLIENT_BUF_DONE` fires `vfe_buf_done()` twice per frame. Subscribing solely to `STATUS1_WM_CLIENT_BUF_DONE(wm)`, loading real `buf[0]`/`buf[1]` first, and falling back to `VFE_DUMMY_BUF` only on genuine underrun gives clean 20/20 frame delivery on all three sensors.
4. **Local SOF latching & NoC safety**: Keep `VFE_CORE_CFG (0x050) = 0` (`BIT(4)` enables `extern_reg_update` slave mode and stalls RDI updates). On SC8180X, cutting `camnoc_axi` / VFE genpd after streaming causes an immediate SoC NoC bus deadlock; keeping clocks/genpd active while `qcom-camss` is loaded and issuing `vfe_reset()` on warm stream reuse prevents hangs across repeated captures.
5. **RDI `Y10` `VIDIOC_S_FMT`**: Populating `pix_mp` from `video_get_subdev_format()` in `__video_try_fmt()` for non-line-based RDI nodes allows OpenCV/Howdy (`CAP_V4L2`) to negotiate `640×480 Y10` without failing format validation.

## 5. `libcamera` `DebayerEGL` CCM Column-Major Bug

On Adreno 685 (`FD680` / Freedreno), `libcamera`'s GPU `DebayerEGL` shader uploads the 3×3 CCM from IPA YAML directly into an OpenGL ES `mat3` (column-major) and computes `ccm * rgb`, effectively transposing row-major CCMs. Asymmetric row-normalized CCMs then sum columns ≠ 1.0 and clip bright highlights to magenta.

Workaround in [`libcamera/ipa/simple/ov5693.yaml`](../libcamera/ipa/simple/ov5693.yaml) and [`ov13858.yaml`](../libcamera/ipa/simple/ov13858.yaml): use **symmetric** matrices where every row and column sums to `1.00`, paired with `blackLevel: 1536` (16-bit scale) to eliminate washed-out shadows while preserving natural color on GPU debayering.
