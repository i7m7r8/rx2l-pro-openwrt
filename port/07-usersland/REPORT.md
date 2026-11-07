# Track 07 — base-files, uci-defaults, sysupgrade, modern userland

Scope: audit of `files/target/linux/realtek/base-files/` end-to-end against
base A (`/tmp/rebase/openwrt-rx23302/openwrt/target/linux/rtl819x`, upstream
openwrt @ `82e2e4f4`, kernel 6.18), plus the staged replacement overlay,
sysupgrade design, seed config and first-boot checklist.

Nothing here was built or run on hardware. Every claim cites a file I read;
unverified items say UNVERIFIED.

## Deliverables

| file | what |
|---|---|
| `base-files/` | staged overlay: `etc/board.d/02_network`, `etc/uci-defaults/{05,06,07}_*`, `etc/init.d/{rtl819x-hwnat,rtl819x-hwnat-gate,rx2lpro-network-recovery}`, `lib/{realtek.sh,wifi/rtl8192cd.sh,upgrade/platform.sh}`, `usr/share/nftables.d/table-post/20-rx2lpro-flowtable-eth0.nft`, plus **`MANIFEST.md`** (carry-over / drop / corrections table — read this first) |
| `sysupgrade.md` | image↔partition contract, what is backed up, the single-bank problem, 4-layer "unreachable ≠ bricked" design, and the two decisions the image track owes |
| `seed.config.new` | 54 active CONFIG_ lines, each annotated with the file it was verified in or UNVERIFIED + the command to settle it; includes the defconfig silent-drop gate |
| `first-boot-checklist.md` | 30 risk rows (A/B/C/W/U/S/R/K) with the read-only command that proves each, and the ordered console sequence |

All 10 shell files pass `sh -n`.

## Corrections to the brief (measured, with sources)

