# The 6.6 port, preserved

Provenance: `github.com/tobyyw121/Openwrt_RTL`, path
`package/kernel/rtl8832br-wr1500/`, recovered from a local sparse checkout of
that tree (`/tmp/rebase-base`, HEAD `11ef41fd`) because the repository now
answers 404 -- private or removed. GPL-2.0-only, as the package itself declares.

Why it is copied here rather than linked: it is the only known port of Realtek's
G6/PHL Wi-Fi 6 driver to a modern kernel, and it was living in `/tmp`. Losing it
would mean redoing ~2,500 lines of kernel-API compat work before the 6.18 port
could even start.

## What is here

| file | what it is |
|---|---|
| `Makefile` | the package as built for kernel 6.6.114 + mac80211 backports 6.12.96. Carries the real provenance: `PKG_SOURCE=GPL_ax1500v3.tar.gz` from TP-Link's GPL drop, `PKG_HASH=6d133598b134b6878ce802a53b3061d9f2efb3636ad0d31ec4cb3bcbdf438f2a` |
| `patches/100-linux66-openwrt-wr1500.patch` | 74 KB / ~2.5k lines of compat work: the vendor tree built as an OpenWrt package on 6.6 |
| `patches/101-use-private-station-header.patch` | station header handling |
| `patches/102-check-wr1500-mmio-window.patch` | BAR2/MMIO window guard (OEM BAR2 answers at `0x19000004`, needs `>= 0x100000` bytes) |
| `patches/103-wr1500-pci-irq-preflight.patch` | INTx preflight; this SoC's root complex has one slot and no MSI |

NOT copied: `files/RFE50/**` (the vendor RF tables). Our board's own copies are
in `/etc_ro/wlan/rtl8832bre/RFE50/` on the device, `port/06/stock-evidence.md`
§5 records which of them are byte-identical to this port's, and
`../files/rfe50-manifest.sha256` pins ours.

## How to use it

The 6.6 tree is the *starting point*, not a drop-in: it was built against
backports 6.12.96, and 6.18 moved several APIs it touches. Diff `100-*.patch`
against what 6.18 needs before writing anything new -- the file list it edits is
enumerated in `../README.md` §2, and that list is where the work will be.

Both prerequisites are still unmet on our side and are tracked there: PCI must
be enabled and boot-validated first (see `../../REPORT.md`, driver inventory).
