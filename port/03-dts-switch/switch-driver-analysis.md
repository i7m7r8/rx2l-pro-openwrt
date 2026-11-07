# Switch bring-up for the RX2L Pro: swconfig `rtl8367b` vs DSA `rtl8365mb`

Scope: which driver claims **our** RTL8367-family chip, what the device tree
has to say in each case, what `realtek,init-regs` would have to contain, and
what each choice forces on `/etc/config/network`.

Two candidate drivers, both read from source we hold:

| | base A (kernel 6.18, `target/linux/rtl819x`) | base B (kernel 6.6, `target/linux/realtek`) |
|---|---|---|
| driver | `files-6.18/drivers/net/phy/rtl8367b.c` + `rtl8366_smi.c` | mainline `net/dsa/rtl8365mb.c` + `drivers/net/dsa/rtl8365mb` glue, patched by `patches-6.6/325-...add-native-rtl8367d.patch` and `328-...add-mw5-rtl8363-family.patch` |
| model | **swconfig** (`switch0`, `port@N` in uci) | **DSA** (one netdev per port: `lan1..lan4`, `wan`) |
| bus | bit-banged SMI on two gpios (`sck-gpios`, `sda-gpios`) | same, but named `mdc-gpios` / `mdio-gpios` and going through a `mdio` subnode |
| config | `CONFIG_SWCONFIG=y`, `CONFIG_RTL8367B_PHY=y` (`rtl8197f/config-6.18:208,231`) | `CONFIG_NET_DSA_REALTEK_RTL8365MB=y` (`rtl8197f/config-6.6:330`) |
| offload | PHYLINK `rtl819x-eth` + `rtl819x_hwnat.c` on `flow_offload` (857-929 Mbit/s measured on the donor board) | DSA + tc |

## 1. Chip identity: who detects what

Both drivers read the *same* two registers after unlocking with the magic
value: reg `0x1300` = chip number, `0x1301` = chip version, and the unlock
write is `0x0249` to `0x13c2`
(`rtl8367b.c:133,135,226,227,1713-1716`).

`rtl8367b_detect()` (`rtl8367b.c:1704-1777`) accepts exactly six combinations
and fails with `unknown chip (num:%04x ver:%04x)` otherwise:

| chip number | chip version | name in swconfig driver | enum |
|---|---|---|---|
| any | `0x1000` | 8367RB | `RTL8367B_CHIP_RTL8367RB` |
| any | `0x1010` | 8367R-VB | `RTL8367B_CHIP_RTL8367R_VB` |
| `0x6367` | `0x0020` | 8367RB-VB | `RTL8367B_CHIP_RTL8367RB_VB` |
| `0x6367` | `0x00A0` | 8367S | `RTL8367B_CHIP_RTL8367S` |
| `0x6642` | `0x0010` | 8367S-VB | `RTL8367B_CHIP_RTL8367S_VB` |
| `0x6642` | `0x0030` | **8367D** | `RTL8367B_CHIP_RTL8367D` |

`rtl8365mb` (base B, after patch 325/328) keys off `chip_id` with an optional
version mask, and its table contains at least:

| chip id | version | `name` | external interfaces (`extints`) |
|---|---|---|---|
| `0x6367` | any | **RTL8367C/R/RB/SB family** | extif1 → **port 6**, extif2 → port 7 |
| `0x6642` | `0x0030`, mask `0xfff0` | **RTL8367D / RTL8367RB-VC** | extif1 → **port 7** |
| `0x0801` | `0x0080` (mask `0x00f0`) / any | mw5/rtl8363 family (patch 328) | — |
| `0x6511` | `0x0080`/any | rtl8365mb-class (patch 328) | — |

**This is the single most important difference between the two routes for us.**
Our CPU trunk is port 6 (see §3). In the swconfig driver `cpu_port 6` is
understood as `RTL8367B_CPU_PORT_NUM(5) + 1` → EXTIF1 for the RB family
(`rtl8367b.c:248,990-1016`) and is simply stored for the D family. In the DSA
driver the port↔extif relationship comes from the *chip table*, and the
RTL8367D entry declares extif1 at **port 7, not port 6**. So if our chip
answers `0x6642`:

* swconfig works as-is (it does not consult a table for this);
* DSA will reject a `port@6` CPU node until someone adds a
  `RTL8367D-extint1-at-port-6` entry to `rtl8365mb_chips[]`.

