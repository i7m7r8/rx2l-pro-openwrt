# Digest: what the 6.6 reference base (tobyw121/Openwrt_RTL@MW5_Stable) really is

Source: full read of `/tmp/rebase-base` (`/tmp/rebase-base-read/PORTMAP.md` holds
the long form, 655 lines). This is *reference* material — our primary base is the
6.18 tree — but four of its findings change how we integrate, and two are traps
that would have cost us a debug cycle each.

## Structure

* `target/linux/realtek/image/Makefile:269` is only `include $(SUBTARGET).mk`;
  every board lives in `image/rtl8197f.mk` as `define Device/<name>` +
  `TARGET_DEVICES += <name>`. `DEVICE_VARS` are registered at `image/Makefile:26-44`.
* `rtl8197f/board-profiles-v42.json` is **not** a generator — it is an evidence
  database, validated against the makefiles and DTS by
  `scripts/rtl8197f-validate-board-profiles.py`.
* A new board needs, minimally: an explicit `DEVICE_DTS` (SOC is unset, so
  `SUPPORTED_DEVICES` falls back to `$(subst _,$(comma),$(1))`,
  `include/image.mk:516`), and sysupgrade metadata comes free from upstream
  `append-metadata` (`include/image-commands.mk:91`, fwtool + usign).

## Container / loader

* `scripts/rtl8197f-image.py` computes `sig[4] + be32 startAddr + be32 burnAddr +
  be32 len` with `len = payload + pad + 2` and checksum
  `(-sum16_be(payload)) & 0xFFFF` — the same arithmetic as our
  `htons(~sum16+1)`.
* `Build/rtl8197f-rootfs` → `scripts/rtl8197f-rootfs-checksum.py` writes a BE32
  length marker at squashfs `+0x08` holding `align_up(bytes_used)+2-0x282`, then
  4 KiB zero-pad and the BE16 checksum. Rootfs-at-`16+len` holds by construction.
* The cr6c payload is upstream `image/lzma-loader`, driven by
  `Build/rtl8197f-loader-kernel`, with global `KERNEL_LOADADDR = 0x80100000`
  (`image/Makefile:6`) and per-device `RTL8197F_LZMA_TEXT_START`. `rtkload/` is
  vendored SDK *reference* only (it needs `../.config` and `$(DIR_USERS)`); its
  `start_no_flash.S` RAM variant is the 0x80500000→0x81000000 pattern.
* **Trap 1 — the console whitelist.** `image/lzma-loader/src/board.c:23-32` and
  `head.S:34-43` gate the Realtek UART + watchdog-stop path on a hardcoded
  `CONFIG_BOARD_*` list. A new board symbol that is not in that list silently
  gets the AR71XX console path: no serial output exactly when you need it.
* **Trap 2 — address collisions are real.** One MW5 build overflowed the
  equal-address case by `0xd5b` bytes. Our own budget must be explicit, not
  inherited: loader at `0x81000000`, decompressed kernel below 16 MiB.
* **RAM boot exists** and we should copy the shape of it:
  `Device/cudy_ax1500_ram` uses `KERNEL_INITRAMFS ... | check-size 31744k` with
  `KERNEL_INITRAMFS_SUFFIX := -ram-loader.bin` and `rdinit=/sbin/init`. That is
  the no-flash-write way to test any of this.

## Root split

* `target/linux/generic/files/drivers/mtd/mtdsplit/mtdsplit_rtl8197f.c`, enabled
  by `CONFIG_MTD_SPLIT_RTL8197F_FW=y` (`rtl8197f/config-6.6:250`), computes
  `rootfs_offset = 0x10 + be32 len` and creates `kernel`/`rootfs`.
* The trigger is name-based (`mtd_partition_split()` in
  `generic/pending-6.6/400-mtd-mtdsplit-support.patch`): the partition label must
  be **exactly `firmware`** and the node must have **no `compatible` property**.
  No `root=` in bootargs (only `rootfstype=squashfs,jffs2`); root comes from
  `CONFIG_MTD_ROOTFS_ROOT_DEV`.
* Squashfs magic is never mangled in a working build — plain `hsqs` everywhere.

## Performance reality check

* Ethernet is `files-6.6/drivers/net/ethernet/rtl8197f_rtknet.c` (`1.8.48-cudy-r76-dsa`),
  bound via `realtek,rtl8197{d,f,fh,g}-rtknet` / `realtek,rtl865x-cpuif`, with
  ~40 runtime `of_machine_is_compatible()` board gates.
* It implements `ndo_setup_tc` for `TC_SETUP_FT` but **returns `-EOPNOTSUPP`
  under its "strict firewall policy"**; `/proc/rd05-rtknet` reports
  `firewall_policy mode=strict routed_shortcuts=0 tc_flowtable_hw=0 dma_path=linux`.
  HW NAPT there is a disabled prototype.
* `ACCELERATION.md` contains **no measured throughput at all** — "working" means
  a configuration check passed. `RD05_TX_DMA_AB.md` is a diagnostic gated to
  `xiaomi,r4-rd05` only. The MW5 TX watchdog (5 s without OWN-descriptor
  progress → re-kick) is the one genuinely relevant CPU-TX mitigation, and it is
  **not** enabled for AC23/RD05.
* Conclusion for us: the "high performance" requirement is met by the 6.18
  tree's phylink + `flow_offload` hwNAT (857–929 Mbit/s measured), not by this
  driver. Confirms the base choice rather than weakening it.

## Wi-Fi, and two useful ideas

* 2.4 GHz = platform backend `src/linux6/rtl8197f_soc.c` of
  `package/kernel/rtl8192cd-rtl8197f` (4 variants; 8197G HAL for RD05/AC23,
  8197F HAL for MW5), cfg80211 against 6.6.114 + backports 6.12.96, WEXT deleted.
* 5 GHz = `package/kernel/rtl8832br-wr1500` → `rtk_wifi6.ko` built from TP-Link's
  `GPL_ax1500v3.tar.gz`. Its history matches our own: BAR/data-bus window faults
  (fixed with PCI windows `0x18c00000`/`0x19000000` + BAR validation), then
  rev-4 failed at IRQ allocation and the timer softirq, autoload removed, AP mode
  never worked, open TODOs "empty RF path and eFuse/MAC errors" — and the probe
  hard-gates on `cudy,ax1500-r76`.
* **Idea worth taking:** they read the Tenda text KV factory data through
  **NVMEM** (`rtl8192cd_board.c`), and their `02_network:131` already does
  `mtd_get_mac_ascii factory TENDA.lan_mac`. That is the clean replacement for
  our `/bin/rfhw_convert` + `/tmp/rfhw.sh` userspace dance — MAC and calibration
  both as nvmem cells, no vendor binary in the boot path.
* **Idea worth taking 2:** their validation script pattern (JSON evidence DB
  asserted against the makefile/DTS) is how we should encode our own board
  contract so CI catches drift.

## Licensing / hygiene

The tree is a squashed 341-commit fork of OpenWrt 24.10-SNAPSHOT with **no
upstream merge-base** — you must diff by content, not history. Everything
relevant declares GPL-2.0/-or-later (`rtkload` is Jay Carlson GPLv2, rtl8192cd is
Realtek GPL-2.0 with stray ISC headers, rtl8832br is a pinned-hash source offer),
but it ships **7 private `fullflash-templates/*.bin` marked "must never be
published"** plus WlanHAL RF/fw `.bin` sets. We reference this tree; we do not
vendor those blobs into our repo. No upstreaming/attribution obligations are
stated anywhere in it.
