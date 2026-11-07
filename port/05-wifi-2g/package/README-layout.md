# Staged package set — 2.4 GHz (RTL8197F integrated WMAC)

Paths below are **repo-relative**; `build.sh` overlays `files/` onto the pinned
base tree (`cp -a files/. .`), so anything under `files/` here is already in
its final position. Nothing in this directory is live in the build until the
orchestrator copies it up.

| staged path | lands at | what it is |
|---|---|---|
| `files/package/kernel/rtl8192cd/Makefile` | `package/kernel/rtl8192cd/Makefile` | the out-of-tree kmod for the vendor HAL on 6.18. Replaces `target/linux/realtek/modules.mk`'s `KernelPackage/rtl8192cd` **and** the 1117-file `files-4.14/drivers/net/wireless/rtl8192cd/` tree. |
| `files/package/utils/rtlmib/{Makefile,src/rtlmib.c}` | `package/utils/rtlmib/` | 62-line `SIOCDEVPRIVATE` client: the `iwpriv set_mib` replacement, because wireless-tools is gone from current OpenWrt. Taken from base A (rx23302), GPL-2.0-only. |
| `files/target/linux/realtek/base-files/usr/sbin/rfhw-convert` | `…/usr/sbin/rfhw-convert` | the Tenda apmib → `set_mib` calibration converter (replaces stock `/bin/rfhw_convert` + `/tmp/rfhw.sh`). |
| `files/target/linux/realtek/base-files/etc/hotplug.d/net/99-rfhw-rx2lpro` | `…/etc/hotplug.d/net/` | applies the above as soon as the rtl8192cd netdev is registered, i.e. while it is still down. |
| `files/target/linux/realtek/base-files/etc/uci-defaults/12_wireless-2g-rx2lpro` | same path in `files/` | rewritten first-boot seeder. **Overwrites** the existing 4.14 file of the same name. |
| `dts/RX2LPRO-wifi2g.dtsi.fragment` | merge into `files/target/linux/realtek/dts/RX2LPRO.dts` | `wifi@18640000` node + commented nvmem cell. Not on the boot-critical path (see the file's own header). |

## Not staged, and why

* **The driver source itself.** `PKG_SOURCE`/`PKG_HASH` in the kmod Makefile
  are placeholders on purpose. The bytes must come from a repacked tarball of
  the GPL release plus its porting delta, and the SHA-256 has to be recorded by
  whoever runs the fetch, not asserted here from memory. `PROVENANCE.md` needs
  the matching row before this ships.
* **`/lib/netifd/wireless/rtl8192cd.sh`** (base A's 527-line custom handler).
  We do not adopt it: our chosen build has cfg80211 compiled in, so stock
  `mac80211.sh` + hostapd drive the radio. It is plan B in
  `uci-wireless.txt` — the file is long enough that it should be pulled from
  base A directly rather than transcribed here.
* **LuCI/iwinfo patches** (base A ships seven iwinfo patches and one
  luci-mod-network patch, all for the WEXT path). Same reason: not needed on
  the nl80211 route. Do not carry them over "for compatibility" — they change
  LuCI's behaviour for *all* radios.
* **`efuse_97f/`, `phydm/`, `WlanHAL/`** etc. ship inside the source tarball;
  they are not separately staged.
