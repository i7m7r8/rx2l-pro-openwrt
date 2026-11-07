# phylink-notes.md — validating the CPU trunk (RGMII) at boot

## 1. The uncomfortable fact first: **there is no phylink in this design**

`rtl819x_eth.c` (base A, 2124 lines) contains exactly four occurrences of the string
`phylink`:

```
line  6:  * NAPI + phylink + DSA), но слой работы с DMA свой: ...   (comment, describing the
          *upstream* RTL838x DSA driver as the thing this file is NOT)
line 29:  #include <linux/phylink.h>
line 114: struct phylink *phylink;
line 115: struct phylink_config phylink_config;
```

There is **no `phylink_create()`, no `phylink_of_phy_connect()`, no
`phylink_set_port_modes()`, no `phylink_mac_config()`, no `phylink_link_up()`** —
`grep -c phylink rtl819x_eth.c` = 4, and all four are listed above. The struct members
are allocated-but-unused leftovers of a half-finished port. `CONFIG_PHYLINK=y` is in
`rtl8197f/config-6.18` (line 229) only because patch 720's Kconfig does `select PHYLINK`.

**Consequence:** no phylink state machine runs for the CPU trunk, so phylink can neither
validate nor break it. The driver's own comment says why that is legitimate for this
topology (`rtl819x_eth.c:1562`): *"RGMII на внешний RTL8367D, там нет приёмопередатчика,
согласовывать нечего"* — there is no PHY on the link, so there is nothing to negotiate.
Everything is fixed-link by construction and is configured by **register writes**, not by
`managed = "RG-MID-bps-rx"` style properties.

So the question "phylink config wrong vs switch pins wrong" does not have a phylink log
line to answer it. §3 gives the discriminator that actually works.

For completeness, the two rejected designs *do* use the normal Linux plumbing, which is
where the "phylink" instinct comes from:

* base B (6.6, `rtl8197f_rtknet.c` + DSA): the external RTL8367 family via
  `net/dsa/rtl8367{,5mb}`, CPU trunk described by `port@N` with `phylink`/`fixed-link`
  and `rtl8_4` CPU tags — real phylink, real `of_match` on `phy-handle`/`managed`.
* base C (iptime, 6.18): upstream `rtl8365mb` DSA, likewise phylink-driven.

If we ever switch to either, the phylink checks below become meaningful. Under base A
they are not.

## 2. Where the RGMII delays actually live on our board (three places, one is real)

| layer | register / property | value for RX2L Pro | who writes it | is it real? |
|---|---|---|---|---|
| **RTL8367RB EXTIF1 (switch side)** | `0x1307` = `EXT_RGMXF1`: **TX delay = single bit at position 3; RX delay = 3 bits, steps 0..7**; `0x1305` mode field (bits 7:4 for extif1) = 1 → RGMII; `0x1311` = DI_FORCE_REG(1) = `0x1076` (FORCE_MODE\|TXPAUSE\|RXPAUSE\|LINK\|DUPLEX\|SPEED_1000) | `realtek,extif = <6 0 5 1 1 1 1 1 2>` → **TX 0, RX 5 (= 1500 ps at 300 ps/step)**, mode RGMII, forced 1000/full, pause on, link/duplex/speed set | `rtl8367b_extif_init_of()` in `files-6.18/drivers/net/phy/rtl8367b.c`, driven by the `switch0` node of `port/03-dts-switch/rtl8197f_tenda_rx2lpro.dts:297` | **REAL — this is the only thing that shapes the RGMII timing on our board.** Mandatory, exactly 10 cells, `-EINVAL` otherwise — except if detect reports **RTL8367D**, where `realtek,extif` is ignored and everything comes from `realtek,init-regs` (which we deliberately do **not** ship: see §4) |
| RTL8197F SoC side (MAC) | none in base A | — | `rtl819x_8197f_hw_init()` sets only `CPUICR` burst/buffer and `RTL819X_CF_NIC_LITTLE_ENDIAN`; there is **no** RGMII delay register programmed | inert — the SoC relies on the switch-side delay. Note base A *does* program `0xBB80414C` (P0GMIICR) = `0x02037D57`: bits 25/26 are **CPU tag strip-on-RX / insert-on-TX**, **not** delay bits |
| **U-Boot** | vendor SDK `rtk_port_rgmiiDelayExt_set(logical_cpu_port, TX_DELAY, RX_DELAY)` (strings present in our mtd1) | unknown, but **it survives** | `rtl8367b_reset_chip()` *deliberately skips* the 8367S/RB soft reset "to preserve the loader's power-on RGMII uplink" (recorded in our 4.14 `dir842-asic` step 1) | REAL but **invisible to us**: if U-Boot already set a delay, our `realtek,extif` values land on top of it. Capture them read-only before first build (§4) |

