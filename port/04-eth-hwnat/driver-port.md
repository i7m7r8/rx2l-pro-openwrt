# driver-port.md — 4.14/4.4 `rtl819x-eth` → OpenWrt 6.18 (`target/linux/rtl819x`)

Scope: the RTL8197F **SoC CPU Ethernet** (`ethernet@18010000`,
`compatible = "realtek,rtl8197f-eth"`) + the **external RTL8367RB** it hangs off,
and the hardware-NAT engine that sits behind both. Not Wi-Fi.

Every claim below is tagged:

* **PROVEN** — read out of a tree we have locally, or out of a stock dump, with a
  path quoted.
* **OUR-BOARD** — derived from RX2L Pro evidence (FACTS.md §5, `03-dts-switch`).
* **SPECULATIVE** — plausible, needs one on-device write to confirm.

---

## 0. The three candidate eth designs, and which one wins

| | tree | driver shape | external switch | hwNAT | measured |
|---|---|---|---|---|---|
| **A** adopted | `/tmp/rebase/openwrt-rx23302/.../target/linux/rtl819x/` (kernel 6.18.41) | `files-6.18/drivers/net/ethernet/{rtl819x_eth.c (2124 l), rtl819x_hwnat.c (664 l), rtl865x_asichal.c (979 l), rtl819x_regs.h, rtl819x_eth.h}` — own swNic descriptor DMA engine, internal rtl865x switch core, `.cpu_port = 8` | **swconfig** `drivers/net/phy/rtl8367b.c` + `rtl8366_smi.c`, `realtek,cpu-port = <6>` | `rtl819x_hwnat.c` on upstream `<net/flow_offload.h>` + `TC_SETUP_FT` + `flow_indr_block_cb_alloc()` — i.e. the **stock nftables flowtable offload path**, not a private netlink | 857–929 Mbit/s NAT at ~0 % CPU (`05_enable_flow_offloading`: "замерено 926–929 Мбит/с, CPU 90–100% idle"); software flowtable alone 850–880 Mbit/s |
| B rejected | `/tmp/rebase-base` (6.6.114, `realtek` target) | `files-6.6/drivers/net/ethernet/rtl8197f_rtknet.c` (>10 k lines), DSA + `rtl8_4` CPU tags | DSA `net/dsa/rtl8367{,5mb}` | **locked off by policy** — `rtl8197f/ACCELERATION.md`: "Ab Treiberversion `1.8.40-strict-firewall`… Es gibt keinen Schalter `strict=0`", `rtl8197f_hw_napt_enable=1` is *rejected*, private proc commands `napt-on` refused, own flowtable bindings return `-EOPNOTSUPP` | none (that is the point of that branch) |
| C rejected | `/tmp/rebase/iptime-a2004mu-openwrt` (6.18.36) | `rtl8197f_eth.c` with NAT inside the driver, DSA-tagged path | **DSA `realtek,rtl8365mb`** (upstream) | standard `flags offload` flowtable, paired IPv4 SNAT only, **ceiling 64 active flows**; PPPoE/DNAT/IPv6 stay in software | functional only — `docs/performance.md` explicitly refuses a throughput claim: "No gigabit routing, hardware-NAT throughput, or LAN-switching throughput is claimed" |

**Adopt A.** Reasons that matter for *this* board, not taste:

1. **Port 6.** `03-dts-switch` chose base A because upstream `rtl8365mb` (C's switch
   driver, and B's) maps extif1 to **port 7**, contradicting our `0x40`/`cpu1.phyid=6`
   evidence. swconfig's `rtl8367b.c` takes `realtek,cpu-port = <6>` literally and also
   carries the RTL8367D ~1.2 s post-reset SMI delay (FACTS §5).
2. **It is the only tree with a working, measured hwNAT on 6.18.** B forbids it; C has
   it but publishes no throughput and a 64-flow cap.
3. **A's bindings are already identical to our 4.14 tree** (`realtek,rtl8197f-eth`,
   `realtek,sheipa-spi`, `realtek,rtl8196b-pci`) → our written DTS drops in.
