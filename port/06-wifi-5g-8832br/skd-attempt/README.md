# Route (a) staged attempt — Realtek G6 `rtk_wifi6` against kernel 6.18

Status: **skeleton + research plan, nothing built** (no builds on this host).
Files here: `Makefile` (package proposal), `files/rfe50-manifest.sha256`,
`files/8832-bringup-diag` (read-only RAM-boot triage),
`patches/0000-series.md` (what the patch series must contain).

## 1. What we are building

Realtek's out-of-tree Wi-Fi 6 driver ("G6" / PHL architecture), module name
`rtk_wifi6`, driving `10ec:b832` (RTL8832BR, RFE50 eFEM) on the RTL8197F's
single-lane PCIe root complex. This is the same driver the stock 4.4 kernel has
**built in** (`stock-evidence.md` §1).

Source: TP-Link `GPL_ax1500v3.tar.gz`, subtree
`rtl8198/linux-4.4.x/drivers/net/wireless/realtek/g6_wifi_driver`,
`PKG_HASH 6d133598b134b6878ce802a53b3061d9f2efb3636ad0d31ec4cb3bcbdf438f2a`
(the archive the 6.6 port is proven against). Alternative drops of the same
tree: Mercusys MR1500X/MR60X GPL (`/tmp/rebase/mr1500x-openwrt` uses one).
Worth trying: Tenda's own `RX2LProV1.0` GPL drop, if the images agent can find
it, because its embedded firmware array is the one our units ship with.

## 2. What the 6.6 attempt already proved (read this before writing patches)

Source: `/tmp/rebase-base` git objects (that checkout is sparse — use
`git show HEAD:package/kernel/rtl8832br-wr1500/...`), commits `830c08cf`,
`3dfa5900`, `d5f1b86e`, and
`reverse-engineering/wr1500-wifi6/LINUX66-DRIVER.md`.

* The tree **compiles and modposts clean for mipsel on 6.6.114** against
  OpenWrt `mac80211` backports 6.12.96, as `rtk_wifi6.ko`. Three patches:
  `100-linux66-openwrt-wr1500.patch` (2563 lines),
  `102-check-wr1500-mmio-window.patch`, `103-wr1500-pci-irq-preflight.patch`.
* Files the compat patch had to touch (measured, from the patch itself):
  `Makefile`, `autoconf_rtl8832br_pci_linux.h`, `common.mk`,
  `core/rtw_mib.c`, `core/rtw_mlme_ext.c`, `core/rtw_recv.c`,
  `core/rtw_trx_pci.c`, `core/rtw_xmit.c`, `core/rtw_xmit_shortcut.c`,
  `core/rtw_wlan_util.c`, `include/drv_types.h`, `include/openwrt_g6.h`,
  `include/osdep_service_linux.h`, `os_dep/linux/ioctl_cfg80211.c`,
  `mlme_linux.c`, `os_intfs.c`, `pci_intf.c`, `rtw_cfgvendor.c`,
  `wifi_regd.c`, `xmit_linux.c`, `os_dep/osdep_service_linux.c`,
  `phl/hal_g6/mac/chip_cfg.h`, `phl/hal_g6/mac/mac_ax/gpio.c`,
  `phl/hci/phl_trx_def_pcie.h`, `phl/hci/phl_trx_pcie.c`, `phl/phl_trx_def.h`,
  `phl/phl_types.h`, `phl/pltfm_ops_linux.h`, `phl/test/trx_test.c`,
  `platform/mips_97f.mk`, `platform/mips_97f/rtw_pltfm_{pre,post}conf.h`,
  `platform/platform_mips_97f_pci.c`, `platform/platform_ops.h`.
* Concrete substitutions (their words, "tatsächlich umgesetzt"):
  `kernel_read`/`kernel_write` file helpers replacing `get_fs/set_fs`;
  size checks on the int/u32 file-helper API; `path_put()` after directory
  lookup; `eth_hw_addr_set()` instead of writing `dev_addr`;
  `get_random_u32()` for removed `prandom_u32()`; `INIT_CSD` instead of
  touching internal `csd.flags` for SMP init; `PCI_DMA_FROMDEVICE`/
  `PCI_DMA_TODEVICE` → `DMA_FROM_DEVICE`/`DMA_TO_DEVICE`; removal of
  `pci_map_single`/`pci_unmap_single`/`pci_dma_sync_single_*`/
  `pci_alloc_consistent`/`pci_free_consistent` in favour of the DMA API —
  and explicitly **not** a rename-only change: the OEM path ignored mapping
  errors in void TX helpers and had a `virt_to_phys` fallback, so errors had
  to be propagated to callers and partial TX mappings unwound;
  tasklet-context and cfg80211-callback adaptation; include-path fix so
  `mac80211` `Build/InstallDev` headers (`sta_info.h`) resolve and the vendor's
  userspace-only `include/linux/wireless.h` does not shadow kernel headers.