Note the inconsistency this creates in base B's own trees: toby's
`rtl8197f_tenda_ac23.dts` / `_tenda_lynx.dts` claim
`compatible = "realtek,rtl8367d", ... "realtek,rtl8365mb"` **and** wire
`port@6` with `realtek,tx-port-mask = <0x40>`, while
`rtl8197f-rtl8367-smi-template.dtsi` (the RD05 reference) wires `port@7` with
mask `0x1`. Either those boards are really `0x6367` parts (most likely, since
`0x40`/port 6 is the RB-family wiring) or they lean on extra patches. Do not
resolve this by copying either one; resolve it by reading `0x1300/0x1301`
(REPORT.md item 1).

## 2. The RTL8367D "1.2 s after reset" hazard

`rtl8367b.c:31-34`:

```
 * RTL8367D: the chip does not answer on SMI until about 1.2 s after a
 * reset (a cold boot with a shorter wait fails with bus errors); the
 * vendor driver waits the same 1200 ms.
 */
#define RTL8367B_RESET_DELAY	1200	/* msecs*/
```

Two places it bites:

* `rtl8367b_probe()` (`:1813-1823`): if `reset-gpios` is present it drives
  the line active for 10 ms, releases it and **then sleeps 1200 ms** before
  any SMI traffic. Without `reset-gpios` there is no such wait, and a board
  whose switch was left in reset by the boot loader will fail detection.
* `rtl8367b_reset_chip()` (`:774-801`) polls the soft-reset bit with a
  `msleep(RTL8367B_RESET_DELAY)` first and a 10 × 1 ms poll afterwards.

Consequences for us: (a) our tree **must** declare `reset-gpios` — line 58 /
H2 is a real reset input on this SDK family and stock's U-Boot drives it; the
RX23302 board with the same wiring found the chip dead on SMI without it;
(b) first boot now costs ≥1.21 s before `switch0` appears, so any userspace
that assumes `switch0` exists at `boot()` time needs a wait loop; (c) if we
ever drive SMI from U-Boot *and* kernel, the U-Boot path is what leaves the
chip running, so a kernel cold-boot after a U-Boot `reset` needs the full
delay. The DSA driver has no equivalent long delay because the RTL8367D path
in mainline assumes the PHY-side reset, so this argument also favours the
swconfig route on a D chip.

## 3. Why the CPU port is 6, and what stock's VLAN lines decode to

From `etc_ro/diff/IPCOM_TDE/default.cfg:39-40` (identical in
`webroot_ro/default.cfg` and set by `bin/compatible.sh:73-74`), with the
vendor's own comment at `:34-38` ("portmask / tagmask are hex bitmaps, read
right to left as port0, port1 ..."):

```
lanvlan  = 1 0x15d 0x40
wanvlan1 = 2 0x142 0x40
```

| field | value | bits set | meaning |
|---|---|---|---|
| lanvlan portmask | `0x15d` | 0, 2, 3, 4, 6, 8 | LAN VLAN members |
| wanvlan1 portmask | `0x142` | 1, 6, 8 | WAN VLAN members |
| both tagmask | `0x40` | 6 | **port 6 is the untagged trunk** |

Port 8 is the SoC's own rtl865x CPU interface (`cpu2.phyid=8`); port 6 is the
external switch's trunk to the SoC (`cpu1.phyid=6`). Every VLAN line in the
file — `iptvvlan 0 0x52 0x42`, `apmode_vlan 1 0x15f 0x40`,
`maxis_internetvlan 0 0x142 0x142` — carries bit 6 or bit 1/4/6, never
anything above bit 8. So:

* **CPU = port 6**, matching `realtek,tx-port-mask = <0x40>` (TD_DP bit 6),
  `realtek,tx-dp-ext = <0x2>` (TD_DP_EXT bit 1 = EXTIF1),
  `realtek,tx-extspa = <0>`;
* **WAN = port 1** (only member of VID 2 besides the two CPU lines) —
  `wan1.phyid=1` and `eth_to_port_config.txt`'s `eth1:1` agree, and `eth1` is
  the stock WAN interface (`portal.ssidselected=eth1`);