4. A's offload glue is the *mainline* one, so it survives firewall4 upgrades instead of
   being a private netlink protocol (see `offload-package.md`).

---

## 1. Delete / adopt / re-express

### 1.1 Deleted outright (base A replaces them)

Our path → base A path:

| ours (`files/target/linux/realtek/…`) | verdict | replacement in base A |
|---|---|---|
| `files-4.14/drivers/net/ethernet/rtl819x/rtl819x-eth.c` | **delete** | `files-6.18/drivers/net/ethernet/rtl819x_eth.c` (note **hyphen → underscore**) |
| `…/rtl819x_swnic.c`, `…/rtl819x_swnic.h` | **delete, no counterpart** | the vendor swNic HAL layer is folded into `rtl819x_eth.c`; base A's object list is `rtl819x-y := rtl819x_eth.o rtl865x_asichal.o rtl819x_hwnat.o` (`patches-6.18/720-net-ethernet-add-rtl819x.patch`) — **three objects, not four** |
| `…/rtl819x_hwnat.c/.h` | **delete** | `rtl819x_hwnat.c/.h` (6.18 `flow_offload` rewrite; header states the 4.14 donor used "downstream `ndo_flow_offload` и ручное удержание таймаутов") |
| `…/rtl865x_asichal.c/.h` | **delete** | same filename, 6.18 version |
| `…/rtl819x_regs.h` | **delete** | 6.18 version (77 lines) |
| `…/Kconfig`, `…/Makefile` | **delete** | supplied as Kconfig/Makefile **hunks inside patch 720** (`config NET_RTL819X` is `tristate` there, `depends on REALTEK`, `select PHYLINK`) |
| `patches-4.14/0006-net-add-rtl819x-eth.patch` | **delete** | `patches-6.18/720-net-ethernet-add-rtl819x.patch` |
| `files-4.14/include/net/*` (4.14 back-ports for the eth stack) | **delete** | unnecessary on 6.18 |

`NET_RTL819X` is already `=y` in `port/01-base-build`'s `rtl8197f/config-6.18`
(line 225). Our 4.14 Kconfig deliberately made it `bool` (two `init_module` in one
multi-object module + proc entries never removed). Base A's `tristate` is fine on 6.18
because `rtl819x_hwnat.c` uses `late_initcall()` and both `rtl819x_eth.c` and
`rtl865x_asichal.c` use `module_platform_driver()`/`late_initcall` consistently —
but keep `=y` regardless; the `/proc` entries (`proc_create("rtl819x_router"…)`,
`proc_create("rtl865x_gw"…)`) are never removed on exit and a modular build would leave
dangling proc files on `rmmod`. **PROVEN** (both facts read from the sources).

### 1.2 Behaviours that must be re-expressed (they do not come free with the driver)

Our `base-files` carry four services with hard-won boot ordering. Base A has an
equivalent pair. Mapping:

