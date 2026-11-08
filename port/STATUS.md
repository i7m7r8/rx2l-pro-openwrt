# Status snapshot — 2026-11-08 (round 36 of 40)

Not a facts file (that is `FACTS.md`) and not a plan (that is each track's
`REPORT.md`). This is the short answer to "where is this port right now".

## In flight

One GitHub Actions run: **`829540e`** (`42429513794`), started 00:23:16Z.
It carries the first *identified* fix for the kernel-stage failure:

* `patches-6.18/999-net-phy-wire-rtl8367b-switch-driver.patch` **deleted** — it
  redefined `RTL8367B_PHY`, which openwrt's own
  `hack-6.18/700-swconfig_switch_drivers.patch` already defines, and the
  duplicate produced
  `error: recursive dependency detected! symbol SWCONFIG is selected by
  RTL8367B_PHY / symbol RTL8367B_PHY depends on SWCONFIG`, so `syncconfig`
  never completed and nothing was ever compiled;
* `CONFIG_RTL8366_SMI=y` added — `RTL8367B_PHY` lives inside `if RTL8366_SMI`,
  so without it the symbol is silently dropped;
* plus everything in the "Verified" list below that is a build-side gate.

Read the run's log for one of three outcomes: `BUILD FAILED` (it now prints the
real error and a kernel `.config` survival list), the symbol assertion (fails
with a line naming any of nine missing symbols), or a green build.

## Verified against evidence (not against hope)

| claim | evidence |
|---|---|
| flash map, container contract, loader env | this unit's own mtd dumps (`notes/firmware-audit.md`) |
| loader accepts `cr6c`/`cs6c` only, BE-u16 zero-sum, squashfs at `16+len` | decompressed stage-1 strings + the flash container satisfying it |
| CFG key offsets for MACs, product id `0x00031053`, no `wan_mac` | `<0x23b>`, `<0x5b5>`, `<0x5d7>` match the DTS cells |
| per-unit calibration is well-formed (xcap 45 in `0<x<0x3F`, 14-channel tables) | the driver's own acceptance condition |
| base commit and both feed commits are the overlay's tested set | the overlay's `versions.sh`, all three identical |
| switch driver's whole API surface exists | base `switch.h` + overlay sources + our `rtl8367.h` |
| VLAN tags are 9 (LAN) / 8 (WAN) | `rtl819x_eth.c` constants + its own debug string |
| the two webui verifiers agree, including *why* they reject | three fixtures, same reason both sides |
| every removed pre-6.18 API in the compiled HAL is handled | sweep of `init_timer`, `skb->nh`, `trans_start`, `get_fs`, … |
| the reference board's seven scripts are inert here | each gates on `gwr1200ac-v1` |

## Not verified

* **that it compiles** — the run above is the test; nothing on this host builds.
* **anything needing silicon**: boot, Ethernet link, switch programming, MIB
  application, 5 GHz. `port/07-usersland/first-boot-checklist.md` is the order to
  check them in.
* the **128 MiB** memory node is from spec, not from our dumps (the device's
  `meminfo` capture came back empty).

## Next actions, in order

1. Read `829540e`'s result. If it failed, the log now names the cause; the symbol
   survival list says whether it was a config drop rather than a compiler.
2. On green: download the artifacts, run `tools/check-cr6c.py --tenda` and
   `scripts/rx2lpro-image-gates.py webui` by hand (the build already ran both),
   then RAM-boot `*-initramfs-kernel.bin` over serial — writes nothing.
3. Only then flash, by install path 2 or 3 in `README.md`, and verify `mtd3`
   starts with `cr6c` before rebooting.
