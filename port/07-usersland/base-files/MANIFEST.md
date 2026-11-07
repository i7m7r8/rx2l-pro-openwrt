# base-files/ — staged overlay inventory

This directory is the proposed replacement for
`files/target/linux/realtek/base-files/` after the rebase to base A
(`target/linux/rtl819x`, kernel 6.18). Paths are relative to this directory and
map 1:1 onto the OpenWrt tree once the overlay is applied with `cp -a files/. .`
(build.sh:43).

Nothing here is executable on the device yet: no image in this repo has ever
booted on hardware.

## STAGED, NEW OR REWRITTEN (this track authored them)

| path | replaces | why it changed |
|---|---|---|
| `etc/board.d/02_network` | same path | runtime switch-model detection (dsa / swconfig / none) instead of one baked layout; `mtd_get_mac_ascii CFG TENDA.lan_mac` replaces the hand grep; radio MAC deltas corrected (+0x14 / +0x04, see §Corrections); `tenda,rx2l-pro` and `rx2l-pro` both accepted |
| `lib/realtek.sh` | same path | `realtek_board_detect()` is dead weight on the new base (no reference tree ships a `*_board_detect()` any more); replaced by `rx2lpro_board_name` / `rx2lpro_is_board` / `rx2lpro_cfg_get` / `rx2lpro_netmodel` |
| `lib/upgrade/platform.sh` | same path | full cr6c geometry validation before the write (magic, start_addr, burn_addr, payload len, squashfs at 16+len, size vs partition); see `../sysupgrade.md` |
| `etc/uci-defaults/05_enable_flow_offloading` | new | firewall4 flow_offloading=1, hw=0 by default; enables the three init.d scripts (base-files does not auto-enable anything) |
| `etc/uci-defaults/06_switch_vlan_ids` | new | swconfig model only: VLAN 1/2 → 9/8, br-lan ports eth0.1 → eth0.9, wan.device → eth0.8. Required by `RTL_NASH_VID=9 / RTL_WAN_VID=8 / RTL_LAN_PORTY=0x11F` in base A's `rtl819x_eth.c:54,55,899` |
| `etc/uci-defaults/07_wireless-2g-rx2lpro` | `12_wireless-2g-rx2lpro` | policy only (MAC by runtime band, HT20 migration, `/etc/Wireless → /tmp`); stanza creation moved to `lib/wifi/`; fails on purpose if the radio is not enumerated yet so the file is retried next boot |
| `lib/wifi/rtl8192cd.sh` | new | the enumeration hook master still honours: `/sbin/wifi` does `include /lib/wifi` then calls `detect_rtl8192cd` (wifi-scripts `files/sbin/wifi:64-81`). `wifi detect` now exits 1 |
| `etc/init.d/rtl819x-hwnat` | new (from base A) | START=17, manages the nft flowtable snippet against `flow_offloading_hw` |
| `etc/init.d/rtl819x-hwnat-gate` | new (from base A) | START=95, arms `/proc/rtl819x_router` + `/sys/module/rtl819x/parameters/hwnat`; AP-role guard; mode change applies by reboot (live toggle wedged base A's bench) |
| `etc/init.d/rx2lpro-network-recovery` | new (from iptime) | START=96 boot-only: if `network.interface.lan` never appears, force LAN to eth0 192.168.1.1 + dhcp + radio0 up |
| `usr/share/nftables.d/table-post/20-rx2lpro-flowtable-eth0.nft` | new | the snippet the hwNAT service deletes when `flow_offloading_hw=1` |

## CARRY OVER UNCHANGED

| path | note |
|---|---|
| `lib/netifd/wireless/rtl8192cd.sh` (314 lines, the existing file) | netifd still execs `.sh` handlers on master — wifi-scripts itself ships `files/lib/netifd/wireless/mac80211.sh`. ⚠ **Two options are now fed to it in a form it does not understand**: master's schema/LuCI write `band='2g'` and `country='DE'`, the handler passes them straight into `RTL8192CD.dat` as `band=2g` (it wants the vendor bitmask, `11` = 11b\|11g\|11n) and `regdomain=DE` (it wants the numeric index). Fix = adopt base A's mapping (`rtl8192cd.sh:237-…` json_get_vars + country→regdomain table, RU=12), or the radio comes up on the wrong channel plan with the wrong PHY mode. Tracked in `../REPORT.md`. |
| `etc/sysctl.d/30-hwnat.conf` | `net.netfilter.nf_conntrack_acct=1` — the frozen-counter test for "did the flow really move into silicon" is model-independent and cheap. Keep. |
| `etc/rc.local` | already an intentionally empty file; keep empty. |

## DROP — do not carry into the rtl819x target

Everything below is DIR-842 R1 (`gwr1200ac-v1`) hardware knowledge or 4.14-base
machinery. That board does not exist in `target/linux/rtl819x`, so these are no
longer "gated", they are dead files in the image. The gating added in the last
audit (`[ "$(cat /tmp/sysinfo/board_name)" = "gwr1200ac-v1" ] || exit 0`) is the
right shape for a shared target; it is not needed in a target that has one
vendor family.

| path | reason |
|---|---|
| `etc/init.d/dir842-asic` (260 lines) | S97 D-Link ASIC bring-up: `fabric_reset`/`gw_prog`, a 17-byte read of `/dev/mtd1` (= the Tenda **Bootloader** on this flash map), D-Link VLAN mask 0x10F. Replaced by `rtl819x-hwnat-gate`. README risk #11. |
| `etc/init.d/dir842-l2flush` | same board, `warm_hosts` pinging to re-fill L2 the D-Link driver wiped |
| `etc/init.d/asic-wifi-settle` | restart-shim for the above. ⚠ The ordering problem it solved is real on RTL8197F generally ("both radios beacon, nobody can associate") — if that reappears on the new base, give it its own settle pass against `rtl819x-hwnat-gate`, do not resurrect this file |
| `etc/config/dir842` | lives in /etc/config, i.e. sysupgrade PRESERVES it forever; an orphan board-config stanza is the one thing gating cannot clean up |
| `etc/sysctl.d/31-dir842-lowmem.conf` | tuned for the DIR-842 R1's 64 MiB / vendor slab layout |
| `etc/uci-defaults/09_wireless-dualband-dir842` | seeds an RTL8822BE 5 GHz radio this board does not have; its header also documents a stale radio0/radio1 mapping |
| `etc/uci-defaults/10_wireless-5g-ifname-dir842` | pins rtw88's ifname against rtl8192cd's wlan0 — no rtw88 in our image |
| `etc/uci-defaults/11_dir842-htmode-ht20` | replaced by the width migration inside 07_ |
| `etc/uci-defaults/97-dir842-l2flush-enable`, `98-asic-wifi-settle-enable` | enablers for dropped services |
| `etc/uci-defaults/99-dir842-m5` (257 lines) | D-Link M5 board layer |
| `etc/uci-defaults/12_wireless-2g-rx2lpro` | superseded by `lib/wifi/rtl8192cd.sh` + `07_wireless-2g-rx2lpro`; **also contains the wrong radio MAC delta** (see §Corrections) |
| `etc/board.d/01_leds` | carries no `rx2l-pro` case at all, and every other case names another board. LED GPIO numbers for this board are still undecoded (DTS TODO, README risk #9) — an LED node on the wrong pin held the external switch in reset on the reference board, so shipping a guess is worse than shipping nothing. Stays dropped until the vendor `dev_leds` table is decoded |

## Corrections found during this audit (measured, sources in place)

1. **Radio MAC deltas.** Brief said 2.4 GHz = LAN+4, 5 GHz = LAN+0xC. This
   unit's `CFG` actually stores `TENDA.wlan0_mac = LAN+0x14` and
   `TENDA.wlan1_mac = LAN+0x04` at 0x5a5 / 0x5c7; there is no +0xC anywhere.
   Sources: `/tmp/rx_dump/flash/mtd2_CFG.bin`, plus stock
   `/tmp/rx_dump/rootfs/x/etc_ro/eth_name_conf.txt` (`wl2_4:wlan0; wl5_0:wlan1`)
   and `eth_to_port_config.txt`. Band↔delta attribution stays explicitly open
   (see `07_wireless-2g-rx2lpro`), because the vendor's own labels disagree
   with themselves; the shipped 4.14 seed picked LAN+4 for 2.4 GHz.
2. **There is no `wan_mac` key at all** — confirmed by grepping every
   `*mac*` key in the CFG dump. WAN = LAN+1 remains our invention, matching
   PROVENANCE.md. base B's sibling Tenda boards use LAN+2 (AC23) and LAN+7
   (Nova MW5): `/tmp/rebase-base/target/linux/realtek/base-files/etc/board.d/02_network:127,133`.
3. **The four jacks are 1 WAN + 3 LAN, not 4 LAN.** `eth_to_port_config.txt`
   on this unit maps `eth0:4 eth1:1 eth2:3 eth3:2` and `eth_name_conf.txt`
   declares `wan_1:eth1; p_wan_1:eth1` with only three physical LAN entries
   (`p_lan_1/2/3` = eth0/eth2/eth3). FACTS.md §5's "jacks are switch ports
   1..4" is the port list, of which port 1 is WAN.
4. **`FACTS.md` §1 says 8 MiB SPI NOR; the flash map in both README and
   `dts/RX2LPRO.dts` is 16 MiB** (KernelFS 0x040000..0xFC0000 = 15.5 MiB, CFM
   at 0xFC0000, mtd0 ALL to 0xFF0000). The 16 MiB figure is the one the image
   recipe and `IMAGE_SIZE := 15872k` depend on. Not my call to edit, but the
   sysupgrade size check inherits it, so flagging it.