| our 4.14 behaviour (`files/target/linux/realtek/base-files/`) | what it actually does | where it goes in base A |
|---|---|---|
| `etc/init.d/dir842-asic` step 0 `wait_netif_mac` — `dd if=/dev/mtd1 bs=1 count=17` then `macaddr_add … 1`, poll `/sys/class/net/eth0.2/address` | ASIC-vs-Linux netif MAC mismatch blackholes the datapath, so `gw_prog` must run only **after** the real flash MAC is on the interface | **re-express, not copy**: `/dev/mtd1` is D-Link geometry; on us mtd1 = vendor `Bootloader` and the LAN MAC is **ASCII `TENDA.lan_mac=` inside `CFG`** → there is no byte-offset MAC and no nvmem cell (`03-dts-switch` states `mac-base` is impossible; the extractor lives in `etc/board.d/02_network`). Our version polls `eth0`/`eth0.8` for the MAC that netifd applied, with the *expected value supplied by 02_network*, then runs `cat /proc/rtl865x_gw`. See patch `003-…mac-from-userspace`. **OUR-BOARD** |
| step 1 `swconfig dev switch0 set apply 1` | clears a wedged trunk→jack forwarding state that survives warm reloads (`rtl8367b_reset_chip()` deliberately skips the 8367S soft reset to keep the loader's RGMII uplink) | **keep verbatim** — base A uses the *same* `rtl8367b.c`; put it first in our gate service. **PROVEN** that the file exists in base A (`drivers/net/phy/rtl8367b.c`), **SPECULATIVE** that our RB has the same wedge (same driver family) |
| step 2 `echo 3 > /sys/module/rtl819x/parameters/fabric_reset` — "MEASURED-REQUIRED: without it a fresh boot forwards NOTHING" | engine reinit + CPUICR soft-reset + full fabric reset (SIRR FULL_RST → swcore clock cycle → MEMCR SRAM re-init → 153-reg restore) | **NO COUNTERPART.** Base A's `rtl819x` module parameters are only `hwnat`, `hwnat_debug`, `l2_mask_lan_default`, `swtcr1_override`, `sel_cpu_reason`, `swtcr1_trap_bits`, `napt_collision_prefill`, `ffcr_unkuc_to_cpu`, `multiport_mode`, `wan_route_mode`, `l2_mask_lan` — there is no `fabric_reset`, no `fabric_autoreset`, no `fabric_gw_rearm`. Base A instead replays a **hand-captured register block** at probe: `rtl819x_intsw_nastroit()` writes `0xBB804004=0x8500FFFF, 0xBB804100=1, 0xBB80414C=0x02037D57, 0xBB804204=1, 0xBB804234=0x03007F7F, 0xBB804300=0x00200000, 0xBB804428=3, 0xBB804754=0x1249` + `PCRP{0..8}`, "Значения СНЯТЫ С ЖИВОГО ЗАГРУЗЧИКА" (taken from a live bootloader, diffed against our kernel snapshot: 16 of 960 words differed). **That replay is the 6.18 expression of the same need — and it is donor-board data, so it must be re-captured from *our* mtd1** (see §5, needs-from-elsewhere). |
| step 3 `cat /proc/rtl865x_gw` (`gw_prog`) — programs netifs, routes, L2 nexthops, ACL ranges; **WIPES the ASIC L2 tables** | same proc node exists in base A (`rtl865x_asichal.c:971`) | **keep, same order** (before the warm). Base A's `rtl819x-hwnat-gate` already does `cat /proc/rtl865x_gw` after `echo 1 > /proc/rtl819x_router`. |
| step 4 `asic_warm` — a few box-originated pings to prime cold L2/ARP rows ("freshly booted box reads as 100 % packet loss") | pure userspace | **keep verbatim**; nothing in base A does this. |
| step 5 `echo 1 > …/hwnat` **last, strictly after the warm** ("arming against cold tables kills the datapath outright") | same parameter name in base A (`module_param_named(hwnat, …, bool, 0644)`, **default 0**) | **keep the ordering rule.** Base A's gate turns `router` on, then `hwnat`, then `gw` — same relative order, but note base A arms `hwnat` *before* `gw_prog` and has **no warm step at all**. Adopt our warm step. |
| role gate `asic_role()` (router vs bridge; bridge must NOT freeze L2 aging) | `TEACR bit0` freeze is set by `gw_prog` | base A's `is_router()` in `rtl819x-hwnat-gate` is the same idea, stricter (also rejects `wan.disabled=1` and `wan.device=br-lan`). **Adopt `is_router()`**; keep our explicit `dir842.asic.role`-style UCI override (`rtl819x.hwmode` or reuse `firewall.@defaults[0].flow_offloading_hw`). |
| `etc/init.d/dir842-l2flush` — poll both radios, `echo <mac> > /proc/rtl865x_l2flush` every 2 s | stale hardware-learned L2 row points a roaming client at the jack and source-port filtering silently drops it | **port as-is**: base A ships the same proc node (`rtl865x_asichal.c:975` `"rtl865x_l2flush"`). Keep the two fixes the donor script records: run it in **both** roles (router role is where aging is frozen ⇒ the row is *permanent*), and **flush unconditionally every cycle** (flush-on-arrival misses a fast roam away-and-back). Keep globbing `/proc/*/sta_info` for the vendor radio instead of hardcoding `wlan0` — base A's `09_wireless…` history (vendor ifname flipped once when `rtl8192cd` gained AUTOLOAD) applies verbatim to `rtl8192fe`. |
| `etc/init.d/asic-wifi-settle` — re-run the whole bring-up ~20 s after **both** radios are bridged | the boot pass programs L2 before wireless settles; `restart` by hand fixes it every time | **no counterpart in base A — port it.** Base A has the same `gw_prog`-wipes-L2 property, so the same race exists. Gate on router role, exactly as ours. |
| `etc/sysctl.d/30-hwnat.conf` → `net.netfilter.nf_conntrack_acct=1` | with hwNAT a flow's packets never reach the CPU, so per-conntrack counters **freeze while iperf3 still reports throughput** — the cheapest direct proof the datapath moved into silicon | **keep** (see acceptance tests in `offload-package.md`). |
| `etc/sysctl.d/31-dir842-lowmem.conf`, `board.d/01_leds`, `lib/realtek.sh` board gate, `99-dir842-m5` | out of track | not eth; `realtek_board_detect()` must match **our** DTS `model` string or `board_name` stays `generic`, `02_network`'s case never runs, and the result is indistinguishable from "no ethernet". Flagged for `07-usersland`. |
| **every `dir842-*` script's `[ "$(cat /tmp/sysinfo/board_name)" = "gwr1200ac-v1" ] || return 0` board gate** | these scripts are DIR-842 R1 code shipped disabled | **delete the gates** and re-target the files to `tenda,rx2l-pro`; base A's own scripts gate on `electra,rx23302` (`05_enable_flow_offloading`, `06_switch_vlan_ids`) → **change that literal to our board name in both**, otherwise offload is silently never enabled on our image. **PROVEN** (string read in those files). |

### 1.3 The `USE_PROCD` trap (still true, different reason)

Our `dir842-asic` header warns that this fork's `rc.common` tests `[ -n "$USE_PROCD" ]`,
so *any* value — including `0` — silently replaces `start()` with an empty procd
service. Base A's two services are written the other way: `USE_PROCD=1` with
`start_service()`/`reload_service()`/`service_triggers()` and
`procd_add_reload_trigger firewall`, plus `START=17` (must run **before** firewall's 19)
and `START=95` (must run **after** network's 20). Adopt base A's form for those two
files and keep the legacy non-`USE_PROCD` form for `l2flush`/`asic-wifi-settle` (they
are loops/timers, not services). Do not mix: `START` ordering is load-bearing.

---

## 2. What the CPU trunk actually is on RTL8197F — and the DP-bit trap

This is the section that decides whether "high performance" is real or whether the box
silently blackholes. Read it before copying any number out of a neighbour's DTS.

### 2.1 Three *different* port namespaces

A TX frame from the CPU crosses **three** selectors, and a "port 6" fact means
something different in each:

1. **Descriptor `TD_DP`** — `rtl819x_tx_desc.opts4` bits 24..30
   (`RTL819X_TX_DP_SHIFT = 24`, `MASK = GENMASK(30,24)`). A 7-bit
   **destination port mask in the SoC's own port list**: internal switch ports
   `0..8`, CPU = **8** (`.cpu_port = 8`, `rtl819x_eth.c:1982`).
   `RTL819X_TX_DP_ALL = 0x3f`. CPU is *not* in the mask ("frame came from there, no
   point sending it back").
2. **`TD_DP_EXT`** — a separate extension **destination selector** (4 bits; visible on
   the RX side as `opts2: … qid(3) dp_ext(4) extspa(2)`, and dumped by the hwNAT
   rule proc as `dp %02x dpext %x dpc %u`). Per base B's binding
   `files-6.6/Documentation/devicetree/bindings/net/realtek,rtl8197f-rtknet.yaml:115`:
   *"It is meaningful only when a SoC design intentionally targets the internal
   extension destination (normally tx-port-mask BIT(6)). It must be **zero** for the
   RD05 RTL8197F-P0 to RTL8367D-EXT1 RGMII cascade."*
3. **`EXTSPA`** — `opts5` bits 30..31, **source** extension port number, 2 bits.
   Vendor header (`rtl819x_swNic.h:129`, quoted verbatim in `rtl819x_eth.h:216`):
   *`[31:30] extspa: Source extension port number: 2'd0: from CPU, 2'd1~2'd3: from
   extension port numbers`*.

### 2.2 The trap, stated plainly

**`BIT(6)` in `TD_DP` is NOT "the trunk is port 6 of the external switch".**
It is "send to the SoC's internal extension destination #6", which only pairs with
`tx-dp-ext` as a selector. Meanwhile the `0x40` in our stock
`lanvlan = 1 0x15d 0x40` / `wanvlan1 = 2 0x142 0x40` is the **RTL8367RB's untagged
member mask** — port 6 = EXTIF1 of the external chip — and `cpu1.phyid=6` in
`RouterCfm.cfg` says the same. Two independent facts that both spell `6`, in two
different chips, reached by two different registers. The boards that "prove" the triple
(toby's `rtl8197f_tenda_ac23.dts:223-225` = `0x40 / 0x2 / 0x0`) were written for the
**base-B `rtl8197f_rtknet.c`** driver, which is the only driver that reads them
(`rtl8197f_rtknet.c:10223`, `rtl8197f_rtk_read_u32_prop(np, "realtek,tx-port-mask", …)`
plus `tx-dp-ext`, `tx-extspa`). Compare `xiaomi_r4-rd05.dts:359-361` =
`0x1 / 0x0 / 0x0` for the same physical chip family: same topology, *different triple*.
Copying numbers across that line is how you get a board whose switch pins are fine and
whose datapath is dead.

**Second, worse trap — `extspa` is the SOURCE, and the vendor constant lies to you.**
`PKTHDR_EXTPORT_LIST_CPU` (`common/mbuf.h:121`) is `3`. That is an index in the SDK's
*programmatic port list*, used for `dp_ext` masks; it is **not** the hardware
source-port field. Writing `3` into `EXTSPA` tells the switch "this frame arrived from
extension port 3", so the switch does not treat the CPU as source and **hands the frame
straight back to us**. Base A measured it (`rtl819x_eth.h:221-230`): 30 frames sent,
exactly those 30 received back, 0 at the peer. Correct value for CPU-originated traffic
is **0**, and that is the one value in the triple that is design-independent:
`RTL819X_TX_EXTSPA_CPU 0`, unconditional at `rtl819x_eth.c:1797`.

### 2.3 The values for OUR board, and where they must be enforced

| quantity | value | status | enforced by |
|---|---|---|---|
| RTL8367RB CPU/trunk port | **6** (= EXTIF1) | OUR-BOARD (`lanvlan 1 0x15d **0x40**`, `wanvlan1 2 0x142 0x40`, `cpu1.phyid=6`) | `realtek,cpu-port = <6>` in the `switch0` node of `port/03-dts-switch/rtl8197f_tenda_rx2lpro.dts:274` → consumed by `rtl8367b.c` (its `RTL8367B_CPU_PORT_NUM` is 5, so 6 selects EXTIF1) |
| RGMII trunk config | `realtek,extif = <6 0 5 1 1 1 1 1 2>` (order `cpu txdelay rxdelay mode force txpause rxpause link duplex speed`; mode 1 = RGMII, speed 2 = 1000M; RX field is 300 ps/step ⇒ `5` = 1500 ps; TX delay 0) | OUR-BOARD, from `rtl8367b_extif_init_of()` requirements (mandatory, exactly 10 cells, `-EINVAL` otherwise, except on RTL8367D) | swconfig driver; see `phylink-notes.md` |
| CPU TX tag | **none** — stock U-Boot ran `rtk_cpu_tagPort_set(…, CPU_INSERT_TO_NONE)` | OUR-BOARD (03-dts comment, `realtek,init-regs` intentionally absent) | means the SoC↔switch link is plain 802.1Q, which is exactly base A's **v6.6 default** (`P0GMIICR 0xBB80414C = 0x02037D57`: RX tag-strip bit 25 on, TX tag bit 26 **off**) — a lucky agreement, see below |
| `realtek,tx-port-mask` / `tx-dp-ext` / `tx-extspa` in the DT | `0x40` / `0x2` / `0` — **inert documentation** under base A | PROVEN: `rtl819x_eth.c` contains **no `of_property_read_*` at all** (only `of_get_mac_address_nvmem` + `of_get_ethdev_address`) | documented in `rtl8197f_tenda_rx2lpro.dts:363-381`; harmless (unknown properties ignored) |
| what actually goes in `opts4` (`TD_DP`) on the non-router path | **`0x01`** for both WAN and LAN = internal port 0 = the RGMII wire to the RTL8367RB; the *jacks* are chosen downstream by the RB's VLAN table from the 802.1Q VID | PROVEN (base A): `RTL_8367_WAN_GNEZDA 0x01`, `RTL_8367_LAN_GNEZDA 0x01`, `rtl819x_tx_gnezda()` | `rtl819x_eth.c:930-931`, runtime-adjustable via `echo "<wan> <lan>" > /proc/rtl819x_gnezda` |
| what actually goes in `opts3` | the **VID** (8 = WAN, 9 = LAN) in router mode, else `RTL_NASH_VID` = 9 | PROVEN | `rtl819x_eth.c:1751` |
| `opts5` `EXTSPA` | **0** (CPU) | PROVEN | `rtl819x_eth.c:1797` |
| router-mode jack masks | donor: `RTL_ROUTER_WAN_DP = 0x01` (jack 0), `RTL_ROUTER_LAN_DP = 0x18` (jacks 3,4). Ours would be **WAN `0x02`** (port 1) and **LAN `0x1C`** (ports 2,3,4) | **SPECULATIVE** — only reachable if `MACCR1.PORT0_ROUTER_MODE` works here, and base A says it does not on their silicon: "Вендор ставит бит 26 только вместе с MACCR1.PORT0_ROUTER_MODE, **у нас он не заводится**" | `rtl819x_eth.c:961-962`; patch `001` parameterises them |

**Why 0x01 is the right compiled-in mask for us too.** In router mode the 8197F is
supposed to see the RB's jacks as its *own* ports 0..4 — that is the mode where
`dp` becomes a jack mask and where `0x18` vs `0x1C` matters. In the tag-less v6.6
default, `dp` is only "which internal port" and there is exactly one cable, so
`0x01` is correct regardless of how many jacks the external chip has. Our board differs
from the donor in **jack count and numbering**, which is a VLAN-table fact, not a
descriptor fact. Base A's own reversal of the hypothesis is the proof
(`rtl819x_eth.c:925-929`): *"v6.6: гипотеза выше НЕВЕРНА. dp — порты ВНУТРЕННЕГО свитча
8197F (0x18 = внутр. 3,4 — никуда не разведены, LAN был мёртв)"*, and the earlier loop
(`rtl819x_eth.c:909-921`) documents exactly what a wrong mask looks like: `0x1F` flooded
LAN traffic into the blue WAN jack and produced a broadcast loop ("received packet with
own address", DHCP never reached clients). Keep the tag off; do not chase router mode
until first boot is green.

---

## 3. VID 9/8 vs 1/2 — where the fix lives (the "dead jacks" blocker)

`03-dts-switch` flagged that base A's eth side hardcodes VID 9/8 while swconfig generates
1/2. Reconciliation, with the exact locations:

* **The numbers are not cosmetic.** `rtl819x_intsw_nastroit()` writes VLAN *table
  entries* for the internal switch keyed on VID: `rtl819x_intsw_zapis_vlan(RTL_NASH_VID,
  RTL_LAN_PORTY, RTL_LAN_PORTY, 0)` and `rtl819x_intsw_zapis_vlan(RTL_WAN_VID, 0x101,
  0x101, 1)` (members = port 0 + CPU port 8, FID 1 so WAN and LAN MACs never mix), plus
  `rtl819x_intsw_pvid(port, RTL_NASH_VID)` for ports 0..4 and 8. TX puts the VID in
  `opts3`. **A frame tagged 1 or 2 matches no entry and is dropped by the internal
  switch** — `rtl819x_eth.c:1612`: "Без этой строки таблицы внутренний коммутатор кадры
  с меткой 8 выбрасывает: проверено в ОЗУ, eth0.8 молчал, пока группы не было."
* **Single fix location: `target/linux/rtl819x/base-files/etc/uci-defaults/06_switch_vlan_ids`.**
  That file is the reconciliation: it renumbers `network.switch_vlan.vlan` 1→9 and 2→8,
  sets `fid='1'` on the WAN vlan, swaps `br-lan.ports` `eth0.1`→`eth0.9`, and repoints
  `network.wan.device`/`wan6.device` to `eth0.8`. It runs once, only against the
  untouched generated config (it gates on `network.lan.device == br-lan` **and**
  `network.wan.device == eth0.2`).
  **Two edits required for us, both mandatory:**
  1. board gate `electra,rx23302` → our board name, or the file exits 0 and the jacks
     stay dead.
  2. the `eth0.1`/`eth0.2` preconditions and the port lists are the RX23302 shape.
     Ours is **1 WAN + 3 LAN on the external chip** (stock `eth_to_port_config.txt`
     `eth0:4 eth1:1 eth2:3 eth3:2` + `eth_name_conf.txt` `wan_1:eth1` ⇒ WAN = RB port 1,
     LAN = 2/3/4, CPU = 6). Our `02_network` must generate `switch_vlan` with those
     members (`03-dts-switch` proposes VID 1 = `0?t 2t 3t 4t 6u`… — the *port* set is
     ours, the *renumber* to 9/8 is base A's) and `06_switch_vlan_ids` must match the
     names it expects, or the renumber silently no-ops.
* **Because one VID table drives both chips**, the renumber also makes the **RTL8367RB**
  use 9/8: swconfig hands `rtl8367b.c` whatever `switch_vlan.vlan` says. That is the
  whole trick — the internal switch and the external switch agree because there is only
  one input.
* `RTL_LAN_PORTY = 0x11F` (internal ports 0-4 + CPU 8) needs no change: internal port
  numbering is a property of the 8197F, not of the board.

---

## 4. TX-descriptor / DMA pitfalls to carry forward (from `03-dts-switch`'s "measured
pitfalls" sources and base A's own comments)

1. **Length field position.** TX `mlen` lives in `opts2` starting at **bit 15**
   (`RTL819X_TX_MLEN_SHIFT = 15`), and must include `ETH_FCS_LEN`. Writing the length
   into the low bits of `opts2` collides with `vlantag`/queue numbers and
   "превращает кадр в мусор с нулевым размером, и свитч молча его выбрасывает".
   (`rtl819x_eth.c:1735-1743`)
2. **`hwlkup` (hardware MAC lookup) must stay OFF** on this design. Base A removed it
   30.08 after measuring: with it, *multicast* never egressed and ARP failed in both
   directions while unicast group traffic worked, because `ff:ff:ff:ff:ff:ff` matches no
   FDB row and the bootloader's `FFCR EnUnkMC2CPU` sent unknown-group frames back to the
   CPU instead of flooding them. (`rtl819x_eth.c:1761-1780`)
3. **Descriptor rings need their limit in `DMA_CR1`** — the byte offset of the last
   descriptor — "иначе железо забирает дескрипторы и не возвращает"
   (`rtl819x_eth.h`, section «Управление кольцами»). Ring count in silicon: 6 RX, 2 TX;
   base A uses `rx_rings = 1` (deliberately, "для начала"), `tx_rings = 2` with ring 1
   allocated-and-enabled but unused — an **enabled ring with no base address hangs TX**.
4. **256-byte descriptor alignment is a hardware requirement**, not an optimisation
   (`RTL819X_DESC_ALIGN`).
5. **Bus-endian bit in CPUICR1.** `RTL819X_CF_NIC_LITTLE_ENDIAN` must be set or every
   4-byte word arrives byte-swapped: `3c:97:0e:c7:0d:f5` reads as `c7:0e:97:3c … f5:0d`,
   the destination stops being broadcast, the kernel marks the frame foreign
   (`pkt_type=3`), the bridge learns addresses and delivers **nothing upward**, and
   **no errors are visible** because the checksum is computed before DMA and agrees.
   Verified on live board both ways. (`rtl819x_eth.c:1964-1980`) — this is the single
   most diagnostic fact in the whole track: a "bridge learning MACs but eth0 counters at
   zero, no error counters" signature is *this bit*, not phylink, not VLANs.
   On MIPS little-endian (FACTS §2 — settled) this bit is mandatory; a BE build dies
   here first.
6. **TX buffer length must be padded to `ETH_ZLEN` via `skb_padto()` before DMA** or the
   controller reads past the skb into foreign memory (`rtl819x_eth.c:1697-1709`).
7. From the 6.6 tree (`rtl8197f/RD05_TX_DMA_AB.md`): they had valid-looking
   PH_LEN/M_LEN (+4 FCS) frames with an **empty external PCAP and rising CPU-port FCS
   errors**, and had to add a `rd05_tx_coherent` A/B switch because "the trace copied
   bytes before streaming DMA mapping and does not establish the actual DMA fetch
   contents". Treat streaming-vs-coherent TX buffering as a *known open variable* on
   this SoC: if our frames appear in the driver's own counters but never on the wire and
   FCS errors climb on the switch CPU port, run the A/B, don't chase RGMII delays.

---

## 5. Needs generated by this file

* Re-capture the **bootloader register snapshot** of `0xBB804000..0xBB804F00`,
  `PCRP/PSRP/PVCR` and the RB's own VLAN/EXTIF tables **from our mtd1** (or read-only
  from the live stock kernel via `/proc/rtl865x/*`) and diff against base A's compiled-in
  constants. Until then `rtl819x_intsw_nastroit()` is donor data on our board — this is
  the largest single risk in the track.
* Decide (image track) whether `realtek,init-regs` can be captured: `03-dts-switch` says
  our silicon is still not identified (RB vs D) and applying an 8367D table to an RB
  board corrupts it.
* `rtl8367b.c` build wiring: `03-dts-switch` proved base A ships
  `drivers/net/phy/{rtl8367b.c,rtl8366_smi.c}` with **no Makefile/Kconfig** and an
  unsatisfied `#include "rtl8367.h"` (the header exists only in
  `target/linux/generic/files/include/linux/rtl8367.h` in base B). My eth work does
  **not** assume that tree builds as-is — see `patches/README.md` for the wiring I add.
