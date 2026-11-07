# REPORT — 5 GHz RTL8832BR track

**Recommended route: (a), vendor G6 SKD (`rtk_wifi6`), as a staged,
manually-probed kernel package — with (c) (no 5 GHz in v1) staying the shipped
default.** Rationale and measured facts: `routes.md`. The SKD tree already
compiles for mipsel on Linux 6.6 with modpost clean, on this exact SoC+chip
(`/tmp/rebase-base` commit `830c08cf`), and 12 of our 16 RFE50 calibration
files are byte-identical to that port's copies (`stock-evidence.md` §5). Route
(b) is blocked twice over — no `10ec:b832` in rtw89 and no
`rtw89/rtw8832_fw.bin` we could legitimately ship (`rtw89-gap.md` §4).

Deliverables in this directory:
`routes.md`, `stock-evidence.md`, `rtw89-gap.md`,
`skd-attempt/README.md` (staged plan + compat punch list + failure-mode table),
`skd-attempt/Makefile`, `skd-attempt/patches/0000-series.md`,
`skd-attempt/files/{rfe50-manifest.sha256,8832-bringup-diag}`.

## New facts established this session (all re-verified from the dumps)

1. **PCI ID of our client = `10ec:b832`**, from the `pci_device_id` table at
   `/tmp/work3/kernel.lzma.out` offset `0xb7787c` (also `b814`=RTL8852A and
   `0192`=RTL8192XB at `0xb77860`/`0xb77898`, all subsystem-ANY).
2. **INTx on MIPS CPU line 5**, measured from stock runtime:
   `/tmp/rx_dump/logs/syslog/sys_info.log:501` `5: 968696 MIPS 5 rtk_wifi6`
   (`6: wlan1` = 2.4 GHz, `7: timer`, `ERR: 13`). Root complex has no MSI.
3. Driver is **built into** the stock kernel (`/etc_ro/init.d/rcS:57`, the
   `insmod rtk_wifi6.ko` lines 61–67 are commented out); full PHL/G6 module-param
   surface present (`rtk_wifi6.rtw_fw_file_path`, `phl_log_level`, …).
4. **Firmware is embedded** — stock has no `/lib/firmware`, no wifi `.bin`, and
   the equivalent G6 build sets `CONFIG_FILE_FWIMG=n`.
5. **RF tables are file-loaded and mandatory** (eFEM `rfe_type >= 50`):
   `/etc_ro/wlan/rtl8832bre/RFE50/**` (40 files) + `rcS:69`
   `iwpriv wlan0 flash_set para_path=/etc/wlan/`. `AP_5G/PHY_REG.txt` and
   `AP_5G/Radio{A,B}.txt` are 33–37-byte stubs — the active tables are the
   parent-level ones.
6. **`wlan0` = 5 GHz, `wlan1` = 2.4 GHz** (inverted vs. habit) — stock
   `km_online_ip` tags and `/tmp/rebase/mr1500x-openwrt` init script.
7. Base A (`openwrt-rx23302`, 6.18) currently has `# CONFIG_PCI is not set` and
   `&pcie0` **disabled**, with a note that enabling the PCI bus hung their boot.
   **This is the real blocker for 5 GHz, and it is not a driver problem.**
8. The sibling AC23 DTS node `pci10ec,b814` is a **RTL8852A**, not our family
   member, and its binding is toby-local (userspace nvmem hints), not a driver
   match. Its *PCIe plumbing* (identity ranges + `<&intc 21 4>` = CPU5) is
   exactly what we need and is copied from measured stock behaviour.

## First experiment, once the box boots 2.4 GHz

Goal: prove the PCIe fabric before any Wi-Fi code exists. Steps, on a
RAM-boot (`*-initramfs-kernel.bin`, nothing flashed):

0. Build-side prerequisite: enable `CONFIG_PCI` (+ the RTL8197F/`rtl8196b-pci`
   host driver) for the subtarget and set `&pcie0 { status = "okay"; }` with
   `interrupt-parent = <&intc>; interrupts = <21 4>;` and identity `ranges`
   (§7 of `routes.md`). If base A's "enabling PCI hung boot" reappears,
   **that is the experiment's result** — capture it and stop; the SKD cannot be
   evaluated on a bus that does not enumerate.
1. Copy `skd-attempt/files/8832-bringup-diag` to the box and run it.
2. Expect, and record: one endpoint `10ec:b832`;
   `/sys/bus/pci/devices/0000:00:00.0/resource` BAR2 row (third line) =
   `0x19000000 … 0x190fffff [mem]`; BAR2 raw in config space = `0x19000004`
   (64-bit, base 0x19000000); `.../irq` non-zero; `dmesg` showing PCIe link-up
   (single lane; generation never measured here) with no `Data bus error`.
3. Only if step 2 is clean: `insmod rtk_wifi6.ko` **once**, then
   `dmesg | tail -80` and classify with the table in
   `skd-attempt/README.md` §4 (A BAR → B IRQ → C timer softirq). Never
   load/unload repeatedly; a wedged radio on this family survives reboot and
   needs 10+ s without power.
4. If C reproduces, capture `/proc/timer_list` + full dmesg and treat the
   vendor timer-init audit as the next work item — do not paper over it with
   extra `del_timer_sync()` calls (toby tried the fast-fail route and left
   autoload off for exactly this reason).

Cheap parallel win available today: the 6.6 patch series plus our RF manifest
can be built and *compile-tested* in CI before any hardware time, because the
6.18 compat delta (§3 of `skd-attempt/README.md`) is the part that needs
iteration and it needs no device.

## Needs from elsewhere

* **images / DTS agent**: `CONFIG_PCI` decision for base A, `&pcie0` enablement
  with identity ranges + `<&intc 21 4>`, and an explanation of rx23302's
  "enabling PCI hung the boot". Highest-value single item on this track.
* **kernel-6.18 agent**: confirm `pci-realtek.c` (`realtek,rtl8196b-pci`) vs
  toby's `pcie-rtl8197f.c` — which host driver we ship decides whether the
  BAR-zero bug is even reachable (`arch/mips/pci/pci-realtek.c:321-350` builds
  hose resources from `range.cpu_addr`, which is identity-correct).
* **build/CI agent**: run `make package/kernel/rtl8832br-rx2lpro/compile V=s`
  against the inherited 6.6 series and send back the first 50 compiler errors;
  that turns `skd-attempt/README.md` §3 from prediction into a punch list.
* **firmware/provenance agent**: locate Tenda's own RX2LPro GPL drop (its
  embedded FW array and BSP `rtl8197h_v3.6.0_4.4.176` source are what our units
  actually run) and confirm redistribution position for (a) the G6 source tree
  and (b) extracted RF tables. Package skeleton assumes a TP-Link GPL archive;
  swapping source is one `PKG_SOURCE` line.
* **userspace/netifd agent**: if a wiphy ever appears, the 5 GHz radio must be
  brought up hostapd-style, *not* through the stock `mac80211.sh`
  `iw phy … interface add` path, which hard-hangs this driver. mr1500x's
  `files/etc/init.d/mr1500x-wifi` is a proven template (same chip, vendor
  kernel) including the country-code-via-proc and never-pre-`up` rules.
* Nobody needs anything from me for v1: 5 GHz stays absent by design, and that
  is already correct in `seed.config` / README risk row 8.
