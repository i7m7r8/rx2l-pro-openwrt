# Status snapshot — 2026-11-08 (round 37 of 40)

Not a facts file (that is `FACTS.md`) and not a plan (that is each track's
`REPORT.md`). This is the short answer to "where is this port right now".

## In flight

One GitHub Actions run: **`40f5e9d`** (`42446968192`), started 04:28:41Z.
It carries the first fix aimed at an error the compiler actually printed:

* `mtdsplit_cvimg.c:49` said `const static` -> `-Werror=old-style-declaration`
  -> `cc1: all warnings being treated as errors`. Now `static const`.

Both runs before it (`42429513794` on rebase-latest at `829540e`, `42433172719`
on test/pci5g at `1932146`) died on that same line and nothing else: it was the
only `error:` in either 5000-line log, 95 minutes in. So the two previous
"failures" have one root cause, and it is one word.

Why `-Werror` is real and stays: OpenWrt implies it from the toolchain choice —
`toolchain/gcc/Config.version:19` has `imply KERNEL_WERROR` under
`GCC_USE_DEFAULT_VERSION` (GCC 14 is the default), and build.sh's kernel config
step maps top-level `CONFIG_KERNEL_*` into the kernel namespace, which is what
turns it into `CONFIG_WERROR=y` *after* `generic/config-6.18:8120`'s
`# CONFIG_WERROR is not set` — last write wins. The platform overlay is
hardware-tested at this same base rev with the same implied flag, so the
warning was ours to fix, not the build's to relax.

Why it appeared only now: this file had never been compiled. Every earlier run
died before cc1 ran at all (the swconfig Kconfig cycle; then the missing
realtek Kconfig symbol), so `drivers/mtd` was the first place the splitter
reached the compiler.

```sh
gh run list --limit 3                       # is 40f5e9d done, and how
gh run view <id> --log | grep -nE 'BUILD FAILED|MISSING CONFIG_|GATE FAIL|error:'
```

Read the log for one of three outcomes: `BUILD FAILED` (the script prints the
real error plus a kernel `.config` survival list), the symbol assertion (a line
naming any of nine missing symbols), or a green build. Note the failure order
this run reaches first: `drivers/mtd` is past, so the next compiler stops are
`drivers/net/phy` (rtl8367b + our `rtl8367.h`, whose field use was re-checked
against the driver and matches) and then `drivers/net/wireless/rtl8192fe` --
the vendored 4.14 HAL, which no earlier run has compiled. If it is green, the
artifacts are under `bin/targets/rtl819x/rtl8197f/` and the next action is the
RAM boot.

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
