# Route decision — 2.4 GHz (RTL8197F integrated WMAC) on kernel 6.18

**Adopted: base C's packaging and driver — iptime A2004MU's
`package/kernel/rtl8192cd` (RTL8197F, cfg80211, Linux 6.18, hardware-tested)**
with two things bolted on from base A: base A's `rtlmib` utility (there is no
`iwpriv` in current OpenWrt) and base A's apmib-reading discipline. Base B is
used only as a calibration-format reference. Base A's *driver* is rejected.

Then, in one line: **cfg80211 build, out-of-tree package, userspace
calibration from the Tenda text apmib, standard `mac80211` netifd handler, no
custom wifi handler, no LuCI patches.**

---

## 1. The three candidates, as measured

### A — `/tmp/rebase/openwrt-rx23302` (PRIMARY BASE, kernel 6.18.41)

Driver: `target/linux/rtl819x/files-6.18/drivers/net/wireless/rtl8192fe/`,
"RTK-8192FE-wlan-driver 3.8.0", 1608 files, spliced **into the kernel** by
`target/linux/rtl819x/patches-6.18/737-wlan-rtl8192fe.patch` (22 lines: adds
`obj-$(CONFIG_RTL8192FE_RX23302) += rtl8192fe/` and
`source .../Kconfig.rx23302`). Config lives in `rx23302.mk`; the `-D` flags are
passed through `ccflags-y` in the driver's own `Makefile`.

Already fixed for 6.x — and this is the best-documented compat layer of the
three, all of it in five files:

| breakage | where it is fixed | citation |
|---|---|---|
| `/proc` needs `struct proc_ops` (5.6+) | `8192cd_proc.c:83-84`, `:97`, `:112` — all three `RTK_DECLARE_*_PROC_FOPS` macros now emit `const struct proc_ops` with `.proc_open/.proc_read/.proc_write/.proc_lseek/.proc_release` | verified in file |
| `PDE_DATA` renamed to `pde_data` (5.17+) | `compat_kernel.h:31-33` `#ifndef PDE_DATA / #define PDE_DATA(inode) pde_data(inode)` | verified in file |
| `netdev->dev_addr` is const (5.17+); vendor `memcpy` produced "Incorrect netdev->dev_addr" on every rmmod | `compat_kernel.h:107-117` `rtl_dev_addr_set()` → `eth_hw_addr_set()` | verified in file |
| IPX support and `<net/ipx.h>` deleted (5.15+) — the bridge-ext code still parses IPX frames | `compat_kernel.h:36-61` re-declares `struct ipxhdr`/`ipx_address` verbatim | verified in file |
| vendor `hmac_sha1` collides with in-tree `lib/crypto/sha1.c` when built **into** the kernel ("multiple definition") | `compat_kernel.h:15-25` `#define hmac_sha1 rtl_hmac_sha1` | verified in file |
| `WLAN_CATEGORY_*` action-frame codes used to come from `linux/ieee80211.h`, which is not there without cfg80211 | `compat_kernel.h:76-87` | verified in file |
| DMA cache ops no longer exported to modules; RTL819x BSP glue gone | `Makefile:1053-1068`: `-include compat_dma.h`, `compat_timer.h`, `compat_kernel.h`, `compat_legacy_timer.h`, plus `compat_bsp_glue.o` and `compat_dma_dev.o` objects | verified in file |
| `802.11 skb->__unused` guest marking doesn't exist on a stock kernel | `compat_kernel.h:89-95` no-op macro | verified in file |
| warning-as-error policy of the vendor tree | `Makefile:1046-1052`, `-Wno-error=…` list + `-Wno-error` | verified in file |

One residual bug in A, worth knowing because we would have inherited it:
`8192cd_osdep.c:19397` still declares `struct file_operations
wlan_custom_Passthru_proc_fops` and hands it to `proc_create_data()`. That is
only harmless because `rx23302.mk:47` defines `-DRX23302_NO_PASSTHRU`, which
compiles the whole feature out (`8192cd_cfg.h:276`). Copy that tree without the
define and the `/proc/custom_Passthru_wlan` entry dispatches the wrong
callbacks on 6.18.

