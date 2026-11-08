# First-boot risk checklist — 6.18 / master base

Updated replacement for the README "First-boot risk checklist" table. Every row
is a decision that exists in *this* tree (file named), has a symptom that is
distinguishable from its neighbours, and has a **read-only** proof command.
Nothing here writes flash, nothing reboots, nothing changes link state.

Order of use: RAM-boot `openwrt-rtl819x-rtl8197f-tenda_rx2lpro-initramfs-kernel.bin`
through the Realtek loader shell (`LOADADDR`, `XMOD`, `IPCONFIG`) and run the
rows top to bottom on the serial console at **115200** (this board's own
`/proc/cmdline`; the DIR-842 reference is 38400 and that difference is not
cosmetic — it is the first thing you will "misconfigure" and blame on the base).

Legend: **[ inherited ]** = carried from the 4.14 table, still true.
**[ NEW ]** = introduced by the rebase, not present in the old risk list.

---

## ⚠ READ THIS FIRST — the table below predates the current tree

Several rows describe files that have since been **removed or replaced**. The
rows are kept because their *symptom* and *proof* columns are still the right
way to diagnose each failure, but these facts supersede what they assert:

| row | what it says | what is true now |
|---|---|---|
| A1 | `lib/realtek.sh` rewritten | **deleted**. `board_name` comes from the DTS `compatible` alone (`02_sysinfo`: `strings /proc/device-tree/compatible \| head -1`). Nothing writes `/tmp/sysinfo/board_name` from `/proc/cpuinfo` any more. |
| A2 | DTS is `dts/RX2LPRO.dts` | now `dts/rtl8197f_tenda_rx2lpro.dts`, and the consumers match `rx2l-pro\|*rx2l-pro`, so both `rx2l-pro` and `tenda,rx2l-pro` (the value the kernel actually reports) work. A pre-clone CI gate enforces it. |
| A3 | profile in `image/Makefile` | now `files/target/linux/rtl819x/image/rtl8197f.mk`, device `tenda_rx2lpro` (hyphen-free: the Kconfig symbol is `DEVICE_$(subst -,_,NAME)`). |
| B3 | "base A ships no mtdsplit_cvimg at all" | **fixed**: `mtdsplit_cvimg.c` ships in this tree with an additive Makefile/Kconfig, and `CONFIG_MTD_SPLIT_CVIMG_FW=y` is set. Without that symbol the parser was compiled by nothing and the firmware partition was never split. |
| B5 | rootfs_data comes from `cvimg -j` marker | **no marker is shipped, deliberately**: `CONFIG_MTD_SPLIT_SQUASHFS_ROOT=y` carves `rootfs_data` from the tail of `rootfs`, and the reference board's reason for disabling that (a loader checksum lives right after the squashfs) does not apply here — our container's checksum is inside the container at 16+len. |
| C1 | switch has "no DT node and no driver" | **fixed**: the board DTS has the `rtl8367b` node (SMI on gpio1 24/23/26, CPU port 6, `realtek,extif` 10 cells) and `CONFIG_RTL8366_SMI=y` + `CONFIG_RTL8367B_PHY=y` select the base's own wiring. (An earlier claim that `patches-6.18/999` had to define `RTL8367B_PHY` was wrong: openwrt's generic `700-swconfig_switch_drivers.patch` already defines it, and our duplicate definition produced a Kconfig recursive dependency that stopped the kernel from configuring at all. See `port/03-dts-switch/REPORT.md`.) |
| C2 | runtime netmodel detection | **not used**. One model: swconfig `switch0`, VLANs 9/8, devices `eth0.9`/`eth0.8`. |
| C3 | `06_switch_vlan_ids` must run | ours is `13_switch_vlans_rx2lpro`, gated on `tenda,rx2l-pro` (the platform's 06 is gated on its own board and is inert here). Same renumber: 1→9, 2→8, bridge port and WAN device follow. |
| — | switch port map unknown | **now measured** from the stock CFG: `lan1/2/3.phyid=4/3/2`, `wan1.phyid_default=1`, CPU trunk bit 6 in both VLAN masks. |
| — | 5 GHz | **absent and off by design**: `CONFIG_PCI` is not set, and it could not have been — the platform's `arch/mips/realtek/Kconfig` still `select HW_HAS_PCI`, a symbol 6.18 does not have. See `port/06-wifi-5g-8832br/REPORT.md`. Do not expect `wlan0`-as-5GHz on first boot. |


---

## A. Identity — does the board name resolve at all

| # | Risk | Where it lives | Symptom if wrong | Proof (read-only) |
|---|---|---|---|---|
| A1 | **[ NEW ]** Board name is now the DT `compatible`, not `/proc/cpuinfo`'s `machine` string. Nothing in the rtl819x target ships a `*_board_detect()` any more, so `realtek_board_detect()` from the 4.14 tree is dead code, not a safety net. | `base-files/lib/realtek.sh` (rewritten), `etc/board.d/02_network` | `board_name` = `generic` → our case never matches → package/base-files' `99-default_network` gives `LAN=eth0` and no WAN. Degraded, NOT bricked (better than 4.14's "configure nothing") | `cat /tmp/sysinfo/board_name`<br>`tr '\0' '\n' < /proc/device-tree/compatible \| head -1`<br>`uci show network \| head -20` |
| A2 | **[ NEW ]** The DTS `compatible` and every string literal in base-files must agree (`tenda,rx2l-pro` vs the legacy `rx2l-pro`). The staged files accept both; a *third* spelling reopens A1. | `dts/RX2LPRO.dts:22`, `02_network`, three `uci-defaults`, three `init.d` | as A1, but only after a sysupgrade (config already generated → nothing re-runs) | `rx2lpro_board_name; rx2lpro_is_board; echo $?`<br>`grep -rn 'rx2l-pro' /rom/etc/ \| wc -l` |
| A3 | **[ inherited ]** `model` in the DTS vs `image/Makefile` profile name vs the metadata `supported_devices` list. | `image/Makefile` `Device/RX2LPRO` | `sysupgrade` refuses a valid image, or accepts an invalid one | `ubus call system board` (compare `board_name`, `model`) |

## B. Root device — the thing that stops a boot

| # | Risk | Where it lives | Symptom if wrong | Proof (read-only) |
|---|---|---|---|---|
| B1 | **[ inherited ]** No `root=` in the cmdline by design (`CONFIG_MIPS_CMDLINE_FROM_DTB=y` + chosen bootargs); the root device comes from mtdsplit. | `dts/RX2LPRO.dts` chosen; `mtdsplit_cvimg.c` | `VFS: Unable to mount root fs on unknown-block(0,0)` — identical hard hang every boot | `cat /proc/cmdline`<br>`dmesg \| grep -iE 'mtdsplit|squashfs|Creating MTD'`<br>`cat /proc/mtd` |
| B2 | **[ inherited ]** The partition must be labelled exactly `firmware` for `CONFIG_MTD_SPLIT_FIRMWARE_NAME` to fire. | `dts/RX2LPRO.dts partition@40000` | as B1 | `cat /proc/mtd` (expect `kernel`, `rootfs`, `rootfs_data` created under `firmware`) |
| B3 | **[ NEW ]** squashfs must sit at **un-aligned** `16+len`. Kernel 6.18's mtdsplit core is a different file from 4.14's — our patched parser has to be re-applied to the new tree, and base A ships **no mtdsplit_cvimg at all** (fixed partitions instead). | `files/target/linux/generic/files/drivers/mtd/mtdsplit/mtdsplit_cvimg.c` — does base A's tree even have that directory? | works one build, silently fails the next (kernel size mod 4) | `dmesg \| grep -i 'rootfs offset'`<br>`hexdump -C -s $((0x40000+16+LEN)) -n 4 /dev/mtd3` vs `dd if=/dev/mtd3 bs=1 count=4 skip=$((16+LEN))` — compare to the header's `len` field: `hexdump -v -n4 -s12 -e '1/1 "%02x"' /dev/mtd3` |
| B4 | **[ NEW ]** `/etc/config/*` is preserved across sysupgrade only because the providing packages declare those files as conffiles. If `wireless` or `network` is not on that list, the config resets on every upgrade and it looks like a uci-defaults bug. | package Makefiles `conffiles` sections | "my settings vanish after sysupgrade" | `grep -h '^/etc/config' /etc/opkg/*.conffiles /lib/upgrade/keep.d/* \| sort -u`<br>`ls /etc/config` |
| B5 | **[ NEW ]** `rootfs_data` is *derived* (mount_root carves it from the tail of `rootfs`), not declared in the DTS. If the jffs2 marker section is missing from the image, there is no overlay: everything runs read-only, `/etc/config` writes fail, and `sysupgrade -c` saves nothing. | `image/Makefile` `cvimg-pad-rootfs` (`cvimg -j`) | `logread` / `uci commit` errors: "Read-only file system" | `mount \| grep -E 'overlay|rootfs_data'`<br>`cat /proc/mtd \| grep rootfs_data`<br>`dmesg \| grep -i jffs2` |

## C. Ethernet / switching — the historically unresolved one

| # | Risk | Where it lives | Symptom if wrong | Proof (read-only) |
|---|---|---|---|---|
| C1 | **[ inherited, still open ]** External RTL8367-family switch has no DT node and no driver landed for *this* board; CPU trunk assumed port 6 (untagged mask `0x40`). | `dts/RX2LPRO.dts` TODO; staged `02_network` | box alive, reachable only over UART or the 2.4 GHz AP; WAN dead | `ls /sys/class/net/`<br>`ls /sys/class/switch/ 2>/dev/null` (swconfig agent)<br>`swconfig dev switch0 show 2>/dev/null \| head -40`<br>`dmesg \| grep -iE 'rtl8367|rtl8366|smi|dsa|phylink'` |
| C2 | **[ NEW ]** Which model actually won decides every device name in the config: swconfig → one `eth0` + `eth0.9`/`eth0.8`; DSA → `lan1..lan3`, `wan`, `eth0` conduit. LuCI, the firewall zones, the LEDs and the nft flowtable snippet all follow it. | staged `02_network` runtime detection + `lib/realtek.sh:rx2lpro_netmodel()` | config generated for the other model: `br-lan` exists but has no ports, or `network.lan.device=eth0.9` with no such netdev | `rx2lpro_netmodel`<br>`ubus call network.interface.lan status \| jsonfilter -e '@["l3-device"]'`<br>`brctl show 2>/dev/null \|\| ip link show br-lan` |
| C3 | **[ NEW ]** With base A's swconfig path, the SoC's **internal** switch is in the same path and hard-codes `RTL_NASH_VID=9 / RTL_WAN_VID=8 / RTL_LAN_PORTY=0x11F` (`rtl819x_eth.c:54,55,899`). ucidef's generated ids are 1/2 → jacks administratively UP, carrying nothing. `uci-defaults/06_switch_vlan_ids` is the fix and must run. | staged `etc/uci-defaults/06_switch_vlan_ids` | DHCP on the LAN jack times out while `eth0` link is up and `switch0` looks programmed | `uci show network \| grep -E 'vlan|fid|device'`<br>`swconfig dev switch0 show \| grep -A12 'Global VLAN'`<br>`dmesg \| grep 'rtl819x: router mode'` |
| C4 | **[ NEW ]** PHYLINK replaced the 4.14 driver's private phy attach. `rtl819x_eth.c` Kconfig *selects* PHYLINK; the RGMII internal/external delay properties live on the **switch's** cpu port, not on eth0, and a missing `rx-internal-delay-ps` gives a link that is up at 1000/full and passes nothing (or nothing at all). | base A `files-6.18/drivers/net/ethernet/rtl819x_eth.c`, `dts/RTL8197F.dtsi`, our board DTS | `eth0: link becomes ready` + `Speed: 1000Mb/s` and zero traffic; RX-only (TX wedge look-alike) | `ethtool eth0` (driver name must be `rtl819x_eth` or `rtl819x` — see K1)<br>`dmesg \| grep -iE 'phylink|validated interface'`<br>`cat /sys/class/net/eth0/carrier /sys/class/net/eth0/speed` |
| C5 | **[ NEW ]** The SMI GPIO numbers. FACTS §5 pins MDC=SDK H0=GPIO56, MDIO=SDK G7=GPIO55, reset=H2=GPIO58 — but which *gpiochip* and which *line offset* the RTL8197F driver exposes differs between the two bases: base B's template uses `&gpio0 56/55/58`, base A's rx23302 board uses `&gpio1 24/23/26` for the same physical lines, iptime uses a `brcm,bcm6345-gpio` node at `0x1800350c` with `gpio 18/19`. Copying numbers across bases is a wrong-pin write. | board DTS `rtl8367b` / switch node | switch not detected (id read fails → `rtl8367b: no switch found`) **or** the reset line drives a functional pin: switch held in reset, which is indistinguishable from a brick (old risk #9) | `ls -l /sys/devices/platform/soc/*/gpio/` and `cat /sys/kernel/debug/gpio` (needs debugfs mounted: `mount -t debugfs none /sys/kernel/debug` is a RAM-boot-only write to the mount table, no flash) <br>`dmesg \| grep -iE 'rtl8367|gpio'` |
| C6 | **[ NEW ]** Hardware NAT / offload is armed from userspace **after** ifup, never from the cmdline; and router mode rewrites the switch PVID/VLAN/MACCR1. | `dts/RX2LPRO.dts` chosen (deliberately no `hwnat`), `etc/init.d/rtl819x-hwnat-gate` START=95 | TX wedge at boot: it boots, console is quiet, you cannot reach it (old risk #11, measured twice) | `cat /proc/rtl819x_router 2>/dev/null`<br>`cat /sys/module/rtl819x/parameters/hwnat 2>/dev/null`<br>`ls /etc/rc.d/ \| grep -E 'S17\|S95'` |

## W. Wireless

| # | Risk | Where it lives | Symptom if wrong | Proof (read-only) |
|---|---|---|---|---|
| W1 | **[ NEW ]** `wifi detect` is **retired**: wifi-scripts' `/sbin/wifi` exits 1 with "Wifi detect is deprecated. Use wifi config instead" (`files/sbin/wifi:58-61`); enumeration is `wifi config` → `include /lib/wifi` → `detect_rtl8192cd` (:64-81). Our radio has no cfg80211 wiphy, so the ucode detector contributes nothing for it and `/lib/wifi/rtl8192cd.sh` is the only writer of the stanza. | staged `lib/wifi/rtl8192cd.sh`; `seed.config.new` (`CONFIG_PACKAGE_wifi-scripts`) | image boots with **no wireless at all** → the fallback access path is gone (compounds C1) | `grep -n 'wifi config\|uci_apply_defaults' /etc/init.d/boot`<br>`ls /lib/wifi/`<br>`wifi config; uci show wireless \| head -20`<br>`ls /proc/wlan* 2>/dev/null` |
| W2 | **[ NEW ]** Order dependency: if `wifi config` runs *after* `uci_apply_defaults`, `07_wireless-2g-rx2lpro` sees no radio stanza. It exits non-zero on purpose (uci-defaults are only deleted when they SUCCEED), so the fix is a second boot — but you must know which side of the race the base is on. | staged `etc/uci-defaults/07_wireless-2g-rx2lpro` | "wireless is empty on first boot, right on the second" | `ls /etc/uci-defaults/ \| grep 07_` (still present = it has never succeeded)<br>`logread \| grep rx2lpro-wifi` |
| W3 | **[ NEW ]** Radio MAC band attribution is unresolved: `CFG` stores `TENDA.wlan0_mac = LAN+0x14` and `TENDA.wlan1_mac = LAN+0x04`; stock `eth_name_conf.txt` says `wl2_4:wlan0; wl5_0:wlan1` (⇒ 2.4 GHz = +0x14), and the shipped 4.14 seed used +4 for 2.4 GHz. The brief's "+4 / +0xC" is wrong on both counts (there is no +0xC anywhere in CFG). | `02_network` §3 note; `07_wireless-2g-rx2lpro` | wrong BSSID only — never lost access. Cheap to fix now, annoying later (clients, MAC filters, `iwinfo`) | `grep -aE 'phyBandSelect' /proc/wlan0/mib_rf` and `…/wlan1/mib_rf` (1=2.4 GHz, 2=5 GHz)<br>`uci get wireless.radio0.macaddr` vs `logread \| grep rx2lpro-wifi`<br>values are per-unit: compare only the **last** octet against the LAN MAC |
| W4 | **[ NEW ]** The vendor handler speaks the old option schema. Our `/lib/netifd/wireless/rtl8192cd.sh` maps `htmode`→`use40M`/`2ndchoffset` correctly but passes `band` and `country` **straight through** as the vendor MIB values (`band=11` bitmask, `regdomain=<index>`). master's schema and LuCI write `band='2g'` and `country='DE'`. | `base-files/lib/netifd/wireless/rtl8192cd.sh` (carried unchanged; see `base-files/MANIFEST.md`) | radio comes up with the wrong PHY mode (no 11n) and/or falls back to the driver's FCC domain — i.e. wrong channel plan, possibly illegal channels. Silent. | `cat /proc/wlan0/mib_rf \| grep -iE 'phyBandSelect\|dot11BssType\|CountryIeDomain'`<br>`iwpriv wlan0 show_mib \| grep -iE 'band\|regdomain'`<br>`cat /tmp/RTL8192CD.dat` (symlink target of /etc/Wireless) |
| W5 | **[ inherited ]** 5 GHz RTL8832BR has no driver in any 6.x tree we hold; seeding it is worse than not seeding it (netifd fails the whole wireless bring-up). | `07_wireless…` §3, `seed.config.new` | `wifi up` errors, 2.4 GHz also dead | `lspci \| tail`<br>`dmesg \| grep -iE '8832\|rtw89\|pci 0000:00:00.0'` |
| W6 | **[ NEW ]** `/etc/Wireless → /tmp` must exist before the first `wifi up`, or every interface-up is a flash write to the overlay (and the driver's `RTL8192CD.dat` rewrite fails outright on a read-only root). | `07_wireless…` | `Cannot write /etc/Wireless/RTL8192CD.dat` in dmesg; radio uses whatever the module default is | `ls -l /etc/Wireless`<br>`dmesg \| grep -i 'Set MIB from'` |

## U. Userland — ucode LuCI and the nftables switch

| # | Risk | Where it lives | Symptom if wrong | Proof (read-only) |
|---|---|---|---|---|
| U1 | **[ NEW ]** A selected-but-nonexistent CONFIG_ symbol is dropped by `make defconfig` **without any error**. The whole point of `seed.config.new`'s header gate. | `seed.config.new`, `build.sh` | image with no web UI / no firewall4 / no swconfig userspace, and a green CI run | On the box: `which luci uhttpd fw4 swconfig iwinfo`<br>`ls /usr/lib/lua 2>/dev/null; ls /usr/lib/ucode 2>/dev/null` (which LuCI generation is installed)<br>Offline, from the artifact: `tar tzf openwrt-…rootfs.tar.gz \| grep -E 'ucode-mod-html\|luci-base'` |
| U2 | **[ NEW ]** firewall4/nftables replaced firewall3/iptables. If `kmod-nft-offload` is missing, `flow_offloading=1` is accepted, the ruleset loads, and **nothing** offloads — reads exactly like "the perf work did nothing". If nft itself is mis-kernel-config'd, fw4 goes deny-all with a clean console. | `seed.config.new`, `05_enable_flow_offloading`, `20-rx2lpro-flowtable-eth0.nft` | LAN clients associate/get DHCP but no NAT/no transit; or total silence | `nft list ruleset \| head -20`<br>`nft list flowtable inet fw4 ft 2>/dev/null`<br>`conntrack -L \| grep -c '\[OFFLOAD\]'` (needs `nf_conntrack_acct`/events; `sysctl net.netfilter.nf_conntrack_acct`)<br>`logread \| grep -iE 'fw4\|nft'` |
| U3 | **[ NEW ]** LuCI (ucode) does not know `type=rtl8192cd`: it probes capabilities through hostapd, so the encryption dropdown loses WPA2. base A ships a **luci feed patch** for this and CI must apply it or the UI actively opens the network on Save. | `/tmp/rebase/openwrt-rx23302/openwrt/feeds-patches/luci-mod-network/100-rx23302-wireless-rtl8192cd.patch`; `REPORT.md` "Needs from elsewhere" | LuCI shows only "No Encryption"; saving turns a PSK network into an open one | `grep -c rtl8192cd /usr/lib/lua/luci/model/cbi/… 2>/dev/null` (19.07) — on master: `grep -rc rtl8192cd /usr/share/luci/ /usr/lib/ucode/ 2>/dev/null \| grep -v ':0'`<br>`ubus call network.wireless status` |
| U4 | **[ NEW ]** `cgi-io` + `rpcd` + `uhttpd-mod-ubus` + `liblucihttp-ucode` are the four things that make the web UI able to *do* anything (upload, backup download, status). Missing one is a page that renders and then errors in the browser only. | `seed.config.new` | LuCI login works, System→Flash Firmware or Backup/Upload fails | `ls /usr/lib/ucode/ /usr/lib/rpcd/`<br>`ubus list \| grep -E 'rpcd\|file\|status'`<br>`uhttpd -V 2>/dev/null; ls /rom/www/luci-static/ \| head` |

## S. Sysupgrade

| # | Risk | Where it lives | Symptom if wrong | Proof (read-only) |
|---|---|---|---|---|
| S1 | **[ NEW ]** The cr6c geometry checks in `platform_check_image` need `hexdump`, `fwtool`, `get_image`, `mtd` **in the initramfs stage2**. 6.18's busybox config is not 19.07's, and a missing applet makes a check return empty (= passes vacuously). | staged `lib/upgrade/platform.sh` | upgrade "passes" all checks and then does not boot | In the running image (not stage2): `which hexdump fwtool mtd; busybox | grep -o 'hexdump'`<br>Dry run, no write, no risk: `sysupgrade -T openwrt-…sysupgrade.bin` |
| S2 | **[ NEW ]** `sysupgrade.bin` ≠ `factory-cr6c.bin`: only the former has the tar+metadata trailer. With `REQUIRE_IMAGE_METADATA` the factory file is rejected — correct behaviour, but people will reach for `-F`. | `image/Makefile IMAGES` | "Image check failed: metadata missing" | `fwtool -S <file>` (prints the attached metadata or nothing)<br>`fwtool -q -i /dev/null -T <file> \| wc -c` |
| S3 | **[ NEW ]** Board-name change breaks the metadata compatibility check: the profile still says `SUPPORTED_DEVICES := rx2l-pro` while master's identity check uses the metadata list + `ucidef_set_compat_version` (base B ships `etc/board.d/05_compat-version`; we do not). | `image/Makefile`, `sysupgrade.md` §6.2 | sysupgrade refuses an image built for this very board | `cat /proc/cpuinfo >/dev/null; sysupgrade -T <img>`<br>`fwtool -T <img> \| strings \| grep -A2 supported_devices`<br>`uci show system.@system[0] \| grep compat 2>/dev/null` |
| S4 | **[ NEW ]** No `image_boot` to fall back to — see R3. Any upgrade is single-shot. | `sysupgrade.md` §4 | interrupted write = loader cannot find a valid image | `sysupgrade -T`<br>`cat /proc/mtd` (is there a second writable region?) |

## R. Recovery and boot contract

| # | Risk | Where it lives | Symptom if wrong | Proof (read-only) |
|---|---|---|---|---|
| R1 | **[ NEW ]** Console speed 115200 (this board) vs 38400 (both reference boards). A terminal at the wrong speed looks exactly like a dead bootloader. | `dts/RX2LPRO.dts` chosen bootargs; `image/Makefile CMDLINE` | "nothing after `Booting...`" (also old risk #5, which is a *different* cause) | `cat /proc/cmdline` (if the kernel got up, your speed is right) |
| R2 | **[ inherited, still open ]** `start_addr` in the cr6c header (`0x81000000` = our loader link address) vs what Tenda's own stage-1 honours. The staged `platform_check_image` asserts the value; the boot behaviour is still unverified on hardware. | `image/Makefile CVIMG_LOADER_START_ADDR`, `LZMA_TEXT_START` | silent: console stops after `Booting...` | From the dump, offline: `python3 tools/check-cr6c.py --expect-start 0x81000000 <file>`<br>On the box: `hexdump -v -n4 -s4 -e '1/1 "%02x"' /dev/mtd3` |
| R3 | **[ NEW ]** **FACTS.md §1 and README contradict each other on dual-bank.** FACTS: "single image, no KernelFS1/2, no dual-bank rollback". README "Recovery net": "The vendor runs a dual image: KernelFS1/KernelFS2 with `env set image_boot 1\|0 unlock`". The DTS and `/proc/mtd` show one KernelFS. This decides whether `platform_do_upgrade` can order writes at all. | `sysupgrade.md` §4 | wrong assumption ⇒ either an unnecessary two-partition rewrite, or a "safe ordering" that cannot be safe | Read-only, and **do not** run `fw_setenv`: `strings /dev/mtd1 \| grep -oE 'image_boot[0-9=_a-z]*' \| sort -u` (or on the host: `strings flash/mtd1_Bootloader.bin \| grep image_boot`) and `cat /proc/mtd` |
| R4 | **[ NEW ]** Failsafe (hold RESET during boot) is the layer that survives everything above. It needs the button to exist in the DT — currently `gpio-keys` is **intentionally empty** (undecoded vendor GPIO table), so there may be no failsafe button and therefore no `diag.sh` path. | `dts/RX2LPRO.dts` `{/}` TODO; old risk #9 | "the only way back is a serial cable and a solder-side flash re-write" | `ls /sys/class/input/; cat /proc/bus/input/devices \| head -20`<br>`ls /lib/preinit/ \| grep -i failsafe`<br>`grep -rn 'failsafe' /etc/init.d/boot /lib/preinit/99_10_failsafe_login 2>/dev/null` |
| R5 | **[ NEW ]** `disaster_web` (`ipaddr=192.168.1.1 serverip=192.168.1.99`) is gated by a reset strap read at `0xb8003528 & 2`, not by the kernel path. It is the actual last resort. Verify the PC can be addressed at 192.168.1.99 **before** ever needing it. | FACTS §3 | a half-written KernelFS with no TFTP path | Boot, hold reset, then from the PC: `arp -n \| grep 192.168.1.1` / `curl -s http://192.168.1.1/ \| head` — no device writes involved |

## K. Kernel 6.x specifics

| # | Risk | Where it lives | Symptom if wrong | Proof (read-only) |
|---|---|---|---|---|
| K1 | **[ NEW ]** Module and driver names changed between the bases: `rtl819x-eth.c` (4.14) → `rtl819x_eth.c` + `rtl865x_asichal.c` + `rtl819x_hwnat.c`, module `rtl819x`; the sysfs/proc surface (`/sys/module/rtl819x/parameters/hwnat`, `/proc/rtl819x_router`, `/proc/rtl865x_gw`) is base A's, not measured on ours. Our init.d gates guard with `[ -w ]`, so a wrong name is a **silent no-op** — i.e. "hardware NAT never engaged", not an error. | staged `etc/init.d/rtl819x-hwnat-gate` | perf work silently absent; or a wedge if armed too early | `ls /sys/module/ \| grep -i rtl`<br>`ls /sys/module/rtl819x/parameters/ 2>/dev/null`<br>`ls /proc/ \| grep -iE 'rtl819x\|rtl865x'`<br>`ethtool -i eth0` |
| K2 | **[ NEW ]** 6.x driver names for the same hardware differ from the 4.14 seed's assumptions: `kmod-rtl8192cd` **does not exist** on base A (the HAL is built into the kernel via `CONFIG_WLAN_HAL_8197F`), and the *iwpriv*-style configuration depends on `CONFIG_WEXT` surviving in 6.18 (it is deprecated; base A's own handler reports their kernel has no WEXT and they wrote `rtlmib` instead). | `seed.config.new` header; `MANIFEST.md` carry-over table | `make defconfig` eats the symbol, image has no radio, CI green (U1). Or WEXT is gone and every `iwpriv` in the handler no-ops. | `zcat /proc/config.gz \| grep -E 'WEXT\|WLAN_HAL\|RTL8192\|CFG80211'`<br>`iwpriv wlan0 help \| head` vs `rtlmib -h`<br>`ls /proc/wlan0/mib_all` (identity test the handler already uses) |
| K3 | **[ NEW ]** `CONFIG_KERNEL_*` overrides happen *after* the target `config-<ver>` merge: crashlog-off must live in the seed (inherited), and any 6.18 reconfiguration (e.g. `CONFIG_MIPS_CMDLINE_FROM_DTB`, `CONFIG_MTD_ROOTFS_ROOT_DEV`) must be re-verified in the new subtarget config, not assumed to be inherited. | `seed.config.new` tail; B1 | as B1 (no root device) or the crashlog hang-on-oops behaviour returns | `zcat /proc/config.gz \| grep -E 'CRASHLOG\|CMDLINE_FROM_DTB\|MTD_ROOTFS_ROOT_DEV\|MTD_SPLIT_FIRMWARE_NAME\|SQUASHFS'` |
| K4 | **[ NEW ]** No `ramoops`/pstore carve-out, deliberately (known TX wedge on this family: CPU-originated frames dropped while RX works). If the rebase "helpfully" enables pstore in the new config, that wedge returns. | `dts/RX2LPRO.dts` `{/}` comment | intermittent dead-TX-still-works-RX | `ls /sys/fs/pstore/` (must be empty/absent)<br>`zcat /proc/config.gz \| grep -i PSTORE` |

---

## The sequence, in order, on the serial console

Nothing gets flashed until every line below looks right on the RAM-booted
image. Read-only; `wifi config` and `sysupgrade -T` touch RAM and stdout only.

```
cat /proc/cmdline                              # 115200, no root=      R1 B1
cat /tmp/sysinfo/board_name                    # A1
tr '\0' '\n' < /proc/device-tree/compatible    # A2
dmesg | grep -iE 'mtdsplit|Creating MTD|squashfs'   # B1 B3
cat /proc/mtd                                  # B2 B5
ls /sys/class/net/ ; ethtool eth0 ; ethtool -i eth0  # C1 C2 C4 K1
ls /sys/class/switch/ ; dmesg | grep -iE 'rtl8367|phylink|dsa'  # C1 C5 K1
rx2lpro_netmodel ; uci show network | grep -E 'device|vlan|proto|macaddr'  # C2 C3
ls /proc/wlan* 2>/dev/null ; wifi config ; uci show wireless   # W1 W4
grep -aE 'phyBandSelect' /proc/wlan0/mib_rf                    # W3
nft list ruleset | head -20                                    # U2
ls /usr/lib/ucode/ ; which fw4 swconfig iwinfo                 # U1 U4 K2
sysupgrade -T openwrt-*-sysupgrade.bin                         # S1 S2 S3
```

## The reference board's services are inert here (verified, not assumed)

This target also carries the DIR-842/GWR1200AC board's uci-defaults and init
scripts, and they are *enabled* in an image unless something stops them. Every
one of them stops itself:

| file | gate |
|---|---|
| `etc/uci-defaults/09_wireless-dualband-dir842` | `board_name = gwr1200ac-v1` or `exit 0` |
| `etc/uci-defaults/10_wireless-5g-ifname-dir842` | same |
| `etc/uci-defaults/11_dir842-htmode-ht20` | same |
| `etc/uci-defaults/97-dir842-l2flush-enable` | same |
| `etc/init.d/dir842-asic` (START=97) | same, inside `start()` |
| `etc/init.d/dir842-l2flush` (START=98) | same |
| `etc/init.d/asic-wifi-settle` (START=99) | same, and its own comment says it is "gated to gwr1200ac-v1" |

`board_name` on this board is `tenda,rx2l-pro` (the DTS compatible's first
string, written by `/lib/preinit/02_sysinfo`), so all seven exit before touching
anything. That matters because `dir842-asic` and `dir842-l2flush` program switch
and ASIC state — a single ungated one would fight this port's own switch setup.

Checked by reading the gate in every file rather than trusting the names.
