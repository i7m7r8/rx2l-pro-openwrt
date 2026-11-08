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

## Driver inventory (2026-11-07 evening): what is actually missing

Asked directly whether any hardware driver is missing, and re-checked against the
pinned base rather than against memory. Answer: **one driver, plus one disabled
subsystem** — and the subsystem is the one that blocks the driver.

| hardware | driver | state on this base |
|---|---|---|
| RTL8197F SoC ethernet + HW NAT | `rtl819x_eth.c` + `rtl819x_hwnat.c` | ✔ present, `CONFIG_NET_RTL819X=y`, 720 patch builds both |
| external RTL8367RB switch | `rtl8367b.c` + `rtl8366_smi.c` | ✔ present, **was never compiled** — no header/Makefile/Kconfig; now selected by `CONFIG_RTL8366_SMI=y` + `CONFIG_RTL8367B_PHY=y` against openwrt's own `700-swconfig_switch_drivers.patch` (a `patches-6.18/999` of ours was deleted: it duplicated the symbol and produced a Kconfig recursive dependency — see `port/03-dts-switch/REPORT.md`) |
| on-SoC 2.4 GHz WMAC | `rtl8192fe` HAL | ✔ present, wired by patch 737, config symbols real |
| SPI NOR, GPIO, timers, IRQ | platform code | ✔ `arch/mips/realtek/*` |
| **PCIe root complex** | `arch/mips/pci/pci-realtek.c` | ⚠ **present but switched off**: `# CONFIG_PCI is not set` (`rtl8197f/config-6.18:244`) |
| **5 GHz RTL8832BR (10ec:b832)** | vendor G6 SKD (`rtk_wifi6`) | ✗ **not in tree, not in mainline**; staged plan in `skd-attempt/` |

So the PCIe controller is *not* missing: 468 lines sit in
`files-6.18/arch/mips/pci/pci-realtek.c`, it matches
`compatible = "realtek,rtl8196b-pci"`, and our board DTS already sets
`&pcie0 { status = "okay"; }` on the SoC's node (reg/reg-names/ranges all
present). What is missing is the config switch — and the platform's own note is
that turning it on hung their boot, which is why it is off.

The 5 GHz gap therefore has two stages, in this order:

1. **Enable PCI.** One line in the subtarget config (`CONFIG_PCI=y`, and
   `CONFIG_PCI_QUIRKS=y` is probably wanted for the legacy host bridge), then
   validate a boot **from the initramfs image over serial** — never the first
   flash — because the failure mode reported upstream is a boot hang, not a
   missing device. The DTS side needs nothing.
2. **Then the driver.** `10ec:b832` has no mainline driver in v6.18 or master
   (rtw89 knows `b851/b852/b85b/c852/8922` only; re-checked today), and no
   firmware file exists that we could legitimately ship. The vendor SKD is the
   only route, already staged with a 6.6 compile proven elsewhere.

It is deliberately NOT enabled in this tree: the first image we hand over must
boot, and a PCIe hang would take the whole board down before anyone could see
whether the rest works. The experiment belongs to a boot on the bench, with the
serial console open and the initramfs image — not to the first flash.

## The PCIe driver may need no porting at all — evidence and a prediction

Before the experiment branch finishes, the API surface was checked directly
against v6.18, because "does it compile" and "does it work" are separate
questions and only the first can be answered in CI.

`arch/mips/pci/pci-realtek.c` is written against the **legacy MIPS PCI core**:

| it uses | v6.18 still has it? |
|---|---|
| `struct pci_controller` | ✔ `arch/mips/include/asm/pci.h:30` |
| `register_pci_controller()` | ✔ declared `pci.h:58`, defined `arch/mips/pci/pci-legacy.c` |
| `pcibios_map_irq(const struct pci_dev *, u8, u8)` | ✔ same signature, `pci.h:63` |
| `pci_ops` with `.read/.write` | ✔ unchanged shape |
| `ioport_resource` / `iomem_resource` mutation | ✔ still writable symbols |

And the core that provides `register_pci_controller` is selected by
`CONFIG_PCI_DRIVERS_LEGACY` (`arch/mips/pci/Makefile:7`), which this port
already sets — it is `def_bool !PCI_DRIVERS_GENERIC`, and the Realtek platform
never selects the generic one.

**Prediction, to be checked against the build:** `pci-realtek.c` compiles on
6.18 unchanged once `HAVE_PCI` is reachable. The only thing that ever stopped it
was the platform's `select HW_HAS_PCI`, a symbol current kernels do not have
(see above). Two known non-blocking defects were left alone deliberately:
`devm_clk_get()` is tested with `if (!rpc->clk)` instead of `IS_ERR()`, and
`pcibios_map_irq()` hardcodes IRQ 5 with a TODO for the second slot — both are
behaviour bugs to revisit if/when the card is brought up, not compile errors,
and changing them before we can test on hardware would only make the first
boot harder to reason about.

If it compiles, the 5 GHz sequence becomes: enable PCI (config only) → boot
validate on the bench → then the SKD port. If it does not, the failure will name
which of the five rows above moved.

## What the stock 5 GHz stack needs at runtime (from the rootfs, not the kernel)

The vendor rootfs carries **no firmware blob** for the radio — `find` over the
whole tree returns only the module metadata under
`/lib/modules/4.4.176/*.bin`. `rtk_wifi6` is built into the kernel with its
firmware embedded, so a port does not have to ship an `rtw*.bin` under
`/lib/firmware/`; it has to carry the firmware in the driver image, exactly as the
G6 SKD does.

What *is* on the rootfs, under `/etc_ro/wlan/`:

| path | what |
|---|---|
| `rtl8832bre/RFE50/**` | 38 text-table files: `PHY_REG.txt`, `PHY_REG_GAIN.txt`, `RadioA.txt`, `RadioB.txt`, `TXPWR_ByRate.txt`, `TXPWR_LMT.txt`, `TXPWR_LMT_RU.txt`, `TXPWR_TrackTSSI.txt`, per band (`AP_2G`, `AP_5G`, and `PON_*` variants) |
| `rtl8832bre/RFE50/*.txt` (top level) | the active copies; the per-band subdirectories are stubs — see the calibration note in `port/05` |
| `.wlan_rfhw.cfg` | the factory apmib template (49 `HW_*` keys) |
| `ther.conf` | thermal trip points, one line per radio: `wlan0 56 52 45 …`, `wlan1 70 65 58 …` |

So the 5 GHz bring-up surface is: one driver with embedded firmware, and a
directory of text calibration tables read at `flash_set para_path=/etc/wlan/`
(the vendor's `rcS` line). Nothing in that path is a binary blob we would have to
reverse -- which is why track 06 can be a *port*, not an RE exercise.

The `ther.conf` split is worth noting for later: the two radios have different
cut-offs (56 vs 70 °C), which is the kind of per-radio constant a shared thermal
handler would get wrong.
