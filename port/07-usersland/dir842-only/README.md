# DIR-842-only assets, kept out of an RX2L Pro image

*(Moved here from a duplicate `port/07-userspace/` directory: two tracks
shared the 07 number, and this one had no REPORT.md. The content belongs
with the userspace track that owns base-files and sysctl decisions.)*

This tree builds exactly one device: `tenda_rx2lpro` (see
`files/target/linux/rtl819x/image/rtl8197f.mk`, which replaced the 4.14 image
Makefile and its DIR-815/822/RE708/GWR profiles). The DIR-842 material inherited
from the 4.14 line is therefore either inert or actively wrong on this board.

## `31-dir842-lowmem.conf` — why it lives here now

It set `vm.min_free_kbytes=2048` to escape the 64 MiB DIR-842's 8192 kB floor (a
real OOM cascade was measured there). Two problems on the RX2L Pro:

* its rationale is that board's `MemTotal ~58 MB`; ours is ~115 MiB, where
  OpenWrt's own tier picks 16384 kB. Neither number applies;
* `sysctl.d` files cannot be conditional, so unlike `uci-defaults/*` and
  `init.d/*` (all board-gated) this one ran on every board of the target.

Lowering a 128 MiB board's reserve to 2 MiB is not obviously fatal -- the
kernel's own computed default there is ~1.3 MiB -- but it is a memory-pressure
behaviour change justified only by a 64 MiB board's measurements. Our board has
no such measurement, so it does not get the tuning.

If a DIR-842 profile ever returns to this tree, the correct form is a
board-gated uci-default (or an init script) that writes this file, not an
unconditional `sysctl.d` drop-in.
