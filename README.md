# OpenWrt for the Tenda RX2L Pro

Port of OpenWrt to the **Tenda RX2L Pro** (`devModel RX2LProV1.0`, internal
`AX2_PRO_V2`), a Realtek **RTL8197F** router with 16 MiB SPI NOR and 128 MiB
RAM, shipping vendor firmware `V16.03.51.18(6989)_cn`.

The device support here is derived from the
[ADCMS/OpenWrt DIR-842 R1 port](https://github.com/ADCDS/openwrt-dlink-dir842-r1)
(same SoC, hardware-proven) and adapted to this board's own flash map,
console speed, RAM size and MAC layout. See `PROVENANCE.md`.

---

## Board facts, and how each was obtained

| item | value | source |
|---|---|---|
| SoC | RTL8197F, MIPS 24Kc V8.5, single core | `/proc/cpuinfo` on the device |
| RAM | 128 MiB | `/proc/meminfo` (`MemTotal: 115744 kB` = 128 MiB minus driver reservations) |
| console | `ttyS0` at **115200** | `/proc/cmdline` |
| stock root | `/dev/mtdblock4` | `/proc/cmdline` |
| flash | `m25p80`, erasesize `0x10000`, 16 MiB | `/proc/mtd` + boot log |
| kernel | 4.4.176 (vendor) | boot log |

Partition table, verbatim from the device's `/proc/mtd`:

| mtd | start | size | end | label | ours |
|---|---|---|---|---|---|
| – | 0x000000 | 0x30000 | 0x030000 | Bootloader (mtd1) | read-only, never written |
| – | 0x030000 | 0x10000 | 0x040000 | CFG (mtd2) | read-only; holds the ASCII `TENDA.*_mac=` keys |
| **firmware** | 0x040000 | 0xF80000 | 0xFC0000 | KernelFS (mtd3) | **the partition we write** |
| – | (0x400812, inside mtd3) | 0xBBF7EE | 0xFC0000 | RootFS (mtd4) | squashfs window, not a sibling |
| – | 0xFC0000 | 0x10000 | 0xFD0000 | CFM (mtd5) | read-only, RF calibration + MACs |
| – | 0xFD0000 | 0x10000 | 0xFE0000 | CFM_BACKUP (mtd6) | read-only |
| – | 0xFE0000 | 0x10000 | 0xFF0000 | MISC2 (mtd7) | read-only |

`mtd0 "ALL"` spans 0x0..0xFF0000, so the final erase block belongs to no
partition. mtd4's start (0x400812) minus mtd3's base (0x40000) is 0x3c0812,
which is exactly where the cr6c container's squashfs superblock sits — the two
"overlapping" partitions are one region viewed twice.

Per-unit MACs are ASCII inside the `CFG` text config (from a live dump; the
real values are **elided** here because the console root password is derived
from the LAN MAC, so a published MAC is a half-secret):

```
0x22d  TENDA.lan_mac=80:9F:XX:XX:XX:B0
0x5a5  TENDA.wlan0_mac=80:9F:XX:XX:XX:C4   (2.4 GHz)
0x5c7  TENDA.wlan1_mac=80:9F:XX:XX:XX:B4   (5 GHz)
```

`80:9F:E4` is Tenda's OUI; the last three octets are per-unit. The offsets and
key names are the portable part — read the MACs from your own `CFG`.

There is **no** `wan_mac` line — the vendor derives it. `board.d/02_network`
uses WAN = LAN+1, which is our choice, not a vendor fact, and the comment
there explains why LAN and WAN must differ on this SoC.

---

## The container format (this is the part that matters)

The stock `KernelFS` is **not** a vendor invention. It is a standard Realtek
`cvimg` image with the `cr6c` signature — the same host tool OpenWrt's realtek
target already ships. Verified byte-exactly against three samples (the
device's own mtd3 and two decrypted vendor upgrades), including its 16-bit
checksum. Full field table in `notes/cr6c-container.md`.

```
0x00  "cr6c"          signature
0x04  0x80d00000      start_addr (BE)   <- vendor kernel; ours is our loader's link addr
0x08  0x00040000      burn_addr  (BE)   <- mtd3 offset, matches our DTS
0x0c  payload_len + 2 (BE)
0x10  payload (kernel blob)
      2-byte cvimg checksum             <- ~sum16(payload)+1, big-endian
      squashfs superblock, exactly at 16 + len
```

★ The vendor's squashfs superblock magic is mangled `hsqs` → `nice`. That is a
**stock-kernel patch**, not a container requirement: our kernel uses normal
squashfs and needs no such patch.

★ `mtdsplit_cvimg.c` as shipped upstream computed the rootfs offset as
`ALIGN(16 + len, 4)`. On this board the superblock sits at the *un-aligned*
`16 + len` (0x3c0812 is not divisible by 4), and the parser's 4 KiB-step
fallback scan can never see an offset like that. Whether our own images hit
the aligned candidate depends on kernel size modulo 4, i.e. the split would
work one build and silently fail the next. This repo patches the parser to try
the exact offset first, then the aligned one, then scan
(`files/target/linux/generic/files/drivers/mtd/mtdsplit/mtdsplit_cvimg.c`).

---

## Install paths — read this before touching a router

**The webui's "Local Upgrade" will not take our image.** An earlier revision of
this file said the opposite ("the signature verdict is discarded, so a plaintext
image flashes"); that reading stopped one function too early, and it is worth
showing why, because it decides which install path you are allowed to plan on.

`/bin/decry_firm` verifies an **RSASSA-PKCS1-v1_5 with SHA-512 signature over the
entire AES payload** of a `TDE01` container with a 4096-bit public key embedded in
the binary (modulus at `.rodata 0x41cd00`, `e = 65537` at `.data 0x42f940`), and no
self-built file can pass it. It is true that `upgrade()` (`libgo.so@0x441ec`)
never reads `decry_firm`'s return value — but it does not have to: `decry_firm`
is called as `decry_firm /var/image /tmp/UPGRADE_DECRY_FAIL_FLAG`, and it
*records its own verdict* in that second argument. It unlinks a stale flag,
decrypts in place, and on failure **creates the flag** (0 bytes, rc 255) while
leaving the image byte-identical.

`check_fw()`, the pre-flash gate in `lib/libcommonprod.so` at `0x2a518`, tests
that flag **before it does anything else**, before type, size or CRC:

```
0002a550  move   $a1, $zero          ; access(path, F_OK)
0002a558  addiu  $a0, $v0, 0x61a8    ; ['/tmp/UPGRADE_DECRY_FAIL_FLAG']
0002a55c  lw     $v0, -0x7e30($gp)   ; {access}
0002a564  jalr   $t9
0002a570  bnez   $v0, 0x2a5b0        ; flag ABSENT -> the real checks
0002a578  addiu  $v0, $zero, -1      ; flag PRESENT -> return -1
0002a590  ... printf('...-----image decrypted failed---\n')
```

That -1 travels back through `tpi_upload` (`libgo.so@0x42e38`) to
`systool_upgradefile_handle` and into `upgrade()`, which at `0x44708`
(`bnez $v0, 0x44778`) **skips the only flash trigger** in the binary
(`systool_call_writeImage_delay_11ac`), deletes `/var/image` and reboots. Nothing
is written to flash, and the browser gets
`/corss-domain-res.html?errCode=1`.

So: we cannot build a file that `decry_firm` accepts (the TDE01 key is not
recoverable), and the moment we cannot, `check_fw` refuses. The webui path is
closed, not because RSA cannot be forged, but because **the failure is
remembered in a file that the next gate checks first**. The full chain, with the
qemu execution proof for `decry_firm` and every call-site address, is
[`port/08-veto-paths/REPORT.md`](port/08-veto-paths/REPORT.md); the header/format
fields the vendor gate would have wanted are still documented in
[`notes/webui-upgrade-contract.md`](notes/webui-upgrade-contract.md) because they
define the container geometry.

`check_fw` → `check_fw_type` / `check_fw_valid` / `check_fw_crc` /
`check_mtd_size` (`check_fw` itself in `lib/libcommonprod.so`; `lib/libcommon.so`
carries only the `check_fw_*` helpers) contain **no** RSA or
`EVP_DigestVerify` call: they check header shape, sizes, `fw_type`,
`fw.product.id` and the payload CRC32. Those are exactly the fields
`tools/check-webui-bin.py` validates offline, and they are still worth
validating — not for the webui, but so a container that *would* have been
accepted can be told apart from one that is malformed before it reaches flash.

Install paths, in the order to try them. The step-by-step version, including the
pre-flight dumps and the recovery ladder, is
[`notes/install-runbook.md`](notes/install-runbook.md):

1. **RAM boot (no flash write, always safe).** `*-initramfs-kernel.bin`, loaded
   through the Realtek loader shell (`<RealTek>` prompt: `LOADADDR`, `XMOD`,
   `IPCONFIG`) — see *First-boot risk checklist* below. Serial cable required.
   Nothing is persisted; a power cycle returns you to stock.
2. **Vendor `tendaupgrade` from a root shell.** One `KernelFS` write done by the
   vendor's own flasher: `tendaupgrade <fwtype> <1|0> /var/image`. This is the
   first path that actually installs, and it is not affected by anything above:
   `tendaupgrade` contains neither the `decry_firm` string nor a `check_fw`
   import (verified on the extracted binary), so it never decrypts and never
   consults the flag. Fresh squashfs = pristine default config, which is the
   factory-reset behaviour you want; the `CFG` partition is never touched
   because our port reads the MAC and RF calibration out of it.
3. **Loader `AUTOBURN`** with `*-factory-cr6c.bin` over TFTP — the recovery-grade
   path, and the one that works when nothing on flash boots at all.
4. **Direct MTD write** to `mtd3` from a root shell — the explicit fallback when
   the vendor flasher itself is what is misbehaving.

⚠ The webui will reject the plaintext artifact with `errCode=1` and will not
touch flash; that is expected behaviour, not a build failure. What still has to
be true before path 2–4 runs on hardware is narrower than it used to be: the
four `check_fw_*` comparisons are read and encoded in `tools/check-webui-bin.py`
(done), so an image can be proven *well-formed* offline, and the only open
question left is whether a daemon that reads `UPGRADE_DECRY_FAIL_FLAG` acts on a
stale one after a reboot (`business_proc`, `netctrl`, `cwmpd` all name the file;
none of them is on the `tendaupgrade` path, and `/tmp` is tmpfs, so the flag does
not survive a power cycle). Path 1 is unaffected by all of it.

## First-boot risk checklist

Ordered by "what actually stops a boot", and what each item's failure looks like
on the serial console. Every one of these is a decision in the tree, not a
generic worry — the file named is where it lives.

| # | Risk | Mechanism / where | Symptom if wrong | Status |
|---|------|-----------------|------------------|--------|
| 1 | **Board name does not resolve** | `lib/realtek.sh` matches `/proc/cpuinfo` `machine` against a hardcoded list; an unmatched board gets `board_name=generic` and `etc/board.d/02_network` never configures anything | boots, but no LAN config at all (default `br-lan` with no ports) | **fixed** — `"RX2L Pro"` entry added |
| 2 | **Root device never assigned** | there is deliberately **no `root=`** in the cmdline: `CONFIG_MIPS_CMDLINE_FROM_DTB=y` + `chosen bootargs` in `dts/RX2LPRO.dts`, and the root device comes from `CONFIG_MTD_ROOTFS_ROOT_DEV=y` (base tree `generic/config-4.14`) firing on the partition `mtdsplit_cvimg` creates | `VFS: Unable to mount root fs on unknown-block(0,0)` — hard hang at the same place every boot | verified in this audit; parser offset bug found and **fixed** (`mtdsplit_cvimg.c`: `len` is used *exact*, `+2` for the checksum — the old `ALIGN(len,4)` landed 2 bytes past the superblock) |
| 3 | **Partition label** | `CONFIG_MTD_SPLIT_FIRMWARE_NAME="firmware"` — the cvimg parser only runs on a partition named exactly `firmware` | same as #2 | label is `firmware` in `dts/RX2LPRO.dts` ✓ |
| 4 | **squashfs magic** | the vendor kernel needs the mangled magic (port-facts §6: `hsqs`→`nice`, *and* a mangled XZ magic — theirs is a squashfs fork); our image ships **our own kernel**, which uses stock `hsqs`, and `mtdsplit.c` matches `SQUASHFS_MAGIC` | rootfs not found / not mounted | do **not** mangle ours; both ends are ours. CI gate: `tools/check-cr6c.py` |
| 5 | **Header `start_addr` vs where the loader is linked** | `CVIMG_LOADER_START_ADDR := 0x81000000` == `LZMA_TEXT_START` in `image/Makefile`. The reference port's boot log shows the Realtek SDK loader honoring the field (`Jump to image start=0x81000000`), but **Tenda's own stage-1 is a different binary** | silent: nothing on the console after `Booting...` | **open** — stage-1 disassembly in flight (mtd1's copy-destination and jump-target sources) |
| 6 | **Ethernet datapath** | `rtl819x-eth.c` hardcodes the SoC VLAN table for the *reference* topology (`sw_add_vlan_fid(RTL865X_VID_LAN, 0x10F, ...)` = jacks 0-3 + CPU8). This board's jacks hang off an external **RTL8367RB-class** switch with **no driver and no DT node**, and its CPU trunk port is unidentified | box boots and is reachable only over UART or the 2.4 GHz AP; WAN dead | **open** — mitigated: LAN is `eth0` untagged (not `eth0.2`), and the 2.4 GHz AP is seeded as the fallback path |
| 7 | **2.4 GHz driver actually builds** | `CONFIG_RTL8192CD=m` in `rtl8197f/config-4.14`; the vendor source in `files-4.14/drivers/net/wireless/rtl8192cd` carries a Kconfig help that still says *"WORK IN PROGRESS — not expected to build or function yet"* while `modules.mk` says *"proven on hardware"* (the two statements come from different stages of the reference port) | if it fails: no radio at all, and the #6 fallback disappears → UART-only | **settled by the first CI build**; do not flash a factory image before it is green |
| 8 | **5 GHz radio** | RTL8832BR (Wi-Fi 6) on PCIe; the vendor driver is Realtek's out-of-tree SKD (`rtk_wifi6`), mainline `rtw88` cannot bind it and `rtw89` is >=5.15 | none — it is simply absent, **by design**: seeding a radio with no driver makes netifd fail the whole wireless bring-up | documented in `seed.config`; the inherited `kmod-rtw88`/`rtl8822be-firmware` were removed in this audit |
| 9 | **LED / button GPIO** | vendor configures LEDs/blobs through its own `ugw_gpio` misc driver with ids 54 (green), 57 (red), 33 (RESET **and** WPS on one pin); those are **vendor indices, not** `rtl8197f-gpio` numbers. On the reference board an LED node on the wrong pin held the external switch in reset | switch held in reset = dead Ethernet, looks like a brick | deliberately **undeclared** in `dts/RX2LPRO.dts` (TODO) |
| 10 | **ramoops / pstore** | known TX wedge on this SoC family (CPU-originated frames dropped while RX works) | intermittent, brutal to debug | absent from our DTS by design |
| 11 | **hardware NAT armed too early** | the reference board's `dir842-asic` (S97) service does `fabric_reset`/`gw_prog`/hwnat arming using D-Link flash geometry, incl. a 17-byte read of `/dev/mtd1` — which here is the vendor **Bootloader** | wedged TX at boot, i.e. "it boots but you can't reach it" | **gated to `gwr1200ac-v1`** in this audit (uci-defaults + both init scripts) |
| 12 | **Vendor `tendaupgrade` KM signature gate** | the stock flasher writes `echo 1 > /proc/km/km_upgrade_ignore_kernel_sig`, and that node is mode **0440** | install refused, or a half-written KernelFS | only affects install path 3 (webui/`tendaupgrade`); paths 1/2/4 bypass it — see "Install paths" |

**Sequence that keeps all of the above cheap:** RAM-boot
`*-initramfs-kernel.bin` first (never writes flash) and read, in this order —
`cat /tmp/sysinfo/board_name` (= `rx2l-pro`), `dmesg | grep mtdsplit` (rootfs
offset), `cat /proc/mtd` (labels), `ip l` (eth0 present? link up?),
`ls /proc/wlan*/mib_all` (rtl8192cd loaded?), `uci show network wireless`.
Nothing gets flashed until each of those lines looks right on the console.

## Recovery net (verify offline before you need it)

* mtd1..mtd7 were dumped and are kept off-device. Re-flashing mtd1 restores the
  bootloader entirely.
* The stage-2 in mtd1 serves **`disaster_web`** (`printenv` in the loader shows
  `disaster_web=1 ipaddr=192.168.1.1 netmask=255.255.255.0 serverip=192.168.1.99`),
  i.e. a TFTP/HTTP recovery mode reachable without a serial console.
* The vendor runs a **dual image**: `KernelFS1`/`KernelFS2` with
  `env set image_boot 1|0 unlock`, so a boot failure can fall back to the other
  slot. `image0_stats`/`image1_stats` track which. Do not write either slot
  until the env semantics are confirmed on your own dump.

## Build

Local (Debian 11-era host) or container:

```sh
docker build -t owrt-rx2lpro .
docker run --rm -v "$PWD":/build -w /build owrt-rx2lpro ./build.sh
```

`build.sh` pins `ggbruno/openwrt @ 8a0ccb9` (the only tree with an `rtl8197f`
subtarget), overlays `files/`, pins both feeds to `openwrt-19.07` (master's
ucode-based LuCI does not build against this base), applies `seed.config`,
builds, and then **refuses to call the build a success until every produced
container passes `tools/check-cr6c.py`**.

```sh
python3 tests/test_container.py            # tool selftest, no vendor data needed
python3 tools/check-cr6c.py --stock --expect-start 0x80d00000 /path/to/mtd3_KernelFS.bin
python3 tools/check-cr6c.py --tenda --expect-start 0x80d00000 /path/to/decrypted.bin
```

GitHub Actions (`.github/workflows/build.yml`) runs the selftest, then the full
build inside the container, and uploads the images as artifacts.

## Status

Done: container format and its checksum cracked; vendor upgrade contract
documented; RSA gate characterized and its verdict settled; partition map,
RAM, console and MAC layout read from the device; `cr6c` device profile,
`tenda-wrap`, the geometry fix in `mtdsplit_cvimg`, and a CI gate that
self-tests against synthetic containers and real vendor images.

Not done, and honestly so: **no image in this repo has ever been booted on
hardware**. Unverified and deliberately left as `TODO(port-facts)` in the DTS:
the 2.4/5 GHz chips and their driver pairing, whether this board has an
external switch (RTL8367S-class) or runs on the SoC's built-in core, the LED
and button GPIO tables, and the CFM calibration-block offsets. Those are the
first things to settle before any flash write.

## Do not

* Do not write `/dev/mtd*` on a device you have not dumped.
* Do not commit decrypted vendor firmware, device MACs, or the console
  password derivation. This repo carries no vendor images.
* Do not run the vendor flasher on an image `tools/check-cr6c.py` rejected.
