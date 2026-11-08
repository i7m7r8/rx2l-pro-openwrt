# Shared brief for the 6.x / latest-OpenWrt port

Every agent working on `port/` reads this first. Facts here are **measured**, not
assumed; each line says where it came from so you can re-check it instead of
trusting me.

## 0. Ground rules (non-negotiable)

* The device is **read-only** for us: never write `/dev/mtd*`, never `flash_erase`,
  never `reboot`, never change sysctl/ifconfig state, never send ATE config-reset
  commands. All hardware claims in this file were harvested from *dumped* flash
  images, not from live poking.
* **No builds on this machine.** All builds run in GitHub Actions in this private
  repo. Locally you may run python tools, `unblob`/`strings`/capstone on the
  dumps, and static checks.
* **Never write a per-unit secret into this repo**: no MAC addresses, no serial
  numbers, no WPS PINs, no telnet root password (it is derived at runtime from a
  SALT + the LAN MAC). Use placeholders like `xx:xx:xx:xx:xx:xx` if you must
  show a shape.
* Do not `git commit` or `git push`; the orchestrator integrates and commits.
* Write **only** inside your assigned `port/NN-*/` directory. Never edit
  `build.sh`, `seed.config`, `files/`, or another agent's directory — propose the
  change in your `REPORT.md` instead.

## 1. The device

Tenda **RX2L Pro**, `devModel RX2LProV1.0`, internal board name `AX2_PRO_V2`,
Realtek **RTL8197F** (MIPS 32r2, single core, 128 MiB RAM), **16 MiB** SPI NOR
(`/sys/class/mtd/mtd0 name=ALL size=16711680`, every partition `erase=65536`),
stock `V16.03.51.18(6989)_cn`. Wi-Fi: on-SoC 2.4 GHz WMAC + PCIe **RTL8832BR**
(Wi-Fi 6) for 5 GHz. External Gigabit switch, RTL8367-family.

Flash map, measured from the device's own `/sys/class/mtd/*` dump
(`/tmp/rx_dump/flash/mtdlayout.txt`) — sizes in flash offsets:

    mtd0 ALL        16711680  @0x000000   the whole chip
    mtd1 Bootloader   196608  @0x000000   U-Boot, XIP at 0xb0000000
    mtd2 CFG           65536  @0x030000   MACs/device keys/RF (text KVs)
    mtd3 KernelFS   16252928  @0x040000   <-- the only partition we write (0xF80000)
    mtd4 RootFS     12318190  @0x400812   squashfs, carved out of KernelFS by mtdsplit
                    (0xBBF7EE; the sysfs dump in the same capture printed 12318702,
                     which cannot be right: 0x400812 + 0xBBF7EE = 0xFC0000 = mtd5 start,
                     while +12318702 would overrun it by 512 bytes. See
                     notes/firmware-audit.md.)
    mtd5 CFM          65536  @0xFC0000
    mtd6 CFM_BACKUP   65536  @0xFD0000
    mtd7 MISC2        65536  @0xFE0000   (0xFE0000+0x10000 = 0xFF0000 = 16 MiB, chip full)

`KernelFS` is one image, **`mtd0` exposes no KernelFS1/KernelFS2**, so the
stock partition table has no second bank — but `tendaupgrade` contains
`KernelFS1`/`KernelFS2` strings and touches the `image_boot` env var, so
"no rollback is possible" is an *inference from the partition table*, not a
proof. That is exactly what `port/08-veto-paths/` is testing; do not flash
anything before it reports. Recovery meanwhile is `disaster_web`
(`ipaddr=192.168.1.1 serverip=192.168.1.99`) plus the mtd1..mtd7 dumps kept
off-device.

## 2. Endianness — settled, and it constrains the base

The stock kernel is **MIPS little-endian**. Proof: the kallsyms address table at
blob `+0x9d2428` of `/tmp/work3/kernel.lzma.out` decodes as a strictly ascending
run of **LE** u32s (13,537 entries, last `0x806b97b0`); the same region read as BE
yields zero monotonic runs anywhere in the file.

Both candidate bases agree: toby `rtl8197f/target.mk: ARCH:=mipsel`,
rx23302 `target/linux/rtl819x/Makefile: ARCH:=mipsel`. A big-endian build will not
run on this strap — if you find a BE assumption anywhere, that is a bug.

## 3. Boot contract (from disassembling our own mtd1 — `/tmp/work4/FINDINGS.md`)

