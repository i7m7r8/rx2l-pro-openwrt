# 09-integration — landing order, conflicts, CI

## Verdict
Start the OpenWrt port on **base A**: upstream OpenWrt (`openwrt/openwrt@82e2e4f4…`) + pinned `openwrt-rx23302` overlay, kernel **6.18.41**, `target/linux/rtl819x`/`rtl8197f`, MIPS LE. Keep the 6.6/DSA DTS only as reference. No local build and no flash; first proof target is a CI-built `*-initramfs-kernel.bin` RAM boot.

## Firmware read (RX2L/TX2L treated as same router)
- Decrypted plaintext images: RX2L Pro `V16.03.51.17` (`d1ee92c0…e409bc70`) and TX2L Pro `V16.03.51.14` (`9643f415…0b4edd3d`). Both: 64 B Tenda header (`27 05 19 56`, fam `2`, product id `00031053`, CRC32 payload ok) + `cr6c` container (`start 0x80d00000`, `burn 0x40000`, loader BE-u16 sum 0, rootfs at `16+len`, squashfs mangled `hsqs`→`nice`).
- Same hardware class: RTL8197F, 128 MiB RAM, 16 MiB NOR, `KernelFS` is mtd3 @ `0x40000` size `0xF80000`; rootfs inside KernelFS; CFM/CFM_BACKUP/MISC2 at end. RX2L dumped flash/rootfs is the source of truth; TX2L image is a second build reference.
- Stock kernels are both Linux 4.4.176 Realtek MSDK; boot console `ttyS0`; bootloader has `disaster_web` recovery but no RSA/TDE01 gate. Stock rootfs confirms: `fireversion=AX2_PRO_V2_V16.03.51.18(6989)_cn`, `fw.product.id=0x00031053`, GPIO LEDs 54/57, reset/WPS 33, eth ports `4 3 2 1`, CPU port 6 mask `0x40`, wl2_4/wl5_0 mapping, RFE50 RF tables.
- Install correction: stock webui Local Upgrade is **not** a viable path for a hand-built image. `decry_firm` is run by all four upgrade paths and creates `/tmp/UPGRADE_DECRY_FAIL_FLAG` on plaintext/non-TDE01, then `check_fw()` aborts. Use RAM boot first; `tendaupgrade`/direct MTD only after KM/rollback/dual-bank questions are closed.

## Landing order
1. `01-base-build`: `build.sh.new`, `feeds.conf.new`, `seed.config.new`, CI/Dockerfile, `config-6.18.rx2l.delta`. Remove/replace stale `files/target/linux/realtek` (4.14 path) or build.sh stale-target guard will stop.
2. `02-image-container`: wire image fragment, `rx2lpro-image-gates.py`, `test_container.new.py`, `mtdsplit_cvimg.port`, host tools `cvimg`/`tenda-wrap`, loader notes. Gates: kernel <16 MiB, budget `0xF80000`, `cr6c/cs6c`, BE-u16 sum 0, rootfs at exact `16+len`, Tenda header product id/CRC/total.
3. `03-dts-switch`: land `rtl8197f_tenda_rx2lpro.dts` for base A (swconfig/RTL8367). Resolve MAC/nvmem strategy and GPIO numbering against the RTL8197F gpiochip before copying lines.
4. `07-usersland`: base-files, `02_network`, uci-defaults, sysupgrade contract, seed. Treat `02_network` band attribution and VLAN ids (SoC 9/8) as high risk.
5. `05-wifi-2g`: adopt 6.18 RTL8192CD path; calibrate from CFG text via userspace converter; fix FACTS 64 KiB read; do not use old 4.14 in-tree HAL.
6. `04-eth-hwnat`: finish draft; choose PHYLINK + flow-offload path on base A, not DSA. Confirm switch variant and init-regs on hardware.
7. `06-wifi-5g-8832br` + `11-pcie-5ghz`: deferred. First prove PCIe enumerates `10ec:b832` on RAM boot; no 5 GHz in v1 until PCI/IRQ/timer works.
8. `08-veto-paths` + `10-kernel-boot`: fill stubs with install-path verdict and stage-1/boot checklist. RAM boot sequence: board name → mtdsplit → `/proc/mtd` → eth link → WMAC load → wireless UCI.

## CI must fail if
- stale `target/linux/realtek` still present; overlay clobber report unreviewed; feeds parse fails; defconfig drops target/LuCI/uhttpd/firewall4/wifi-scripts; no `*factory-cr6c.bin`/`*tenda-plaintext.bin` produced; image gates fail; kernel+DTB overruns loader budget; rootfs magic not `hsqs` at `16+len`; Tenda header product id ≠ `00031053` or CRC mismatch.

## Next step
Orchestrator wires 01→02→03→07 as one CI branch, then RAM-boots `*-initramfs-kernel.bin` over serial. Do not flash until board name, mtdsplit offset, eth0 link, and 2.4 GHz WMAC are proven on the console.

