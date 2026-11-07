# Route verdict — 5 GHz RTL8832BR on RTL8197F, kernel 6.18

Three routes were evaluated. Everything below is either **measured** (with the
path/offset it came from) or explicitly marked **unknown**. `stock-evidence.md`
carries the raw evidence; `skd-attempt/README.md` and `rtw89-gap.md` the detail.

Settled first, because it changes the shape of every route:

| Fact | Value | Where it came from |
|---|---|---|
| PCI ID of our 5 GHz client | **`10ec:b832`** | `pci_device_id` table in our own stock kernel at `/tmp/work3/kernel.lzma.out` **file offset `0xb7787c`** (`ec 10 00 00 / 32 b8 00 00`, driver_data 5); the G6 SDK source labels it `{PCI_DEVICE(0x10ec,0xb832), .driver_data = RTL8832BR}` (seen as a removed line in `/tmp/rebase-base` commit `830c08cf`, patch `100-linux66-openwrt-wr1500.patch` line ~1398). Independently measured live on Cudy WR1500 R76: `/tmp/rebase-base` commit `33575aad` "record live B832 PCI identity". |
| Interrupt | **MIPS HWINT 5**, INTx, **no MSI** | stock `/proc/interrupts` captured in `/tmp/rx_dump/logs/syslog/sys_info.log:501` → `5: 968696 MIPS 5 rtk_wifi6`; toby binding doc `realtek,rtl8197f-pcie.yaml`: "one physical slot and no MSI support in the vendor BSP". |
| MAC registers | **BAR2, MMIO, must be programmed to `0x19000000` (identity)** | `/tmp/rebase-base` commit `3dfa5900` + patch 102 ("OEM BAR2 is 0x19000004", `>= 0x100000` bytes). |
| Firmware | **embedded in the driver, no `.bin` anywhere** | stock rootfs has **no `/lib/firmware`** at all (`/tmp/rx_dump/rootfs/x`); toby builds with `CONFIG_FILE_FWIMG=n` and states FW comes from the embedded G6 array. |
| RF/PHY params | **file-loaded, mandatory** (`rfe_type >= 50`, eFEM) | `/etc_ro/wlan/rtl8832bre/RFE50/**` (40 files) + `/etc_ro/init.d/rcS:69` `iwpriv wlan0 flash_set para_path=/etc/wlan/`; same conclusion in `/tmp/rebase/mr1500x-openwrt/files/etc/init.d/mr1500x-wifi` header comment. |
| Mainline `rtw89` | **no 8832 at all** | upstream dir listing (fetched 2026-11-07) has only `rtw8851b/be/bu`, `rtw8852a/ae/au`, `rtw8852b/be/bt/bte/bu`, `rtw8852c/ce/cu`, `rtw8922a/ae/au`, `rtw8922d/de`; `linux-firmware/rtw89/` has **no** 8832 file. |

## (a) Vendor Wi-Fi 6 SKD (`rtk_wifi6`, G6) rebased to 6.18 — **RECOMMENDED**

**Known:** the exact same tree (TP-Link `GPL_ax1500v3.tar.gz`, subdir
`rtl8198/linux-4.4.x/drivers/net/wireless/realtek/g6_wifi_driver`) **already
compiles to `rtk_wifi6.ko` for mipsel on Linux 6.6.114 + backports 6.12.96 with
modpost clean**, in `/tmp/rebase-base` `package/kernel/rtl8832br-wr1500`
(revision 4, three patches: 100 = 2563 lines of compat, 102 = BAR guard,
103 = INTx preflight). 12 of our 16 active RFE50 files are **byte-identical**
to that package's copies (`stock-evidence.md` §5) — same PHY/IP generation.
Everything needed on the userspace side is documented and *proven working on
hardware* by `/tmp/rebase/mr1500x-openwrt` (Mercusys MR1500X, same
RTL8197F + RTL8832BR + RFE50, shipped 5 GHz AP + WPA3 + FT — but on the
**vendor 4.4 kernel**).

**Unknown / the actual work:** (1) the 6.6→6.18 compat delta (unbounded until a
CI build runs; predicted list in `skd-attempt/README.md`); (2) **whether the
driver ever completes init on 6.6/6.18 at all** — toby stopped after BAR fix at
"IRQ allocation, then timer softirq" and removed autoload; (3) base A
(`openwrt-rx23302`, 6.18) has `# CONFIG_PCI is not set` and `pcie0` left
**disabled**, with the note that enabling it "hung the boot" — that PCIe-host
problem is upstream of us and unsolved.

**Cost/risk:** ~2–4 build iterations of compat work by whoever runs CI, plus
one genuinely open hardware question (timer/IRQ). Highest chance of a real
AX1800 radio. Needs `CONFIG_PCI` + PCIe host up on base A first.

## (b) mainline `rtw89` + a new 8832 chip entry — **NOT VIABLE FOR v1**

**Known:** exactly what a new chip costs (`rtw89-gap.md`): a `rtw8832.c`
carrying a ~250-field `rtw89_chip_info` (register address sets, DLE/HFC tables,
efuse sizes, RF-k routines, coex tables), a `rtw8832e.c` bus glue with the
`10ec:b832` id, a Kconfig/Makefile entry — **and a firmware blob that does not
exist upstream**. rtw89 would not attach to our chip today: its tables are
`0xb852/0xb85b/0x8852/0xa85a/0xb851/0x885c/…`; `0xb832` appears in none of them.

**Unknown:** the whole 8832 register/PHY/coex description. Realtek has never
published it, and the reverse-engineering source would be the vendor HAL
(`hal*_8832br`, `mac_ax_*`) — i.e. you would be transcribing the SKD into
rtw89's structures, which is *more* work than route (a), not less. Plus the FW:
only obtainable as the array inside a vendor GPL drop, i.e. the same
redistribution grey zone OpenWrt's `linux-firmware` rule would reject.

**Cost/risk:** months; realistically a new-chip upstream submission. Not a
path to this port's v1.

## (c) Ship v1 without 5 GHz — **MANDATORY BASELINE, not a route**

Cost ≈ 0, risk ≈ 0, and it is already the shipped behaviour: `README.md` risk
row 8 documents 5 GHz as deliberately absent (inherited `kmod-rtw88` /
`rtl8822be-firmware` were removed), because netifd fails the *whole* wireless
bring-up if a radio is seeded with no driver. So (c) is the fallback that must
stay intact while (a) is attempted out-of-tree and autoload-disabled.

## Verdict

Ship (c) for first boot. Build (a) as a **staged, non-autoloaded kernel
package** (`skd-attempt/`), gated behind a working PCIe host on base A. Keep (b)
as documentation-only: if (a) succeeds, the rtw89 gap analysis becomes the
upstreaming spec; if (a) fails on the timer/IRQ path, (b) is not a rescue, it is
a different multi-month project.