* mtd1 is XIP at `0xb0000000`; stage-1 copies flash `0x7000..0x203a0` to
  `0x80100000`, which gunzips U-Boot proper into `0x80000000`.
* Kernel loading is **header-driven**: magic at container+0 must be `cr6c` or
  `cs6c` (else `No sys signature at %X!`), then an additive checksum of **BE u16
  words over `len` bytes starting at container+0x10 must sum to 0 mod 2^16**
  (verified on our mtd3 dump). Jump target = header `start_addr`
  (`lw $s0,4($s0)` → `jalr`); copy destination = `start_addr | 0x20000000`
  (uncached alias). `burn_addr` is byteswapped and **unused**. There is **no
  RSA/TDE01/KM/squashfs gate on the boot path** — signature checks live in
  `tendaupgrade` (userspace) and the KM module, not in the loader.
* U-Boot never decompresses the kernel; the container's own LZMA stub does. Keep
  the decompressed kernel **< 32 MiB** because the loader links at `0x82000000`
  (window = `start_addr - LOADADDR`, so it is our choice: `FUN_8001081c` uses
  +0x04 verbatim, LOADER.md §6). It was 16 MiB until the initramfs image measured
  18,201,116 bytes.
* Disaster-web/TFTP recovery is gated by `0xb8003528 & 2` (reset strap), separate
  from the kernel path.

## 4. Container formats we must reproduce

`cr6c` header (16 B, all BE): `signature[4]`, `start_addr`, `burn_addr`, `len`;
payload; then a 2-byte checksum `htons(~sum16+1)` at `16+len`; squashfs starts at
exactly `16+len`. Stock mangles the squashfs magic `hsqs`→`nice` (and XZ magic) —
**our own kernel must not be mangled**, because our `mtdsplit` matches stock
`SQUASHFS_MAGIC`.

Tenda plaintext header (what `decry_firm` yields, 64 B): `+0x00 27 05 19 56`,
`+0x0c` BE payload len, `+0x10 0x80000000`, `+0x14` BE kref `0x808d45c0`,
`+0x18` **CRC32(payload)**, `+0x1c 05 05 02 03 02 00 00 00`, `+0x24 0x00031053`,
`+0x28` BE total len. `tools/tenda-wrap` round-trips a real vendor image with zero
differing bytes. TDE01 = 512 B RSA-SHA-512 + AES-CBC `Salted__`; nothing under it
is a security boundary.

## 5. Switch — what we know about *our* board

Sources: our U-Boot (`/tmp/work4/uboot_gz.bin`), our stock kernel blob, and the
sibling Tenda boards already ported in the two bases.

* Our U-Boot contains the vendor switch SDK: strings `Switch v1.4 API: switchChip= %s`,
  `8367C`, `8367D`, `rtl8367c_setAsicReg`, `rtk_port_rgmiiDelayExt_set(logical_cpu_port, TX_DELAY, RX_DELAY)`.
* Our U-Boot does a read-modify-write of **`0x03300000`** into a masked field at
  `0x80006a68` and `0x80006c34` (`lui $v0,0x330` / `and` mask / `or` / `sw`).
  toby's Tenda DTS encodes exactly that as
  `pinctrl-single,bits = <0x34 0x03300000 0x0ff00000>` — i.e. **our SMI pin mux is
  the same one those boards use**: MDC = SDK H0 = GPIO line 56, MDIO = SDK G7 =
  GPIO line 55, reset = H2 = line 58.
* Our stock kernel carries the SDK pin table: strings
  `rtl819x_8367r_i2c_pin`, `8367r i2c pin1`, `8367r i2c pin2`,
  `rtl819x_8367r_reset_pin` (rodata at blob `+0xa4882c`, referenced from data at
  `+0xb5ac70`/`+0xb5ad70`/`+0xb5ae78`, index fields 1 and 2).
* Stock userspace (`/etc/wlan/eth_to_port_config.txt` etc., harvested earlier):
  jacks are switch ports **1..4** (`eth_ports = 4 3 2 1`), WAN = right-most jack =
  **port 1**, `lanvlan = 1 0x15d 0x40`, `wanvlan1 = 2 0x142 0x40`. The third field
  (untagged mask) is **`0x40` = bit 6** → the CPU/trunk port is **port 6**, which
  matches toby's `rtl8197f_tenda_{ac23,lynx}.dts`: `port@6 { label="cpu" }`,
  `realtek,tx-port-mask = <0x40>`, `realtek,tx-dp-ext = <0x2>`, `realtek,tx-extspa = <0>`,
  `rx-internal-delay-ps` per board. (RD05/Xiaomi is the odd one out: port 7/EXT1,
  mask `0x1` — do not copy it.)
