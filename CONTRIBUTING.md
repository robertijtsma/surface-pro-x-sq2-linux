# Contributing to `surface-pro-x-sq2-linux`

Contributions, SQ1/SQ2 hardware test reports, kernel rebases, and driver improvements are welcome!

## Rules & Requirements

1. **Zero Proprietary Firmware Blobs or Binaries**:
   - Never commit `.mbn`, `.bin`, `.ko`, `.so`, `.dtb`, or Windows driver files.
   - All device-specific calibration or firmware files must be extracted locally by the end user or read dynamically at runtime (e.g. `sq2_fsg_read`).
2. **Zero Personal Data**:
   - Never include Bluetooth MAC addresses, Wi-Fi SSIDs, IMEIs, serial numbers, hostnames, or local usernames in configs, logs, or scripts.
3. **Verify on Real Hardware**:
   - Kernel patches (`patches/*.patch`) should apply cleanly with `git apply --stat` and follow standard Linux kernel coding style.
   - Before opening a PR, run the built-in validation suite:
     ```bash
     ./scripts/install-sq2-userspace.sh --check
     ./scripts/surface-sq2-diag.sh
     ```

## Testing & Reporting Bugs

When opening an issue or pull request, please include:
- Your hardware variant (**Surface Pro X SQ1** or **SQ2**, RAM, LTE/Wi-Fi)
- Kernel version (`uname -r`)
- Output of `./scripts/surface-sq2-diag.sh`
- Relevant `dmesg` or `journalctl` excerpts (sanitized of MAC/IMEI/serials)

## License

All kernel patches and out-of-tree kernel modules must be `GPL-2.0-only` compatible to match the Linux kernel. `libcamera` IPA tuning YAML files are `CC0-1.0`.