**Why we still reject A's driver.** It is the **8197G** HAL, not 8197F:
`rx23302.mk:13` `CONFIG_WLAN_HAL_8197G := y` and the `-D` line adds
`-DCONFIG_WLAN_HAL_8197G -DCONFIG_SOC_RFE_TYPE_0`. It also drags in the
8812FE 5 GHz HAL (`-DCONFIG_WLAN_HAL_8812FE -DCONFIG_SLOT_0_8812FE`) for a card
we do not have, and it is **cfg80211-free** (`Kconfig.rx23302` header: "cfg80211
у драйвера нет (свой стек 802.11), nl80211-пути нет"), which is what forces its
527-line custom netifd handler, seven iwinfo patches, a LuCI patch, and a
hand-written uci-defaults seeder. Its own target even ships
`CONFIG_WLAN_HAL_8197F=y` in `target/linux/rtl819x/rtl8197f/config-6.18:241`
while `rx23302.mk` compiles the 8197**G** HAL — the two disagree, and that
subtarget config is not what selects the HAL (the `.mk` is), so the tree is
self-inconsistent on exactly the axis we care about.

Also note: building it **into** the kernel (`CONFIG_RTL8192FE_RX23302=y`,
`-DRX23302_BUILTIN`) is what created the `hmac_sha1` clash in the first place.
Out-of-tree, that class of problem does not exist.

### B — `/tmp/rebase-base` (tobyw121/Openwrt_RTL, kernel 6.6.114)

`package/kernel/rtl8192cd-rtl8197f`, an out-of-tree cfg80211 KernelPackage with
**board variants**, and two of them are Tenda RTL8197F boxes:

```
mw5     | RTL8197FS@0xb8640000,irq6,wlan1,RFE5 | RTL8812BRH 10ec:b822,wlan0,RFE6
ac23    | RTL8197FH/VG integrated,wlan1        | RTL8814BR 10ec:b814 (expected)
rd05    | RTL8197FH/VG,wlan1                   | RTL8812FR 10ec:f812
wrd12gn | RTL8197FH 97FN/PKG2,wlan1,RFE0       | RTL8812BRH 10ec:b822,wlan0,RFE0
```
(`files/etc/rtl8197f-wlan-profiles.conf`)

That MW5 row is the closest sibling to our board on the planet: RTL8197F
integrated 2.4 GHz + RTL8812BRH-class PCIe 5 GHz, and it says the integrated
radio lands on **wlan1**, and that the driver does the *external* radio too
(8822B HAL path). Useful for three things: the RFE-type evidence, the
band↔interface naming evidence, and `oem-calibration-schema.json` (its
"provenance"/"ac23" section is what documents *structurally* the packed APMIB
layout — see calibration.md §3). B already fixed `proc_ops`
(`8192cd_proc.c:86-115`, version-gated on `>= 5.6`) and correctly guards its
own `PDE_DATA` definition (`8192cd_proc.c:71` `#if LINUX_VERSION_CODE <
KERNEL_VERSION(3,10,0)`) — but nothing in B provides `PDE_DATA` on ≥5.17, so
its own callers (`:5635`, `:9087`) would not compile against 6.18 as-is.

**Rejected:** wrong kernel (6.6, and the target is `realtek`/DSA with rtl8_4
tags, not our swconfig/PHYLINK world), and every Tenda variant in it is
self-labelled as *needing hardware validation*, including "PCI ID, RFE,
**calibration** and cfg80211 board operation need hardware validation" for
`ac23`. We are not going to rebase our port onto a 6.6 tree whose WLAN story is
unvalidated, when a 6.18 validated one exists.

### C — `/tmp/rebase/iptime-a2004mu-openwrt` — **ADOPTED**

`package/kernel/rtl8192cd/src`, 299 files, out-of-tree OpenWrt
`KernelPackage`, `DEPENDS:=@TARGET_rtl819x_rtl8197f +kmod-cfg80211`, built with
the mac80211 **backport** headers, and configured for our exact chip:

```
CONFIG_RTL_8197F=y  CONFIG_SOC_WIFI=y  CONFIG_WLAN_HAL_8197F=y
CONFIG_BAND_2G_ON_WLAN0=y  CONFIG_SOC_RFE_TYPE_1=y CONFIG_SOC_EXT_PA=y
CONFIG_SOC_EXT_LNA=y  CONFIG_OPENWRT_SDK=y  CONFIG_CFG80211_MODULE=y
```
(from `package/kernel/rtl8192cd/Makefile` + `src/Makefile:11-27`)

Why this one:

1. **Same SoC, same radio, same kernel series, and it is on hardware.**
   `STATUS.md` "Passed on hardware": *"RTL8197F integrated-radio initialization,
   NVMEM calibration, and 2.4 GHz AP; simultaneous 2.4 GHz and 5 GHz AP
   operation; stable 2.4 GHz MAC after two Wi-Fi reloads and a normal reboot"*,
   on `kernel: 6.18.36`, `target: rtl819x/rtl8197f`. That last clause matters:
   a MAC that is stable across reloads means their calibration/MAC path is not
   re-randomising, which is the failure mode we would have hit first.
2. **It is cfg80211.** `wiphy_new()` at `src/8192cd_cfg80211.c:8111`. So
   `wifi detect` enumerates the radio, `iw`/`iwinfo`/LuCI work unpatched, and
   the first-boot risk that our own `seed.config` calls out — *"a seeded 5 GHz
   wifi-device with no driver makes netifd fail the whole wireless bring-up,
   2.4 GHz included"*, and the inverse: an *un*enumerable 2.4 GHz radio boots
   with no wireless section at all — disappears.
3. **The two 6.x killers are fixed, in the vendor's own version ladder.**
   `8192cd_proc.c:107-135` gates the `RTK_DECLARE_*_PROC_OPS` macros on
   `__LINUX_5_6__` and emits `struct proc_ops` (with the `file_operations`
   variant kept only under `#else`), and `__LINUX_5_6__`/`__LINUX_3_10__` are
   derived from `LINUX_VERSION_CODE` in `src/osdep_service_linux.h:257` /
   `:145`. `8192cd_osdep.c:18519-18529` does the same for the passthru proc
   entry. The dead `< 3.10.0` `PDE_DATA()` definition at `8192cd_proc.c:88` is
   correctly compiled out; `PDE_DATA()` at the use sites then resolves through
   the backport headers. **That last link is the one thing I could not prove
   from files on disk** — see REPORT.md blocker B1; it has a one-line fallback.
4. **Small and scoped.** 299 files vs our 1117; the 8197F PHY/MAC/RF tables are
   already extracted into `data_*_8197F.c` (including
   `data_rtl8197Ffw.c`, the 56080-byte firmware) and nothing else is compiled.

---

## 2. License / provenance

| candidate | provenance as documented by its own project | verdict for us |
|---|---|---|
| C (iptime) | `docs/provenance-and-licensing.md`: "based on the RTL8192CD driver in **Cudy's official `GPL_for_GP3000.tar.gz` GPL download**, published 2025-06-16. The archive contains the Linux kernel COPYING, the RTL8192CD source tree, and the RTL8197F firmware and radio-configuration files." Plus a real delta audit: 278 paths common, **253 byte-identical**, 25 differing (the 6.18 port), local-only files are compat headers/COPYING/generated tables, and `data_rtl8197Ffw.c`'s payload SHA-256 `61d6ef42…509515` == the archive's `rtl8197Ffw.bin`. Package metadata declares `GPL-2.0-only ISC RSA-MD SSLeay-standalone LicenseRef-Brian-Gladman-1999` with per-file `PKG_LICENSE_FILES`. It also explicitly refuses to ship the Realtek SDK ("RTL8367 switch API files explicitly identify themselves as proprietary… the complete SDK must not be treated as uniformly GPL-licensed and is not shipped here"). | **clean, and the only one with a hash-level claim we can re-verify.** Same licence set carried into our Makefile verbatim. |
| A (rx23302) | `docs/sources.md:61`: driver from **TP-Link Archer AX12 v1** GPL archive `GPL_AX12v1` (obtained *via* tobyw121/Openwrt_RTL), "865 files with a GPL header, 174 mbed TLS (Apache-2.0, ARM), **407 without a header** (generated tables, HAL)". | usable, but a second-hand chain and 407 header-less files to argue about. We only take `rtlmib` from it, which is 62 lines of their own GPL-2.0-only code. |
| B (toby) | `PKG_LICENSE:=GPL-2.0-only`, no published upstream archive named for `rtl8192cd-rtl8197f`; the repo states the full SDK is not shipped. | reference only. |

Our own repo's `PROVENANCE.md` already records a *third* chain for the driver
we are replacing: "the RTL8192CD vendor driver … [from]
ADCDS/openwrt-dlink-dir842-r1 … GPLv2; the vendored rtl8192cd driver carries
its own Realtek header." That is a DIR-842 R1 (RTL8197D-era) drop, i.e. neither
this chip nor this kernel. Moving to C is also a provenance improvement: a
named vendor GPL release, an exact firmware hash, and a per-file licence list.

**No vendor firmware, decrypted image, MAC, serial, WPS PIN or password goes
into the repo from any of this** (FACTS §0). The one thing C ships that is
binary-ish is `data_rtl8197Ffw.c`; it is GPL-published by Cudy and
hash-identified, and it is Realtek's own 8197F firmware, not Tenda's.

---

## 3. Build risk, stated as the difference between the trees

Our 4.14 copy (`files/target/linux/realtek/files-4.14/drivers/net/wireless/rtl8192cd/`,
**1117 files, 792 635 lines** of `.c`/`.h`) on 6.18:

* `8192cd_proc.c:66-71` — `#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
  void *PDE_DATA(const struct inode *inode) { return PDE(inode)->data; }`.
  Dead on 6.18, but `:77`, `:90`, `:101`, `:105`, `:5036`, `:7736`, `:7896`,
  `:7963` all *call* `PDE_DATA()`, which the kernel removed in 5.17. Nothing in
  the 4.14 tree defines it otherwise ⇒ implicit-declaration / link error.
  **Fixed in A** (`compat_kernel.h:31-33`), **fixed in C** (via backport
  headers — see B1), **not fixed in B**.
* `8192cd_proc.c:73-82` — `RTK_DECLARE_READ_PROC_FOPS` emits
  `struct file_operations read_proc##_fops`, and the `RTK_CREATE_PROC_*` macros
  hand those pointers to `proc_create_data()`. On ≥5.6 that is not a type error
  at all, it is a **runtime** mis-dispatch: `proc_ops` and `file_operations`
  have different member layouts, so `/proc/wlan*/…` entries call the wrong
  offsets. B's comment says this out: "Passing the old layout to
  proc_create_data() can compile with a warning but makes Linux 6.6 dispatch
  the wrong callback at runtime." **Fixed in A** (`8192cd_proc.c:84`),
  **fixed in C** (`:111/:120/:127`), **not in our tree**.
* `8192cd_osdep.c:14662` + `:14697` — the same bug for
  `custom_Passthru_wlan`, with no `RX23302_NO_PASSTHRU`-style guard in our
  config. **Fixed in C** (`8192cd_osdep.c:18519-18529`), version-gated.
* Unversioned but equally fatal: `dev->dev_addr` written with `memcpy` (const
  since 5.17), `struct ipxhdr` (deleted 5.15), `hmac_sha1` name clash,
  DMA-cache symbols not exported to modules, `timer_setup`/`from_timer` for the
  vendor's old `unsigned long data` timer callbacks, and `EXTRA_CFLAGS` (ignored
  by 6.x kbuild; A had to add `ccflags-y += $(EXTRA_CFLAGS)` at
  `Makefile:1044`). None of that is fixed in our 4.14 copy, and all of it is
  fixed in A and C.