* LAN jacks are ports 4, 3, 2 for `lan1/lan2/lan3` (`eth_to_port_config.txt`:
  `eth0:4; eth2:3; eth3:2`), with `eth_port1mask..4mask` =
  `0x151, 0x149, 0x145, 0x143` confirming exactly one jack bit each
  (bit 4, 3, 2, 1) plus the common `bit0 + bit6 + bit8`;
* **port 0 is unresolved.** It is a member of the LAN VLAN and of every
  `eth_portNmask`, but no netdev and no `lanN.phyid` maps to it
  (`lan4.phyid` is empty in `default.cfg` and is `1` — the WAN port — in the
  live `RouterCfm.cfg`, which is self-contradictory). Either a fifth jack
  exists, or the vendor always includes port 0 in the LAN mask as a spare.
  One cable-plug test settles it (REPORT.md item 4).

One caveat I cannot close offline: `tagmask = 0x40` untags only port 6, which
would leave 802.1Q tags on the frames heading to the jacks. That is almost
certainly "the vendor's own semantics for this field" (probably an
*untagged-member* list where the jacks are handled by PVID elsewhere, note
also `rtk_vlan_portPvid_set(i, WAN_VID, 0)` / `(..., LAN_VID, 0)` in the
stock kernel's SDK strings) and not something our tree reproduces — we let
the driver build the VLAN from uci. Reading `0x13xx`/`0x1bxx` VLAN registers
on stock via the debug hook (§5) would confirm the real ingress state.

## 4. `realtek,init-regs` — what it would have to be

### 4.1 What the driver does with it

`rtl8367b_init_regs()` (`:693-730`) first writes a **built-in chip table**:
`rtl8367b_initvals[]` for RB/R-VB, `rtl8367c_initvals[]` for RB-VB/S/S-VB,
and for RTL8367D `rtl8367d_chip_init()` (`:620-690`) writes `rtl8367b_initvals[]`
with one substitution (`0x13eb/0x11bb` → `0x13e0/0x0010`) plus PVID zeroing,
OCP page setup and three analog-parameter fixups. Only *then* does
`rtl8367b_init_post()` (`:732-772`) apply `realtek,init-regs`:

```c
	prop = of_get_property(np, "realtek,init-regs", &count);
	if (!prop) return 0;
	/* must be an even number of cells, each <= 0xffff,
	   at most RTL8367B_MAX_INIT_REGS = 192 pairs */
	for (i = 0; i < count; i += 2) REG_WR(smi, reg, val);
```

So `init-regs` is a **board delta over the chip's own table**, applied
unconditionally on every chip — which is precisely why a table captured for a
D chip silently corrupts an RB chip. Ordering matters too (the RX23302 file
notes the RGMII force-link writes must be contiguous "иначе связь не
поднимается").

After that, for non-D chips, `rtl8367b_extif_init_of(smi, "realtek,extif")`
requires **exactly 10 cells** or the probe fails `-EINVAL`
(`:973-1021`): `<cpu_port txdelay rxdelay mode force_mode txpause rxpause
link duplex speed>`, with `mode 1 = RTL8367_EXTIF_MODE_RGMII`
(`include/linux/rtl8367.h` enum) and `speed 2 = 1000 Mbps`. TX delay is one
bit at position 3 and RX delay 3 bits 0-7 of reg `0x1306 + id`
(`:184-190,928-944`), so for EXTIF1 (`id = 6-5 = 1`) → register `0x1307`.
For RTL8367D the driver *skips* `realtek,extif` entirely and says the board
table must bring the RGMII link up (`:1065-1085`).

### 4.2 Deriving our table from the stock data

`init-regs` cannot be derived from `lanvlan`/`wanvlan1` — those are VLAN
membership semantics, and the driver builds VLANs itself from uci via
`rtl8367b_get/set_vlan_mc` (`:1120-1400`), plus `rtl8366_init_vlan()` at
probe. What `lanvlan`/`wanvlan1` *do* give us is the port/trunk facts of §3,
which constrain exactly three things in a board table:

| what | source | register (from `rtl8367b.c` defines) |
|---|---|---|
| EXTIF1 is RGMII, not disabled | `cpu1.phyid=6`, untag bit 6 | `0x1305` DIS reg, field at `4*id` = bits 7:4, value 1 (`RTL8367B_DIS_RGMII_SHIFT(1)`, `RTL8367_EXTIF_MODE_RGMII`) |
| EXTIF1 RGMII delays | sibling RTL8367C boards: RX 5 × 300 ps, TX 0 | `0x1307`: RX bits 2:0 = 5, TX bit 3 = 0 |
| EXTIF1 forced 1000/full + pause | `rtk_port_macForceLinkExt_set(logical_cpu_port, mode, &mac_cfg)` in our U-Boot | `0x1310 + id` = `0x1311`; bits `FORCE_MODE` 12, `NWAY` 7, `TXPAUSE` 6, `RXPAUSE` 5, `LINK` 4, `DUPLEX` 2, `SPEED` 1:0 = 2 → **`0x1076`** with autoneg off (`0x10F6` if `NWAY` is set). The RX23302 board's live board table writes exactly `0x1311 0x1076`, which is the same EXTIF1 slot with the same force value: our arithmetic and their capture agree. |
| CPU tag inserted or not | `rtk_cpu_tagPort_set(logical_cpu_port, CPU_INSERT_TO_NONE)` in our U-Boot → tag OFF | the tag register (`0x121a` on the D family per RX23302's table, `0x1219/0x121a` pairs) |

Those four are the *minimum* board delta: on an RB-class chip the built-in
table already covers LED defaults, storm control, MIB control and the
analog fixups, so `init-regs` can legitimately be **empty** and the rest of
the config comes from uci `switch` sections.

On a **D-class** chip (`0x6642`/`0x0030`) the story inverts: the driver
declines to touch extifs at all, so the RGMII bring-up, the MAC↔extif
assignment and the CPU tag must be in `init-regs`. That is what the RX23302
board's ~140 pairs are: LED config (`0x1b03 0x0222`, `0x1b24/0x1b25` port
masks), MIB/ACL defaults (`0x0890-0x08a6`), storm control (`0x0a26/0x0a32-35`),
port status enable (`0x1352+`), `0x1d70 0x2000` = MAC7→EXT1,
`0x12c7/0x12cf` force-link on the extif port, `0x121a 0x0239` CPU tag with
trap-to-port-7, `0x1dad 0x97f3`, and the RGMII delay `0x1307 0x0002/0x0005`.
Their register set does **not** transplant to our port-6 wiring (e.g.
`0x1d70 BIT(13)` is "MAC7←EXT1"; on port 6 we would need the MAC6 equivalent,
which is not in their table at all).

**Decision taken in the DTS:** `realtek,init-regs` is present as a commented
derivation sketch, not as live data, in both variants. Shipping a guessed
140-pair table is the one failure mode that costs a re-flash cycle with no
console, and the honest alternative (read the registers off stock, §5) is one
command.

## 5. The one command that ends the guessing (read-only)

The stock kernel exports the SDK's register accessors as strings
`8367read`/`8367write`/`8367phyr`/`8367phyw`/`8367ocpr`/`8367ocpw`/
`8367init`/`8367snr` (and `83xxread`/`83xxwrite` aliases), with the printf
templates `rtl83xx_getAsicReg: reg= %x, data= %x` and
`port:%d,page:0x%x,regId:%d,regData:0x%x` — blob offsets
`0xa6cd10-0xa6d170` of `/tmp/work3/kernel.lzma.out`. Alongside them sit
proc-file name strings `rtl_8367_chip_type`, `rtl_8367r_vlan`,
`rtl_op_layer` (`0xa67d50-0xa67d84`) and the `/proc/rtl865x/...` prefixes
named at `0xa6d170` (`echo diag ... >/proc/rtl865x/phyReg`).

So on the stock kernel, over the serial console:

```sh
cat /proc/rtl865x/rtl_8367_chip_type          # which chip (and 8367r vlan view)
ls /proc/rtl865x/ /proc/rtl8367* 2>/dev/null  # exact entry names
echo 8367read 0x1300 > <the entry> ; dmesg | tail   # chip number  -> READ ONLY
echo 8367read 0x1301 > <the entry> ; dmesg | tail   # chip version -> READ ONLY
echo 8367read 0x1305 > ...   # extif modes
echo 8367read 0x1307 > ...   # extif1 RGMII delays (our init-regs value, in hex)
echo 8367read 0x1311 > ...   # extif1 force-link
echo 8367read 0x1d70 > ...   # MAC<->extif assignment on the D family
```

`8367read` is a read; **`8367write` must never be used** (it reprograms the
switch on a live box). The RX23302 board proved the same trick works for the
LED registers. If we capture the whole 0x0000-0x1fff range this way we get a
real `init-regs` table for our exact silicon, with no guessing.

## 6. What each route forces on userspace

swconfig (base A) — one CPU netdev from `rtl819x-eth` (stock calls it `eth0`,
IRQ 4 in `/proc/interrupts`), plus `switch0` with **8 ports (`port@0..port@7`,
`RTL8367B_NUM_PORTS = 8`, `rtl8366_smi_ops.get_vlan_mc` etc. exposed through
`swconfig`)**:

```
config switch
    option name     'switch0'
    option reset    '1'
    option enable_vlan '1'
config switch_vlan
    option device 'switch0'
    option vlan  '1'
    option ports '0t 2t 3t 4t 6u'      # lan: ports 0? 2 3 4, trunk 6 untagged
config switch_vlan
    option device 'switch0'
    option vlan  '2'
    option ports '1t 6u'               # wan
```

and `ethernet` sections use `switch0.port@N` device names. **Our current
`files/target/linux/realtek/base-files/etc/board.d/02_network` does neither**
for `rx2l-pro`: it calls `ucidef_set_interfaces_lan_wan "eth0" "eth0.1"`,
which is the *internal rtl865x VLAN* shape (eth0.1/eth0.2 exist only when the
SoC's own switch core does the tagging, as on the DIR-842/A2004 reference).
With an external RTL8367 in front, `eth0.1` is not the WAN jack unless the
SoC-side VLAN table happens to forward it. That entry needs to change to
whichever route we pick — flagged in REPORT.md for the usersland track.

DSA (base B) — `rtl8365mb` registers a dsa switch on `ethernet0` and netifd
sees **one interface per labelled port** (`lan1 lan2 lan3 lan4 wan`) created
from the `label` strings in the DT. That is why the port `label`s in the DTS
are load-bearing there and cosmetic in the swconfig route. `ucidef` becomes
`ucidef_set_interfaces_lan_wan "lan1 lan2 lan3" "wan"`, no `switch0` at all,
and the MAC-per-port rules (`ethernets`/`network.device` names) change.

## 7. Recommendation

Take **base A / swconfig** as the first target and keep the DSA file as the
fallback, for three concrete reasons:

1. The CPU trunk on port 6/EXTIF1 is native to `rtl8367b.c`
   (`RTL8367B_CPU_PORT_NUM + 1`) and *contradicted* by base B's RTL8367D
   table entry (extif1 = port 7); if `0x1300` answers `0x6642` the swconfig
   route still works, the DSA route needs a new chip-table entry.
2. Only the swconfig driver carries the 1.2 s reset quench for RTL8367D, and
   we have a real reset line.
3. Base A is the 6.18 target the whole port is aimed at, it already runs the
   same vendor WMAC HAL, and its `rtl819x_hwnat.c` is the measured
   857-929 Mbit/s offload path.

Costs to accept, both small and both buildable in CI:

* base A's `rtl8367b.c` does `#include "rtl8367.h"` and there is **no
  `rtl8367.h` next to it** (nor a Makefile/Kconfig in
  `files-6.18/drivers/net/phy/`). The partial checkout here has no
  `target/linux/generic` to supply either; base B does ship
  `target/linux/generic/files/include/linux/rtl8367.h`. When integrating,
  copy that header into `files-6.18/drivers/net/phy/` and make sure a
  Makefile/Kconfig hunk for `RTL8367B_PHY` exists, or `CONFIG_RTL8367B_PHY=y`
  in `rtl8197f/config-6.18:231` silently selects nothing.
* base A's `rtl819x-eth.c` hard-codes the donor board's topology for the
  external switch (its comments assume "the RTL8367D sits on internal port 0"
  and per-jack masks `0x08/0x10`, `.cpu_port = 8`). Port-6/EXTIF1 wiring for
  our VLAN layout is a driver-side change in the `04-eth-hwnat` track, not a
  device-tree one — the `realtek,tx-*` properties in variant A are inert
  documentation there and only base B's driver parses them.
* `switch0` appears ~1.2 s after probe begins; userspace must not assume it
  is present at `boot()`.