1. **Radio MAC offsets are wrong in the brief and in the shipped seed.**
   This unit's `CFG` (`/tmp/rx_dump/flash/mtd2_CFG.bin`) stores
   `TENDA.lan_mac` @0x22d, `TENDA.wlan0_mac` @0x5a5, `TENDA.wlan1_mac` @0x5c7
   with deltas **LAN+0x14** and **LAN+0x04**. There is no `+0xC` anywhere, and
   no `wan_mac` key at all. Stock `/etc_ro/eth_name_conf.txt` (`wl2_4:wlan0;
   wl5_0:wlan1`) implies 2.4 GHz = +0x14, while
   `etc/uci-defaults/12_wireless-2g-rx2lpro:70` computes `lan+4` **for the 2.4
   GHz AP** — i.e. what shipped hands the 2.4 GHz radio the address the vendor
   stores for 5 GHz. Band attribution is genuinely ambiguous in stock's own
   config, so the staged code reads the *key*, resolves the band at runtime
   from `/proc/wlanN/mib_rf phyBandSelect` (base A's technique) and **logs the
   choice**. Consequence of being wrong is a bad BSSID, never lost access.
2. **The four jacks are 1 WAN + 3 LAN, not 4 LAN.** `eth_to_port_config.txt`
   (`eth0:4 eth1:1 eth2:3 eth3:2`) + `eth_name_conf.txt` (`wan_1:eth1`,
   `p_lan_1/2/3` = eth0/eth2/eth3). Switch ports: 1=WAN, 2/3/4=LAN, 6=CPU
   (mask 0x40), 0 and 5 unused. FACTS §5's "jacks are switch ports 1..4" is
   the port list, of which port 1 is WAN — that changes `ucidef_add_switch`
   and the LuCI port labels.
3. **WAN = LAN+1 stays our invention** (consistent with PROVENANCE.md). Data
   point base B adds: Tenda AC23/Lynx use LAN+2, Nova MW5 LAN+7
   (`/tmp/rebase-base/target/linux/realtek/base-files/etc/board.d/02_network:127,133`).
   Both +1 and +2 are free on this unit.
4. **FACTS §1 says 8 MiB SPI NOR; the flash map is 16 MiB** (KernelFS
   0x040000–0xFC0000 = 0xf80000, CFM at 0xfc0000). `IMAGE_SIZE := 15872k` and
   the sysupgrade size check depend on the 16 MiB figure.
5. **FACTS §1 and README contradict each other on dual-bank.** FACTS: no
   KernelFS1/2, no rollback. README "Recovery net": vendor runs a dual image
   with `env set image_boot 1|0 unlock`. This decides whether
   `platform_do_upgrade` can use an ordering trick at all. One read-only
   command settles it (`sysupgrade.md` §4, checklist R3).

## Biggest usability risks, ranked

1. **`CONFIG_PACKAGE_kmod-rtl8192cd` does not exist on base A — and the old
   seed selects nothing else for wireless.** In base A the vendor HAL is an
   in-tree kernel driver (`CONFIG_WLAN_HAL`/`WLAN_HAL_8197F`/
   `RTL8192FE_RX23302` in `rtl8197f/config-6.18`), not a package. So the exact
   silent failure mode that already bit this repo once (missing `luci` → no
   uhttpd) is sitting in `seed.config` line 34, aimed at the *radio that is
   the fallback access path*. Fix: kernel symbols in the subtarget config +
   `CONFIG_PACKAGE_iwinfo` + `CONFIG_PACKAGE_rtlmib` in the seed + the
   defconfig-drop gate in `build.sh`.
2. **VLAN ids 1/2 vs the SoC's hardcoded 9/8.** With base A's swconfig driver
   the internal RTL8197F switch is in the same path and only forwards VID 9
   (LAN) and 8 (WAN) on internal port 0 (`rtl819x_eth.c:54,55,899`).
   ucidef's generated ids are 1/2 → jacks up, nothing passes. base A already
   ships the renumbering uci-default for this reason; staged as
   `06_switch_vlan_ids`. This is the most likely way "the switch driver works"
   still produces an unreachable box.
3. **`wifi detect` is gone.** wifi-scripts' `/sbin/wifi` exits 1 on `detect`
   and enumerates through `include /lib/wifi` + `detect_$driver` on `config`
   (`files/sbin/wifi:58-81`). Since our radio has no cfg80211 wiphy, the ucode
   detector finds nothing for it: without the new `lib/wifi/rtl8192cd.sh`, the
   rebase image boots with **no wireless at all**, which removes the fallback
   for risk #6 in the README table.
4. **Our `rtl8192cd` netifd handler speaks the old option schema.** It maps
   `htmode` correctly but passes `band` and `country` straight into
   `RTL8192CD.dat` as vendor MIB values (`band=11` bitmask,
   `regdomain=<index>`); master and LuCI write `band='2g'` and `country='DE'`.
   Result: no 11n and/or the driver's default FCC domain — wrong channel plan,
   silent. base A's 527-line handler has both mappings and is GPL-2.0.
5. **LuCI (ucode) does not know `type=rtl8192cd` and will silently open the
   network** — base A documented finding from hardware, 2026-10-05, fixed with
   a luci feed patch (`feeds-patches/luci-mod-network/100-…-rtl8192cd.patch`).
   CI must apply it; there is no other way to reach this from a seed.
6. **firewall4 + nftables changes the failure mode of a missing kmod from
   "error" to "deny-all"**, and a missing `kmod-nft-offload` makes the whole
   offload story look like a no-op. Two of the three nft/offload symbols in
   `seed.config.new` are UNVERIFIED locally (no full checkout on this machine)
   — they must be settled by the drop-gate, not by me asserting them.
7. **Recoverability.** Staged design has 4 independent layers: runtime
   switch-model detection in `02_network` (falls back to `LAN=eth0`, generates
   no WAN, never bridges), unconditional 2.4 GHz seed, a START=96
   `network-recovery` service that forces `eth0`/192.168.1.1/DHCP/AP if
   `network.interface.lan` never appears, and outside-Linux
   `disaster_web`/failsafe. Note honestly: **failsafe needs a button, and
   `gpio-keys` is still deliberately empty** (undecoded vendor GPIO table), so
   today layer 4 is `disaster_web` + serial only.

## Needs from elsewhere

**kernel/image track**
- `CONFIG_WLAN_HAL`, `CONFIG_WLAN_HAL_8197F`, `CONFIG_RTL8192FE_RX23302`
  equivalent for our board, `CONFIG_SWCONFIG`/`SWCONFIG_LEDS`/
  `RTL8367B_PHY`/`NET_RTL819X`/`PHYLINK` in `rtl8197f/config-6.18`; and a
  decision whether the RTL8192CD config path is WEXT (our handler) or
  ioctl-less (`rtlmib`, base A). Our handler's `phyBandSelect=1` is
  **hardcoded**, so it cannot drive a 5 GHz rtl8192cd device at all.
- mtdsplit_cvimg vs fixed partitions (`sysupgrade.md` §6.1) — my
  `platform_check_image` assumes the one-partition/variable-offset shape; if
  the tree moves to kernel+rootfs partitions, `06_switch_vlan_ids`' siblings
  and `platform_do_upgrade` both change.
- Rename `Device/RX2LPRO` → `Device/tenda_rx2lpro`, keep `rx2l-pro` in
  `SUPPORTED_DEVICES`, and consider `ucidef_set_compat_version` (§6.2).
- External-switch DT node: `compatible="realtek,rtl8367b"` +
  `realtek,cpu-port=<6>` + `realtek,init-regs`, with the **gpiochip/line
  numbering measured on this base** (base B `&gpio0 56/55/58`, base A's board
  `&gpio1 24/23/26`, iptime `bcm6345-gpio @0x1800350c` lines 18/19 — three
  different numberings for the same three physical pins).
- Verify `rtl819x` procfs/sysfs node names before trusting the two hwnat
  services (`/sys/module/rtl819x/parameters/hwnat`, `/proc/rtl819x_router`,
  `/proc/rtl865x_gw`).

**build/CI track**
- `build.sh`: add the defconfig silent-drop gate + the `luci-base*.ipk`
  artifact gate (`seed.config.new` header has both, ~6 lines).
- `feeds.conf`: replace the two `openwrt-19.07` pins with commit-pinned master
  feeds. Proven working shape (from iptime, master @6e9fd1c3):
  `src-git packages https://git.openwrt.org/feed/packages.git^<sha>` and the
  same for `luci`, plus `routing`/`telephony`/`video` only if something
  installs from them. rx23302's `versions.sh` is the other pin style
  (`FEED_LUCI_COMMIT=…` + `./scripts/feeds update -a; install -a`). **Keep the
  `#!` comment rule** — bare `#` lines break `scripts/feeds` (commit b0315dd).
- CI must apply base A's `feeds-patches/luci-mod-network/100-…patch` to the
  checked-out luci feed, and carry its 7 `iwinfo` WEXT patches.
- `FEEDS_INSTALL` list changes with the feed pins (`luci-app-opkg` →
  `luci-app-package-manager`, `miniupnpd` → `miniupnpd-nftables`).

**docs track**
- README: fix the 8 vs 16 MiB and the `image_boot` contradiction, replace the
  first-boot risk table with `first-boot-checklist.md`, and delete the
  DIR-842-specific risks (#9, #11) from the RX2L Pro table since those files
  are dropped (`base-files/MANIFEST.md`).
- FACTS.md §1 (flash size), §5 (port 1 is WAN, not a LAN jack).

**not mine to decide, flagged only**
- 5 GHz RTL8832BR remains unaddressed in every 6.x tree; this track keeps it
  deliberately unseeded and explains why the alternative is worse.
- LED GPIO table: `01_leds` is dropped rather than guessed, because on the
  reference board an LED node on the wrong pin held the external switch in
  reset.