## Deep-read addendum (binary-level evidence)
- **Bootloader (`mtd1`)**: gzip stream at `0x8d60` decompresses to 229,584 B U-Boot. Strings include `<RealTek>`, `boot.img`, `Entry disaster mode`, `No sys signature at %X!`, `check failed, ret:%d, sum:%x`, `disaster_web=1`, `ipaddr=192.168.1.1`, `serverip=192.168.1.99`. This confirms stage-1 checks only `cr6c/cs6c` + BE-u16 sum; no RSA/TDE01 boot gate.
- **Kernels**: RX2L/TX2L/mtd3 decompressed LZMA at payload `+0x2808`, all 13,384,292 B; cmdline string is `console=ttyS0,115200 root=/dev/mtdblock4`; partition string is `rtkxxpart ALL Bootloader CFG KernelFS RootFS CFM CFM_BACKUP MISC2`; symbols/strings show `rtk_wifi6`, `rtl8192cd`, `rtl8367`, `phyBandSelect`.
- **Crypto/upgrade gate**: ELF32 MIPS LE. `decry_firm` is dynamic uClibc, only `libc.so.0`, `main@0x406728` (284 B). Capstone MIPS disassembly matches note: if `argv[2]` exists and `access(argv[2])==0` → `unlink(argv[2])`; decrypt in place; if fail → `puts("openssl decode failed")`, `creat(argv[2], 0x1ff)`, `close`, return -1.
- **Call graph**: `libgo.so` has `upgrade@0x441ec`, `systool_upgradefile_handle@0x43cb0`, `tpi_upload@0x42e38`, `check_fw@0x42758`; imports `check_fw_type@0xa4320`, `check_fw_valid@0xa4f70`, `check_fw_crc@0xa43f0`, `check_mtd_size@0xa4770`. `libcommon.so` exports `check_fw_type@0xa824`, `check_fw_valid@0xc42c`, `check_fw_crc@0xb618`, `check_mtd_size@0xbd60`, `flash_write_mtd@0xca38`, `upgrade_boot@0xd544`. `libcommonprod.so` exports `check_fw@0x2a518` and imports the four sub-checks. All four daemons (`libgo`, `business_proc`, `netctrl`, `cwmpd`) contain and use `UPGRADE_DECRY_FAIL_FLAG`; `tendaupgrade` and `httpd` do not. `tendaupgrade` strings: `/proc/mtd`, `KernelFS1/2`, `KernelFS`, `env set image_boot 0/1 unlock`, `/dev/mtd%d`, `echo %d > /proc/km/km_upgrade_ignore_kernel_sig`.
- **CFG/CFM**: `mtd2_CFG` is NUL-separated text KVs; key offsets: `TENDA.lan_mac@0x22d`, `fw.product.id=0x00031053@0x58c`, `TENDA.wlan0_mac@0x5a5`, `TENDA.wlan1_mac@0x5c7`, country `ID`, EasyMesh/PSK/serial/PIN redacted. `mtd5_CFM`/`mtd6_CFM_BACKUP` are near-identical text config (`sys.hw_nat=1`, beamforming, BSS keys redacted). `mtd7_MISC2` begins with JSON table MAC/endpoints (redacted).
- **RF/wlan**: stock `rcS` runs `iwpriv wlan0 flash_set para_path=/etc/wlan/` and reads 64 KiB of CFG via `dd count=8192 ibs=8 skip=1` → `/etc/wlan_rfhwdata` → `rfhw_convert` → `iwpriv set_mib`. RFE50 has 40 files: parent tables plus AP/PON 2G/5G stubs (33–38 B); parent `PHY_REG/RadioA/RadioB/TXPWR_*` are the real tables. This drives tracks 05/06 calibration and 5G deferred status.
- **Web UI**: local upgrade flow uses `agentLocalUpgrade`/`deviceVersionList` with actions `queryversion`/`queryupgrade`; browser checks only `.bin` + size ≤ `CONFIG_LOCAL_UPGRADE_MAX_SIZE||35` MB; error map 1=Incorrect format, 2=CRC check failed, 3=Incorrect file size, 4=Failed to upgrade, 5=Insufficient memory. No extra client-side gate.

## Webui verdict + GitHub-only build plan
- **Stock webui Local Upgrade cannot accept a self-built OpenWrt image.** Every entry point runs `decry_firm image /tmp/UPGRADE_DECRY_FAIL_FLAG`; on plaintext/non-TDE01 it creates the flag (QEMU-proven), then `check_fw()` aborts before flashing. The required webui artifact is a signed **TDE01** (RSA-4096/SHA-512 over AES payload), which cannot be produced without Tenda’s private key.
- CI-only artifacts: `*-initramfs-kernel.bin` for RAM boot (safe proof), `*-factory-cr6c.bin` for loader AUTOBURN/TFTP or direct mtd3 write, `*-tenda-plaintext.bin` for root `tendaupgrade` staged file, `*-sysupgrade.bin` for later OpenWrt sysupgrade. A fresh squashfs gives fresh userdata/uci-defaults; never erase CFG/CFM (MAC/RF).
- Preflash gate: image gates + stock preflight + RAM-boot checklist green; only then persist to mtd3/`firmware`. Single-bank/no-rollback assumed until `image_boot`/KM are proven writable.