* Things they **disabled** to get a clean build: `CONFIG_BR_EXT=n`,
  `CONFIG_PROC_DEBUG=n`, `CONFIG_APPEND_VENDOR_IE_ENABLE=n`,
  WEXT/private ioctl and OEM vendor-command surfaces, private
  bridge/NAT/fleet-conntrack paths, TX descriptor cache. Do not re-enable any
  of these for a first attempt.
* Two build-time guards worth copying verbatim into our series: the RF-table
  SHA256 gate in `Build/Prepare` (16 files) and the relative-include fix in
  `drv_types.h`.

## 3. The 6.18 delta

**Measured:** none — nobody has compiled this tree on 6.18.
**Predicted** (from what broke between 6.6/6.12 and what changed in 6.13–6.18;
treat as a first-iteration punch list, the CI build is the arbiter):

| area | likely breakage | where |
|---|---|---|
| cfg80211/mac80211 ops signatures | `change_beacon`/`del_key`/`get_key`/`add_key` const-ness, `station_state`, `get_txpower`, `set_monitor_channel`, channel-switch notify args | `os_dep/linux/ioctl_cfg80211.c` |
| TX/RX status helpers | the `_ni` variants (`ieee80211_tx_status_ni`, `ieee80211_rx_ni`) are gone upstream; a driver written for 4.4 calls them | `core/rtw_recv.c`, `os_dep/linux/xmit_linux.c` (note: backports normally papers over this — our flags use backport headers) |
| /proc | `PDE_DATA()` removed, `struct proc_ops` required, `seq_file` `single_open` users | `os_dep/linux/proc*` (only if `CONFIG_PROC_DEBUG=y`; keep off for attempt 1) |
| device/class API | `class_create()` is one-arg since 6.4 | `os_dep/linux/*` |
| timers/workqueues | `timer_setup`/`from_timer` are long settled, but **the vendor's own half-initialised timers are the suspected 6.6 hardware failure** — audit every `rtw_timer`/`_rtw_timer_func`/`rtw_os_add_timer` add/delete path, do not paper over it with extra cancellation | `os_dep/linux/osdep_service_linux.h`, `platform/*` |
| tasklets | `tasklet_setup()`/`tasklet_cb` signature (no `unsigned long data`) | `core/rtw_trx_pci.c`, `phl/*` |
| MIPS/PCI host | no driver change expected; `pci_read_config_*`, `dma_*` are stable | `os_dep/linux/pci_intf.c` |
| build flags | the `-Wno-error=…` list will grow: 6.18 gcc is stricter about implicit fallthrough, designated-initialiser ranges, and `-Warray-bounds` on the OEM's container-of abuse | `Build/Compile` |

Order of work for whoever runs CI (cheapest signal first):
1. `make package/kernel/rtl8832br-rx2lpro/compile V=s -j1` with the 6.6 series
   applied unchanged → collect the first ~10 compile errors; they will cluster
   in the table above.
2. Add `110-linux618-compat.patch` on top; **never** edit `100-*` in place
   (it is verified to apply fuzz-free to pristine GPL, which is our only
   reproducibility anchor).
3. Only after it builds: DT + RAM-boot.

## 4. Failure modes and how to tell them apart on a RAM-boot

`8832-bringup-diag` collects all of the below in one shot. Verdict table:

| # | mode | how it presents | discriminating commands | fix owner |
|---|---|---|---|---|
| A | **BAR / outbound window** | immediate `Data bus error` / `Bus error` oops in `os_pci_read8`, address in the `0xb90000??` KSEG1 alias of `0x190000??`, **before** any FW message, inside `mac_ax_ops_init` | `hexdump -C -s 0x10 -n 24 /sys/bus/pci/devices/0000:00:00.0/config` → BAR2 raw must read `00 00 00 19`(+64-bit type bits, e.g. OEM value `0x19000004`), and `cat .../resource` line 3 must start `0x19000000 … 0x190fffff [mem]`. If BAR2 is `00 00 00 00`, the `ranges` bus-address cell is 0 while CPU is `0x19000000` — that is *precisely* the toby bug (`3dfa5900`). Also `grep 19000000 /proc/iomem`. | our board DTS + PCIe host agent |
| B | **IRQ / MSI** | probe passes BAR check, then `request_irq(%u) failed: %d` or the preflight refuses with `invalid PCI INTx routing: pin=%u irq=%u`; no oops, clean `-ENXIO` | `cat /sys/bus/pci/devices/0000:00:00.0/irq` (stock reference value: a Linux irq mapped to **MIPS line 5**); `grep -E 'rtk_wifi6|MIPS' /proc/interrupts`; `hexdump -C -s 0x3c -n 4 .../config` (line 0x3c, pin 0x3d — pin must be 1..4); `ls /sys/bus/pci/devices/*/msi_bus` (this RC has **no MSI**, so any `pci_alloc_irq_vectors(PCI_PORT_IRQ…MSI)` attempt must fall back to INTx) | DT `interrupts = <21 4>` on `&pcie0`, intc-map agent |
| C | **timer softirq** | FW download completes (`Load FW RAM code OK`, `rtw_hal_download_fw`), probe returns, and either the box wedges or an oops trace names `run_timer_softirq`/`call_timer_fn`/`timer` — this is where the 6.6 attempt stopped after A was fixed | `dmesg | grep -Ei 'Load FW|dl_fw|run_timer_softirq|call_timer_fn|scheduling while atomic'`; `head -40 /proc/timer_list` while wedged; `cat /proc/softirqs`; `ls /sys/class/ieee80211` (empty = never registered). Distinguishing B from C: B never reaches a FW download message, C always does. Distinguishing A from C: A dies synchronously on first MAC read, C dies *after* probe returns. | driver compat (this track), needs hardware |