* rx23302 instead drives the same chip class with the **swconfig** driver
  `drivers/net/phy/rtl8367b.c`, whose header says it supports RTL8367RB, R-VB,
  RB-VB, S, S-VB, D, binding
  `compatible="realtek,rtl8367b"; sck-gpios; sda-gpios; reset-gpios; realtek,cpu-port=<7>; realtek,init-regs=<...>`
  (~140 reg pairs, and it notes RTL8367D needs ~1.2 s after reset before it
  answers SMI). Its own board (`electra,rx23302`) uses `&gpio1 24/23/26` = lines
  56/55/58 — the same physical lines, different controller numbering. **Check
  which gpiochip/number the RTL8197F driver exposes before copying numbers.**

## 6. Wi-Fi

* 2.4 GHz = on-SoC WMAC, vendor `rtl8192cd` HAL (8192xb). RF calibration is 8192 B
  of CFG read from flash offset 8 → `/etc/wlan_rfhwdata` → `/bin/rfhw_convert` →
  `/tmp/rfhw.sh`. Stock has **no `/lib/firmware` at all**; PHY tables are embedded
  in the driver blob. rx23302 runs this same HAL on 6.18 (`rtl8192fe/`,
  `CONFIG_WLAN_HAL_8197G`, PCI IDs 10ec:818c/f192/8193/8179/f812); iptime rebuilt
  it from Cudy's GPL drop.
* 5 GHz = **RTL8832BR (Wi-Fi 6) over PCIe, RFE50**, params in
  `/etc_ro/wlan/rtl8832bre/RFE50/{PHY_REG.txt,...AP_5G...}` loaded via
  `iwpriv wlan0 flash_set para_path=/etc/wlan/`. Mainline `rtw89` has **no 8832**
  (only 8851B/8852A/B/BT/C, 8922A/D). No tree we have drives 8832BR on 6.x:
  only the stock vendor `rtk_wifi6` on 4.4 does. toby attempted the SKD and got
  through BAR/data-bus, then stalled on IRQ/timer softirq. Treat 5 GHz as its own
  research track, not a blocker for first boot.

## 7. Candidate bases (both verified present locally)

| | path | base | kernel | switch | eth/offload | image |
|---|---|---|---|---|---|---|
| A (primary) | `/tmp/rebase/openwrt-rx23302` | upstream openwrt@`82e2e4f4` + repo overlay | **6.18.41** | swconfig `rtl8367b.c` (RB family explicit) | PHYLINK `rtl819x-eth` + `rtl819x_hwnat.c` on `flow_offload` (measured 857–929 Mbit/s) | `tools/cvimg` w/ cr6b/cr6c/cs6c, `LZMA_TEXT_START=0x82000000` |
| B (reference) | `/tmp/rebase-base` (tobyw121/Openwrt_RTL `MW5_Stable`) | 24.10-SNAPSHOT era | 6.6.114 | DSA `net/dsa/rtl8367{,5mb}` + rtl8_4 tags | DSA + tc | `scripts/rtl8197f-image.py`, `RTL8197F_IMAGE_SIGNATURE := cr6c`, in-tree `rtkload/` |

A's SoC bindings match our 4.14 tree exactly (`"realtek,rtl8197f-soc"`,
`spi@18143000 "realtek,sheipa-spi"`, `pcie-controller@18b00000 "realtek,rtl8196b-pci"`,
`ethernet@18010000 "realtek,rtl8197f-eth"`), so a new board DTS is drop-in.
B holds the sibling Tenda DTS files and the cr6c image recipe we still want to
mine. Neither has `mtdsplit_cvimg` — that is ours to port (or switch to
fixed-partitions; whichever agent owns images must decide and justify).

## 8. What "done" looks like

User requirement, verbatim: **"i acctually need fully working high perfomance
latest openwrt"**, installable as **`tenda webui upgrade .bin`** → OpenWrt. So:
kernel 6.x + current OpenWrt userland, gigabit switching, wired NAT accel,
2.4 GHz up on first boot, 5 GHz tracked separately, and a factory image that the
stock `tendaupgrade` path accepts.