**Residual risk in C, honestly:** C is 25 files changed against the Cudy
archive, iptime tested *their* board, and they build against
`mac80211-backport`. We change the config (RFE type, no 8812FE, our own
`-D` set), we point it at a different DTS, and we drop the packed-nvmem
calibration in favour of a userspace converter. Those are four deltas, and only
the first CI build resolves them. That is why `seed.config`/README risk #7 must
stay "open until CI is green" rather than being declared fixed here.

**Size/risk ranking, our view:**

| | A | B | **C (adopted)** | ours (4.14) |
|---|---|---|---|---|
| chip HAL | 8197**G** ✗ | 8197F ✓ | **8197F ✓** | 8197F ✓ |
| kernel | 6.18 ✓ | 6.6 ✗ | **6.18 ✓** | 4.14 ✗ |
| cfg80211/nl80211 | ✗ | ✓ | **✓** | partial |
| 6.x proc/PDE_DATA | **fixed** | partial | **fixed** | broken |
| packaging | in-tree, needs target patch | out-of-tree | **out-of-tree** | in-tree |
| files compiled for our board | 1608 | 2578 | **299** | 1117 |
| hardware evidence on this SoC+kernel | yes (RX23302) | no | **yes (A2004MU)** | 4.14 only |
| licence chain | TP-Link via toby, 407 unheadered | unstated | **Cudy GPL, hash-verified** | D-Link reference |

---

## 4. What we take from A anyway, and why that is not contradictory

`package/utils/rtlmib/{Makefile,src/rtlmib.c}` — 62 lines, GPL-2.0-only, their
own code. We need it because `iwpriv` does not exist on current OpenWrt and the
RF calibration values have no nl80211 attribute; they only reach the driver via
`SIOCDEVPRIVATE+1` (`8192cd_ioctl.c:75` `RTL8192CD_IOCTL_SET_MIB`), which
`ndo_do_ioctl = rtl8192cd_ioctl` still serves in C's cfg80211 build
(`8192cd_osdep.c:13694` and three siblings).

A's `lib/netifd/wireless/rtl8192cd.sh` (527 lines) and `lib/wifi/rtl8192cd.sh`
are **not** adopted, but they are the documented plan B (uci-wireless.txt §4)
and the source of several measured facts we do rely on — e.g. that `set_mib
band=` on a non-root VAP Oopses in `init_beacon()` (`8192cd_ioctl.c:4175`), and
that radio-scoped MIBs must only be written to the root interface.