toby's exact statement (`LINUX66-DRIVER.md`, revision 4): autoload was
switched off because "the boot log after the BAR correction failed at IRQ
allocation and then in the timer softirq", the error-unwind path is **not**
fully repaired, the vendor timer code contains partly-initialised timers, and
they advise at most **one** manual probe attempt over UART, never repeated
load/unload. All of that is inherited risk for us.

## 5. Platform prerequisites this track does NOT own

1. **`CONFIG_PCI` must be enabled and `pcie0` must come up on base A.**
   `/tmp/rebase/openwrt-rx23302/openwrt/target/linux/rtl819x/rtl8197f/config-6.18`
   has `# CONFIG_PCI is not set` (only `CONFIG_PCI_DRIVERS_LEGACY=y`,
   `CONFIG_PCI_HCI=y` for the HAL), and
   `dts/rtl8197f_electra_rx23302.dts:364-369` leaves `&pcie0` **disabled**
   with the note that the in-SoC radio attaches by direct address
   (`TYPE_EMBEDDED`, `8192cd.h:4824`) and that *enabling the PCI bus hung
   their boot*; "needed only for a 5 GHz card" — i.e. for us. That hang must
   be explained before any 8832 experiment; it is the single biggest risk in
   this track and it is a PCIe-host/DTS problem, not a driver problem.
   Their host driver is `arch/mips/pci/pci-realtek.c` ("realtek,rtl8196b-pci"),
   which builds hose resources from `range.cpu_addr` — that happens to give
   identity-correct BARs, unlike the DT-driven path that bit toby.
2. **Board DTS** for RX2L Pro must carry (from `routes.md`/`stock-evidence.md`):
   `interrupt-parent = <&intc>; interrupts = <21 4>;` and identity ranges
   `<0x01000000 0 0x18c00000 0x00c00000 0 0x00200000>,
    <0x02000000 0 0x19000000 0x01000000 0 0x01000000>;`
   plus the `wifi@0,0 { compatible = "pci10ec,b832"; reg = <0 0 0 0 0>; }`
   child (our ID, **not** the AC23's `b814`).
3. **Wireless bring-up design.** Whatever we ship must not rely on the stock
   `mac80211.sh` path for this driver (it hangs — `mr1500x-wifi` header
   comment), so a validated 5 GHz needs its own service, exactly as the mr1500x
   port does; that is a userspace decision for the orchestrator, and until it
   is taken, the radio must stay out of `/etc/config/wireless` (README risk 8).

## 6. Reference bring-up sequence for the working (4.4) case

From `/tmp/rebase/mr1500x-openwrt/files/etc/init.d/mr1500x-wifi` — same chip,
same SoC, proven 5 GHz AP on the vendor kernel. Useful when we get a wiphy and
need to know whether a failure is "our port" or "the driver":

* `wlan0` = 5 GHz `rtk_wifi6`/phy1, `wlan1` = 2.4 GHz `rtl8192cd`/phy0.
* 5 GHz rules: **never** pre-`up` the root interface (tuning with no channel
  plan spins the LCK loop and a later AP start hangs the box); write the
  country code into the driver via
  `echo CC >/proc/net/rtk_wifi6/wlan0/country_code` (hostapd's `country_code`
  alone does not set the chplan); set the MAC while down; let hostapd open it.
* 2.4 GHz rules: root iface must be opened before any VAP and
  `iwpriv wlan1 set_mib func_off=0`, else all data frames are dropped.
* RF tables are mandatory for this eFEM board; without them the synthesizer
  never locks.
