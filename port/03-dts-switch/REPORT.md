# Track 03 — device tree + switch bring-up: report

Deliverables in this directory:

| file | what it is |
|---|---|
| `rtl8197f_tenda_rx2lpro.dts` | the board, for **base A** (kernel 6.18, `target/linux/rtl819x`, swconfig `rtl8367b.c`) — recommended target |
| `rtl8197f_tenda_rx2lpro_dsa66.dts` | the same board, for **base B** (kernel 6.6, `target/linux/realtek`, DSA `rtl8365mb`) |
| `switch-driver-analysis.md` | chip detection tables, `init-regs` derivation, uci consequences, the 1.2 s RTL8367D reset hazard |
| `gpio-numbering.md` | vendor `ugw_gpio`/BSP_GPIO line → `&gpioN` cell arithmetic, with the source lines |
| `dtc-check.txt` | real `cpp`+`dtc` output for both files against the real `.dtsi` includes, plus phandle decode |

Both files compile clean (`dtc` exit 0, no unresolved phandles; every warning
comes from the bases' own `.dtsi`). No builds were run on this machine, no
device was touched, no MAC/serial/PIN is written anywhere in this directory —
the CFG nvmem cells are offsets, not values.

## Facts I measured or corrected this session (all from dumps, cited inline)

* **Flash is ~16 MiB, not 8.** `/sys/class/mtd`: mtd0 `ALL` = 16711680 B
  (0xff0000), mtd3 `KernelFS` = 0xf80000 at offset 0x40000, mtd5/6/7 at
  0xfc0000/0xfd0000/0xfe0000. FACTS.md §1 "8 MiB SPI NOR" is stale.
* **RAM 128 MiB confirmed** (`MemTotal 115744 kB`), partition offsets
  confirmed decimal→hex in both DTS files.
* **The MAC is ASCII-only.** I searched the dumped Bootloader/CFG/CFM/
  CFM_BACKUP/MISC2 for the 6-byte binary form of this unit's LAN address,
  forward and reversed: zero hits. So a `mac-base` cell **cannot** express
  our LAN MAC — see "needs from elsewhere" and item 2 below.
* **`wlan0` = 5 GHz, `wlan1` = 2.4 GHz** (the reverse of the comment in
  `files/.../etc/board.d/02_network`). Three independent chains:
  `/proc/interrupts` (`5: rtk_wifi6`, `6: wlan1` — IRQ 6 is the on-SoC WMAC
  in every RTL8197F tree we hold, IRQ 5 is `pcie0`), `ifconfig` giving
  `wlan0` Interrupt:5 and the address CFG+0x5a5 (`TENDA.wlan0_mac`), and the
  live EasyMesh report calling that same address `5gMac`. Our tree's
  `wmac interrupts = <6>` therefore already matches our 2.4 GHz radio, and
  `pcie0 interrupts = <5>` matches the RTL8832BR.
* **CPU trunk is port 6 / EXTIF1**, from `lanvlan 1 0x15d 0x40` and
  `wanvlan1 2 0x142 0x40` (untagged mask bit 6) + `cpu1.phyid=6`. Not
  borrowed from a sibling.
* **Vendor GPIO lines decoded**: green LED 54, red LED 57, and a *single*
  combined Reset/WPS line 33 (`gpio_conf` uses gpio_id 33 for both
  `reset_btn` and `wps_btn`, differing only in `act_time`).
* Our U-Boot's SMI pin-mux RMW re-verified by disassembly:
  `uboot_gz.bin+0x6a68` and `+0x6c34` are `lui $v0,0x330 / and / or / sw`
  (0x03300000 ORed after masking), matching `<0x34 0x03300000 0x0ff00000>`.

## The facts that still need hardware — exact read-only commands

All of these are safe on a live stock box over the serial console: they are
reads of registers/proc/sys files. **Never** the `8367write` / `83xxwrite` /
`8367phyw` / `8367ocpw` variants, never `echo > /dev/mtd*`, never `reboot`.
Afterwards the same image can be RAM-booted (`tftpboot` + `bootm`) to watch
our own tree behave without writing flash.

### 1. Which RTL8367 is on this board (decides the whole route)

`rtl8367b.c` distinguishes six parts by registers 0x1300/0x1301; base B's DSA
table puts extif1 at port 6 for the `0x6367` family and at **port 7** for
`0x6642`, while our stock data says port 6. This single read decides
swconfig-vs-DSA, whether `realtek,init-regs` is needed at all, and whether
the DSA chip table needs a new entry.

```sh
ls /proc/rtl865x/                     # confirm entry names on our firmware
cat /proc/rtl865x/rtl_8367_chip_type  # the stock SDK's own answer
dmesg | grep -iE "switchChip|8367" | head   # boot log already printed it once
```

If the debug hook is present (the strings `8367read`,
`rtl83xx_getAsicReg: reg= %x, data= %x` are in our stock kernel at blob
+0xa6cdac/+0xa6cdc4):

```sh
for r in 1300 1301 13c2 1305 1306 1307 1310 1311 13f9 1d70; do
    echo 8367read 0x$r > /proc/rtl865x/<the entry that accepts it>; dmesg | tail -1
done
```

Reads only. The 0x1305/0x1307/0x1311 values are literally the `init-regs`
pairs our tree would need, and 0x1300/0x1301 are the detection answer.

### 2. Is `TENDA.lan_mac=` at a *stable* offset, or does it move on config rewrite

I already proved offline that no binary 6-byte MAC exists in any dump
(mtd1/2/5/6/7 searched forward and byte-reversed), so a `mac-base` cell is
out. The live question is different and it decides whether the DT cells are
load-bearing or only documentation: **CFG is a rewriteable text file, and a
`key=value` store has no reason to keep `TENDA.lan_mac=` at +0x22d forever.**
If the offset drifts when the vendor config is rewritten, the offsets in the
`nvmem-layout` must stay a hint and the grep in `board.d/02_network` remains
the only extractor.

```sh
hexdump -C /dev/mtd2 | grep -m1 lan_mac                  # left column IS the offset
dd if=/dev/mtd2 bs=1 skip=571 count=17 2>/dev/null; echo # CFG+0x23b -> the address
```

Expected: the key reports at `00022d` and the second line prints a
`xx:xx:xx:xx:xx:xx` value (do not store or print it anywhere afterwards).
Then, after the box has been used normally — the web UI rewrites CFG by
itself, so no action is needed — repeat both lines from the serial console.
Same offsets: the DT cells are safe to keep. Shifted: delete them from both
DTS files and rely on the grep.

If a stable *binary* MAC is ever found (it is not in any dump we hold), add

```
lan_mac: macaddr@<off> { compatible = "mac-base"; reg = <0x<off> 0x6>; };
```

to the CFG `nvmem-layout` and `nvmem-cells = <&lan_mac>;
nvmem-cell-names = "mac-address";` to `&ethernet` — `rtl819x-eth.c` already
calls `of_get_mac_address_nvmem()`.

### 3. What the RTL8832BR answers on PCIe (unblocks the commented `wifi@0,0`)

No dump we hold contains the device ID, so both DTS files leave the child
node commented rather than bake a guess in.

```sh
cat /sys/bus/pci/devices/*/vendor /sys/bus/pci/devices/*/device 2>/dev/null
lspci -nn 2>/dev/null                     # if pciutils exists on stock
dmesg | grep -iE "pci|rtk_wifi6|8832" | head
```

Expect one `10ec:xxxx`; that value goes into `compatible = "pci10ec,xxxx"`
and also tells the 5 GHz track which driver family to chase (mainline `rtw89`
does not know 8832).

### 4. Which jack is which switch port, and is port 0 a real jack

`lanvlan` includes bit 0 but no stock netdev maps to it, and
`lan4.phyid` is empty in `default.cfg` yet `1` (= the WAN port) in the live
config — self-contradictory. Everything downstream (uci VLAN sections, LuCI
port order, the front-panel labels) depends on this, and it is the cheapest
measurement on the list. One cable, one PC, watch carrier — no state is
changed by reading it:

```sh
# with nothing plugged:  cat /sys/class/net/eth{0,1,2,3}/carrier
# then, one jack at a time, link the jack to a switch/AP and:
dmesg | tail -3
cat /sys/class/net/eth0/carrier /sys/class/net/eth1/carrier \
    /sys/class/net/eth2/carrier /sys/class/net/eth3/carrier
```

The stock mapping to confirm or correct is `eth_to_port_config.txt`
(`eth0:4; eth1:1; eth2:3; eth3:2; vlan1:1`), i.e. WAN = port 1, LAN1/LAN2/
LAN3 = ports 4/3/2. If a fifth jack answers, that is port 0 and `port@0`
becomes `lan4` instead of "unconfirmed".

### 5. (bonus, one line) Which mux field releases port E1 — unblocks the button

```sh
md 0xb8000030 4 1        # in U-Boot, or: devmem 0xB8000034 32 on stock
```

Read it once before stock's `gpio_conf` runs and once after; any field that
moved is the E1 (line 33) function select the reset/WPS button needs. Until
then `keys`/`leds` stay `status = "disabled"` in both files, which is the
safe state: line 58 (H2) is the switch's `~RESET` and is two bits away from
57 on the same bank register.

## Needs from elsewhere (proposed, not applied)

1. **`base-files/etc/board.d/02_network` — `rx2l-pro` case.** Today it calls
   `ucidef_set_interfaces_lan_wan "eth0" "eth0.1"`. That is the *internal*
   rtl865x VLAN shape; with an external RTL8367 in front, `eth0.1` is not our
   WAN jack. Under the recommended swconfig route it should become a
   `switch0`/`port@N` uci definition (`switch_vlan` for VID 1 = ports
   `0?t 2t 3t 4t 6u` and VID 2 = `1t 6u`, derived in
   `switch-driver-analysis.md` §6); under DSA it should become
   `"lan1 lan2 lan3" "wan"`. The LAN/WAN MAC reads in that file stay as they
   are — they are correct, and are the only working extractor (§2).
2. **`02_network` comment fix**: swap the `wlan0_mac`/`wlan1_mac` band
   attribution (see the correction above) so the 2.4 GHz/5 GHz ifname
   defaults in `uci-defaults/12_wireless-2g-rx2lpro` line up.
3. **Build blocker for the swconfig route (`01-base-build`)**: base A ships
   `drivers/net/phy/{rtl8367b.c,rtl8366_smi.c}` with **no Makefile/Kconfig in
   that directory**, `rtl8367b.c` does `#include "rtl8367.h"` and no such
   header is in the tree, and `CONFIG_RTL8367B_PHY=y` is set in
   `rtl8197f/config-6.18:231`. Base B carries the header at
   `target/linux/generic/files/include/linux/rtl8367.h`. On integration, copy
   that header next to `rtl8367b.c` and make sure the `RTL8367B_PHY`
   Kconfig/Makefile hunks exist, or the config line selects nothing and the
   switch silently stays absent.
4. **`04-eth-hwnat`**: base A's `rtl819x_eth.c` hard-codes the donor board's
   topology for the external switch (its comments assume the RTL8367 hangs on
   internal port 0 with per-jack masks 0x08/0x10, `.cpu_port = 8`). Our port
   6/EXTIF1 wiring needs a driver-side change; the `realtek,tx-port-mask`/
   `tx-dp-ext`/`tx-extspa` properties in variant A are deliberately inert
   documentation there (that driver does not parse them) and are live only in
   variant B.
5. **`02-image-container`**: variant A keeps the stock label `KernelFS`;
   variant B uses `firmware` so OpenWrt's name-matched mtdsplit runs. Pick one
   naming and make the partition label, `platform.sh` sysupgrade support and
   the splitter agree — do not add a `compatible` to that partition node, it
   suppresses the split. Also re-decide `spi-max-frequency` (I used 15 MHz,
   the value proven on the identical `spi-sheipa` divider in base A; our own
   4.14 tree says 41 MHz).

## Update: the switch driver was not merely unwired, it was unbuildable

Item 3 above said "copy that header next to rtl8367b.c and make sure the
RTL8367B_PHY Kconfig/Makefile hunks exist". Measured against the pinned overlay
tree, the hole was wider than the header:

* `files-6.18/drivers/net/phy/` ships `rtl8367b.c`, `rtl8366_smi.c`,
  `rtl8366_smi.h` and **no header, no Makefile, no Kconfig**;
* nothing in `patches-6.18/` mentions `rtl8367b` at all, so no patch adds the
  `obj-$(CONFIG_RTL8367B_PHY)` line or a Kconfig entry;
* `CONFIG_RTL8367B_PHY=y` sits in `rtl8197f/config-6.18`, but an unknown symbol
  is dropped by kconfig without an error.

Net effect: the switch driver was **never compiled** on this base. The three
source files were copied into the kernel tree and ignored, the config line
selected nothing, and the RTL8367RB would come up in its power-on state -- the
"dead Ethernet" symptom, not a missing feature.

Fixed in this tree, additively (nothing under the platform overlay is edited):

| path | what it supplies |
|---|---|
| `files/target/linux/rtl819x/files-6.18/drivers/net/phy/rtl8367.h` | the header `rtl8367b.c` quote-includes; types/enums only, taken from an OpenWrt-lineage tree (GPL-2.0, `_RTL8367_H`) |
| `files/target/linux/rtl819x/patches-6.18/999-net-phy-wire-rtl8367b-switch-driver.patch` | `obj-$(CONFIG_RTL8367B_PHY) += rtl8367b.o rtl8366_smi.o` plus the Kconfig entry that makes the symbol exist |
| `rtl8197f/config-6.18` | drops `CONFIG_RTL8366_SMI{,_DEBUG_FS}`, which no Kconfig defines either; the objects ride on the RTL8367B_PHY line |

The patch's context is the real `drivers/net/phy/{Makefile,Kconfig}` of Linux
6.18 (the pinned `LINUX_VERSION-6.18 = .41`) and it dry-runs clean against them.
`CONFIG_SWCONFIG` (the Kconfig dependency) is a genuine symbol here -- OpenWrt's
`generic/hack-6.18/700-swconfig_switch_drivers.patch` defines it -- so the chain
is SWCONFIG=y -> RTL8367B_PHY available -> objects built.

Still open, and unchanged by this: the port map itself (items 4 and 5 above), so
`02_network` still does not emit a `switch0` stanza. First boot must therefore be
treated as "switch in power-on state": reachability via the 2.4 GHz AP or serial,
not via a presumed LAN jack.