**Why "TX 0 / RX 5" is defensible without a scope**: 300 ps/step is stated by the sibling
RTL8367C boards, `5` × 300 ps = 1500 ps is exactly the RX delay those boards use for the
RGMII CPU trunk, and TX delay 0 matches the fact that our own U-Boot's
`rtk_port_rgmiiDelayExt_set` call sites are present but their arguments are not yet
decoded. It is the *starting* value, and §5 is the experiment that changes it without
reflashing.

## 3. Boot-log decision table: "switch pins wrong" vs "trunk/VLAN config wrong" vs "delay wrong"

Read these in this order on the first console capture. `dmesg` strings quoted verbatim
from base A sources so they can be grepped for.

| observed | conclusion | next action |
|---|---|---|
| `rtl8366_smi: ... failed to read chip id` / chip id reads `0x0000` or `0xffff`; **no `/sys/class/switch/switch0` at all** | **SMI pins wrong** (MDC/MDIO/reset) — the switch was never talked to | check SMI lines = gpio1 24/23/26 = SDK 56/55/58 and the `0x3300000`/`0x0ff00000` mux field; on RTL8367D also the **1.2 s post-reset** rule below |
| `rtl8367b: ... chip RTL8367D ... ` then `realtek,extif` ignored, `realtek,init-regs` absent ⇒ VLANs never programmed | **silicon identity unknown**, not a pin fault | §4 — identify first, then supply the matching table |
| switch0 exists, `swconfig dev switch0 show` prints a port table, `port 6` link `up`, but `eth0` has **no** rx/tx packets | **descriptor / VID path wrong**, i.e. the CPU trunk is *electrically* fine | `cat /proc/rtl819x_spa` (per-source-port counters; a `spa` column that stays 0 while the switch counts frames = the RB is not delivering to the CPU: `tx-port-mask`/EXTIF/tag-mode); then §5 |
| `rtl819x-sw: VLAN 8 (WAN) = port 0 and 8, FID 1, result -110` (or any non-zero `result`) — `-110` = table-write timeout, `pr_err("rtl819x-sw: VLAN %u table write timed out")` | **swcore not clocked / internal switch table engine stuck** — this is the register block that came from the *donor's* bootloader snapshot | re-capture `0xBB804000..0xBB804F00` from our own mtd1 (`driver-port.md` §1.2 step 2) |
| `eth0` link up **1000/full**, iperf3 starts and then retrmits explode, `rx_errors`/`rx_crc_errors` climb, 64-byte pings are clean but 1400-byte ones corrupt, **the same link is perfectly stable forced to 100 Mbit/s** | **RGMII delay wrong** — the classic signature | §5, change `realtek,extif` RX field 0..7 and TX bit; confirm from a PC capture, not from counters alone |
| link **down** at 1000/full but up at 100/half with the same cable | delay wrong (125 MHz DDR timing never locks) **or** `0x1305` mode field not RGMII | read `0x1305`, `0x1307`, `0x1311` back over SMI |
| `rtl819x-tx: frame 1, len=… head=… tail=… icr=…` appears, TX counters rise, **PCAP on the far side is empty, FCS errors on the switch CPU port increase** | TX **DMA/cache** content fault, not timing — measured on the RD05 board: valid-looking `PH_LEN`/`M_LEN (+4 FCS)` copies, empty capture | do **not** touch delays; reproduce base B's A/B (`rtl8197f/RD05_TX_DMA_AB.md`, `rd05_tx_coherent`) as the discriminating experiment |
| frames arrive byte-swapped inside every 32-bit word (`3c:97:0e:c7:0d:f5` reads as `c7:0e:97:3c … f5:0d`), bridge learns MACs, `br-lan` counters stay 0, **no error counters at all** | `RTL819X_CF_NIC_LITTLE_ENDIAN` in `CPUICR1` — an endianness bug, not phylink | `driver-port.md` §4 item 5; also re-check the mipsel assumption (FACTS §2) |
| `rtl819x hwnat: отказ …` (`HWNAT_DECLINE`, only with `hwnat_debug=1`) | offload declining flows — datapath is up, hardware NAT is not | `offload-package.md` §5 |

