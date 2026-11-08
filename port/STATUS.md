# Status snapshot — 2026-11-08 (round 38 of 40)

Not a facts file (that is `FACTS.md`) and not a plan (that is each track's
`REPORT.md`). This is the short answer to "where is this port right now".

## Where the build actually is

**The kernel compiles.** Run `42447250472` (`aba80cb`) reached the compiler for
the first time in this port's history and came out of it clean, then reported
all eleven asserted symbols present in the built `.config`:

```
ok CONFIG_SWCONFIG=y                     ok CONFIG_RTL8192FE_RX23302=y
ok CONFIG_RTL8366_SMI=y                  ok CONFIG_WIRELESS_EXT=y
ok CONFIG_RTL8367B_PHY=y                 ok CONFIG_WEXT_PRIV=y
ok CONFIG_MTD_SPLIT_CVIMG_FW=y           ok CONFIG_SOC_RTL8197F=y
ok CONFIG_MTD_SPLIT_SQUASHFS_ROOT=y      ok CONFIG_HAVE_PCI=y
ok CONFIG_NET_RTL819X=y                  OFF CONFIG_PCI (branch test/pci5g only)
```

That includes the vendored 4.14 wifi HAL (`RTL8192FE_RX23302`) and the switch
driver. The three earlier red runs were, in order: a swconfig Kconfig cycle
(ours), `const static` under `-Werror=old-style-declaration` (ours), and a
symbol assertion naming `CONFIG_WLAN_HAL_8197F` — a symbol this tree never
defines, because the overlay sources `Kconfig.rx23302` (one symbol) and not the
vendor's 1451-line Kconfig.

**The image stage is the current failure**, and it hides its own error:
`target/install` printed `ERROR: target/linux failed to build.` and nothing
else, because the image build under `target/linux install` runs through
`NO_TRACE_MAKE`. That is why `be97362` makes the failure path re-run *both*
`target/linux/compile` and `target/linux/install` with `-j1 V=s`, grep for
`GATE FAIL`/`BUILD PRODUCED` as well as `error:`, and list
`bin/targets/*/*` and any `CONFIG_BUILD_LOG` files.

Ruled out for that stage while the run was in flight (local, on this host):
the DTS (cpp+dtc 1.7.2 -> 4092-byte blob, warnings only, every `&label` it
uses exists in `RTL8197F.dtsi`), `cvimg` with the exact recipe flags
(`-e 0x81000000 -b 0x40000 -s cr6c` -> container that `tools/check-cr6c.py`
accepts), `tenda-wrap -c cr6c -k 0x808d45c0` (wrap -> webui gate exit 0), and
the `container` / `budget` / `webui` gates on a hand-assembled
kernel+squashfs container (all exit 0). So the cause is in the make wiring of
the image recipe, not in the tools or the gates.

In flight now: **`be97362`** (`42453851939`), started 06:13:27Z.

```sh
gh run list --limit 3
gh run view <id> --log | grep -nE 'BUILD FAILED|MISSING CONFIG_|GATE FAIL|error:|reference'
```

If it is green, the artifacts are under `bin/targets/rtl819x/rtl8197f/` and the
next action is the RAM boot. If it is red again, the re-run section now names
the failing image step.

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
* nothing else. (The 128 MiB memory node used to be listed here as spec-only;
  it is now evidence-backed — the stock syslog shows `MemTotal: 115744 kB`, see
  `notes/firmware-audit.md` §10.)

## Next actions, in order

1. Read `40f5e9d`'s result. If it failed, the log names the cause; the symbol
   survival list says whether it was a config drop rather than a compiler.
2. On green: download the artifacts, run `tools/check-cr6c.py --tenda` and
   `scripts/rx2lpro-image-gates.py webui` by hand (the build already ran both),
   then RAM-boot `*-initramfs-kernel.bin` over serial — writes nothing.
3. Only then flash, by install path 2 or 3 in `README.md`, and verify `mtd3`
   starts with `cr6c` before rebooting.
