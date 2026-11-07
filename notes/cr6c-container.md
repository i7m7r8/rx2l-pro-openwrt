# RX2L Pro KernelFS container = standard OpenWrt `cvimg` image (cr6c)

Verified 2026-11-07 against three independent samples: the device's own
`mtd3_KernelFS.bin`, and two decrypted vendor upgrade images (RX2L 51.17,
TX2L 51.14). The checksum reproduces **byte-exactly** on all three, so the
container is not a vendor invention — it is Realtek's `cvimg` "firmware"
image, and OpenWrt's `tools/firmware-utils/src/cvimg.c` writes it.

## Layout (offsets relative to the start of the cr6c container)

| off | len | field | value (all three samples) |
|-----|-----|-------|---------------------------|
| 0x00 | 4 | signature | `cr6c` — DATA_FIRMWARE / CPU_NEW (rtl8197f family) |
| 0x04 | 4 | start_addr | `0x80d00000` big-endian |
| 0x08 | 4 | burn_addr | `0x00040000` big-endian = mtd3 flash offset |
| 0x0c | 4 | len | BE, = payload_len + 2 → mtd3/tx2l `0x3c0802`, rx2l `0x3c0c02` |
| 0x10 | len−16−2 | payload | vendor boot stub + kernel blob |
| 16+payload_len | 2 | checksum | cvimg `calculate_checksum`, `htons(~sum16+1)` |
| +0 | — | squashfs rootfs | superblock magic mangled `hsqs`→`nice` |

`payload_len` = `len - 2` = 0x3c0800 (mtd3, tx2l) / 0x3c0c00 (rx2l).
Checksum over `payload[0 .. payload_len)` read from offset 0x10, stored
**big-endian** immediately after the payload. Nothing else follows before the
rootfs superblock — the two checksum bytes sit at 0x3c0810/0x3c0c10 and
`nice` starts at the very next byte.

Cross-check with the flash map: mtd4 RootFS begins at absolute 0x400812 =
mtd3 base (0x40000) + 0x3c0812 = exactly the squashfs superblock. So
mtd4 == the rootfs region of this container, and mtd3's slack past it
(mtd3 is 0xF80000, image uses 0x3c0c12 + rootfs) is the JFFS2/overlay area.

## What this means for the port

1. **The writer (task #13) already exists** — it is `cvimg` in the OpenWrt
   realtek target. Our device profile needs only:
   `-s cr6c -e <loader link addr> -b 0x40000` on
   `kernel-bin | append-dtb | lzma | loader-cmdline-compile` output, then the
   squashfs appended after it. The ADCDS DIR-842 R1 port does exactly this for
   the `cr6b` variant (`Build/cvimg-kernel-cr6b` in
   `files/target/linux/realtek/image/Makefile`).
2. **The vendor `nice` squashfs magic is a stock-kernel patch, not a container
   requirement.** Our port ships its own kernel, so plain `hsqs` works and the
   patch does not need to be replicated.
3. **`cr6c` implies no signature gate on the boot path.** cvimg's only
   integrity field is this 16-bit additive checksum, which any builder can
   compute. The RSA-4096/SHA-512 gate lives in userspace `decry_firm` and
   guards the *upload* channel only — it is not consulted by the bootloader.
4. Do **not** use cvimg's `-T` ("Tenda bootloader") checksum mode: that writes
   `len` 16 bytes shorter and puts the checksum at a different offset. The
   stock RX2L Pro images use the standard format, verified above.