Rule of thumb worth putting on the wall: **a wrong *config* fails silently at zero
counters; a wrong *delay* fails loudly at error counters and only above 100 Mbit/s.**
If errors are flat and traffic is absent, stop measuring timing.

## 4. Two open items that a boot log cannot resolve

* **Silicon identity RB vs D.** FACTS §5 says "external RTL8367RB"; the DTS comment says
  "the exact silicon is still not settled offline". It matters because RTL8367D ignores
  `realtek,extif` and needs `realtek,init-regs`, and because `rtl8367b.c` notes RTL8367D
  needs **~1.2 s after reset before it answers SMI**. Wrong guess = a driver that
  correctly does *nothing*. Read-only resolution on stock:
  `cat /proc/rtl865x/rtl_8367_chip_type` (detect prints chip `num`/`ver`; RX23302's 8367D
  is `num 0x6642 ver 0x0030`).
* **`realtek,init-regs` is intentionally absent**, and it must stay absent until the chip
  is identified: `rtl8367b_init_post()` applies it **unconditionally on both code paths**,
  so an 8367D table corrupts an RB board and vice versa (03-dts, `switch-driver-analysis.md`).
  Also capture, read-only, the live RB/8367 register set from stock before first build:
  `echo 8367read <reg> > /proc/rtl865x/…` for at least `0x1305`, `0x1307`, `0x1311`,
  the VLAN table, and the CPU tag register — because U-Boot's
  `rtk_port_rgmiiDelayExt_set()` values are the *baseline our DT overwrites*.
  If stock reports **TX delay ≠ 0**, our `realtek,extif` cell 2 must match stock, not the
  sibling board.

## 5. Changing the delay without reflashing (the only experiment worth running first)

`rtl8367b.c` writes EXTIF at probe, so on a booted OpenWrt the way to A/B the trunk is to
poke the switch register directly over the same SMI bus and watch the counters:

```sh
# after link is up at 1000/full and iperf3 A1 shows error growth:
swconfig dev switch0 set help            # what the agent supports
# EXT_RGMXF1 = 0x1307: TX = bit 3, RX = bits 2:0 (0..7), 300 ps/step
devmem 0x…   # NOT applicable: 0x1307 is a switch register on bit-banged SMI, not MMIO.
             # Use the switch's own access path, i.e. re-run extif with a changed cell:
uci set network.@switch[0]…   # no uci surface exists for extif ->
```

Honest position: there is **no userspace knob today** for `realtek,extif` — swconfig
exposes VLAN/port/PVID/MIB, not the RGMII delay register, and the value is applied once at
probe. So the delay A/B loop is **edit the DTS → rebuild in GitHub Actions → flash**, which
is slow. The cheap, build-free alternative that should be added (see
`patches/001-…`): export the two EXTIF delay cells as writable module parameters or a
`/proc/rtl8367b_extif` writer that re-calls `rtl8367b_extif_init_of()`, exactly the way
base A exported `/proc/rtl819x_gnezda` for the TX jack masks ("Маски можно сменить на
лету, без перепрошивки (проверка гипотезы в ОЗУ и мгновенный откат)"). Until that exists,
the practical discriminator is the **forced-speed test** — put a 100 Mbit/s-only device or
`swconfig dev switch0 port N set force_mode 100M/Full` on the trunk side if the driver
allows it, and see whether the errors vanish: errors vanish ⇒ timing; errors unchanged ⇒
look at DMA/endianness/VLAN instead.
