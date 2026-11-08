# sysupgrade on the 6.18 base — Tenda RX2L Pro

Staged code: `base-files/lib/upgrade/platform.sh`.
Scope: how an upgrade finds the right partitions, what is preserved, what the
single-bank `KernelFS` layout makes impossible, and how a wrong or missing
switch driver is made survivable.

---

## 1. What the image actually is, and how it finds the flash

`IMAGE/sysupgrade.bin` (files/target/linux/realtek/image/Makefile, RX2LPRO
block) is:

```
append-kernel | cvimg-kernel-cr6c | append-rootfs | cvimg-pad-rootfs
              | sysupgrade-tar | append-metadata

offset 0x000000  "cr6c" 16-byte cvimg header  (sig, start_addr, burn_addr, len, BE)
offset 0x000010  kernel payload (lzma-loader + vmlinux.lzma), len bytes
offset 16+len    2-byte ones-complement checksum of the payload
offset 16+len    ← squashfs rootfs begins HERE (un-aligned; see §3)
   ...           cvimg "-j" section: header + 4 bytes deadc0de + checksum
   ...           uhttpd/`sysupgrade-tar` payload (the conffile tar)
   tail          fwtool metadata (board name, versions, compat_version)
```

Flash, from `dts/RX2LPRO.dts` (partition table copied verbatim from the
device's `/proc/mtd`):

| label | offset | size | written by us? |
|---|---|---|---|
| `Bootloader` | 0x000000 | 0x30000 | no — stage-1/stage-2 + `disaster_web` |
| `CFG` | 0x030000 | 0x10000 | no — holds `TENDA.lan_mac=` etc. |
| **`firmware`** | 0x040000 | 0xf80000 | **yes, this and only this** |
| `CFM` | 0xfc0000 | 0x10000 | no — RF calibration |
| `CFM_BACKUP` | 0xfd0000 | 0x10000 | no |
| `MISC2` | 0xfe0000 | 0x10000 | no |

How the pieces are found at runtime:

* **userspace** uses the label: `PART_NAME=firmware`, and
  `find_mtd_part firmware` resolves it through `/proc/mtd`. The staged
  `platform_check_image` refuses outright if that label is absent — a wrong
  flash map must fail *before* anything is erased, not after.
* **kernel** splits it: `CONFIG_MTD_SPLIT_FIRMWARE_NAME="firmware"` is the
  only partition `mtdsplit_cvimg` will parse, and it creates `kernel`
  (0…rootfs_offset) and `rootfs` (rootfs_offset…end). `mount_root` then carves
  **`rootfs_data`** out of the tail of `rootfs` — that is where the overlay
  lives, and it is the destination of the config backup. There is no
  `rootfs_data` in the DTS; it is derived.
* Nothing else in the flow uses an offset, which is the point of §3.

Compare base A (`/tmp/rebase/openwrt-rx23302/.../base-files/lib/upgrade/platform.sh`):
their board has *two separate partitions* (`kernel` @0x30000, `rootfs`
@0x300000) plus a *real* `rootfs_data` partition, so their
`platform_do_upgrade` can order writes (erase kernel → write rootfs → write
kernel last) and their `platform_copy_config` is an explicit
`mtd jffs2write "$UPGRADE_BACKUP" rootfs_data`. We can do neither, and §5
covers what we do instead.

---

## 2. What gets backed up and restored

The framework owns this; `platform.sh` only needs to not break it.

1. `sysupgrade -c`/default run: stage2 (`/lib/upgrade/stage2`) tars the files
   named by `/etc/sysupgrade.conf`, every directory under `/lib/upgrade/keep.d/*`
   and every path declared a conffile by opkg (`/etc/opkg/*conffiles`), plus
   the hard-coded essentials, into `/tmp/sysupgrade.tgz`, then
   `mtd -j "$UPGRADE_BACKUP" write - firmware` embeds it in the trailing JFFS2
   area. On boot, preinit unpacks it over the fresh rootfs.
2. `/etc/config/*` is preserved because the packages that ship those files
   declare them conffiles (netifd → network, firewall4 → firewall, wpad/hostapd
   common → wireless-related, luci → its own). **Verify, do not assume** — the
   read-only command is in `first-boot-checklist.md` item B4. If `wireless` is
   missing from the list, the radio config is silently reset on every upgrade.
3. Files our overlay creates at runtime are **not** in any of those sets and
   are therefore lost on a config-preserving upgrade, by design, and
   re-created by uci-defaults:
   * `/etc/Wireless -> /tmp` symlink → re-made by `07_wireless-2g-rx2lpro`
     (`[ -e /etc/Wireless ] || ln -s /tmp /etc/Wireless`). It must NOT be
     added to the backup list: restoring a dangling symlink into `/tmp` at
     preinit time is a fresh class of boot failure.
   * `/usr/share/nftables.d/table-post/20-rx2lpro-flowtable-eth0.nft` lives in
     the overlay, not /etc → gone after upgrade → `/etc/init.d/rtl819x-hwnat`
     re-materialises it from `/rom` on boot (its `ROM_SNIPPET` branch).
   * `/etc/rc.d/SNN` symlinks for our three services → recreated by the
     `enable` calls in `uci-defaults/05_enable_flow_offloading`. A base-files
     init.d script is never auto-enabled; if that file is dropped, both the
     offload path and the recovery fallback vanish without a log line.
4. Deliberately NOT carried over from the 4.14 tree: `/etc/config/dir842`.
   Because `/etc/config` is preserved across upgrades, an orphaned board
   stanza is the one thing per-boot gating cannot clean up — it would have
   survived forever on every RX2L Pro.

---

## 3. The geometry, and the one bug that bricks

`FACTS.md` §4 / `README.md` §"The container format": squashfs starts at
exactly `16 + payload_len`, **not** at `ALIGN(16+len,4)`. On this unit's stock
mtd3 the superblock is at 0x3c0812, which is not 4-byte aligned.

Why it matters to sysupgrade specifically: `mtdsplit_cvimg` as shipped
computed the aligned candidate first. Whether our *own* build hits the aligned
offset depends on `kernel size mod 4`, so the split can work in one build and
fail in the next — silently, at boot, with
`VFS: Unable to mount root fs on unknown-block(0,0)`. That is the worst
possible place to discover an off-by-two, and this repo already patched the
parser to try exact → aligned → scan
(`files/target/linux/generic/files/drivers/mtd/mtdsplit/mtdsplit_cvimg.c`).

The shipped `platform_check_image` closes the loop from the other side: it reads
`len` out of the *image's own header* and looks for `hsqs`/`sqsh` at byte
`16+len` **before** any erase. So the three independent things now have to
agree, and each is checked in a different place:

| assertion | who checks it | when |
|---|---|---|
| container magic `cr6c` | `platform.sh` | before writing |
| squashfs at `16+len` is a plain `hsqs`/`sqsh` | `platform.sh` | before writing |
| container magic / checksum / no `nice` mangling | `tools/check-cr6c.py` | CI, after build |
| parser tries exact first | `mtdsplit_cvimg.c` | every boot |

One more trap lifted from the iptime port (their `platform.sh` comment, and a
bug they hit for real): `dd … skip=N iflag=fullblock`. Reading from a pipe
returns short, `dd` counts each short read as a whole record, and `skip=`/`count=`
go off by however much the pipe was shortened — so the magic check reads
garbage and, in a write path, the kernel blob is silently truncated. The staged
file uses `bs=1 skip=$off count=4` deliberately: slow on 15 MiB of nothing (we
only read 4 bytes) and impossible to mis-scale. **If anyone "optimises" that
to block arithmetic, keep `iflag=fullblock`.**

---

## 4. Single bank: what that rules out, and the README/FACTS contradiction

FACTS.md §1: *"`KernelFS`(mtd3) — single image, **no KernelFS1/2, so no
dual-bank rollback**; recovery is `disaster_web` plus the mtd1..mtd7 dumps."*
README.md §"Recovery net": *"The vendor runs a **dual image**:
`KernelFS1`/`KernelFS2` with `env set image_boot 1\|0 unlock`, so a boot
failure can fall back to the other slot."*

These cannot both be true, and the sysupgrade design depends on which is. The
flash map in the DTS (and every `/proc/mtd` reading quoted in README) shows
**one** KernelFS partition of 0xf80000, and base A's own board needs a
0x830000 second bank to make its ordering trick work. So the working
assumption here is FACTS §1: **no rollback**.

Resolution is cheap and read-only (checklist item R1): from a RAM boot run
`fw_printenv` (or `strings /dev/mtd1 | grep -o 'image_boot[0-9=_a-z]*'` on the
dump) and look for `image_boot`, `image0_stats`, `image1_stats`, and a second
`KernelFS`. Three outcomes:

* **No second bank (expected).** Then `image_boot` in README is inherited
  vendor boilerplate from a sibling Tenda product and must be deleted, and the
  upgrade contract is: validate hard, write once, and treat
  `disaster_web`/`AUTOBURN` as the only rollback.
* **Second bank exists.** Then the DTS needs a `KernelFS2` partition, the image
  recipe a burn address per bank, and `platform_do_upgrade` should adopt base
  A's ordering *for the same reason they chose it*: erase kernel first so an
  interrupted upgrade cannot boot a new kernel over an old rootfs (or vice
  versa) and instead drops the loader into its fallback path.
* **`image_boot` exists but both slots point at the same region.** Do not
  write U-Boot environment from an upgrade at all. The env is in the
  `Bootloader` partition we never touch, `env` writes have their own
  checksum, and a botched one costs the `disaster_web` serverip/ipaddr
  settings — i.e. it destroys the last way back.

Either way, `sysupgrade` must never write anything but `firmware`.

---

## 5. Making a dead/absent switch driver survivable

The realistic failure on this board is not a bad flash write, it is: image
boots, rootfs mounts, console is clean — and nothing is reachable, because
the external RTL8367 has no driver, or the driver probed and programmed a VLAN
the SoC's internal switch then drops (README risk #6). Four independent
layers, cheapest first, all in the staged files:

**L1 — decide at runtime, never at build time.** `etc/board.d/02_network`
tests what actually materialised (`/sys/class/net/lan1` → DSA,
`/sys/class/switch/switch0` → swconfig, else neither) and picks lan/wan
devices accordingly. In the "neither" case it sets `LAN = eth0` and generates
**no WAN at all**: a missing WAN section is visible and harmless (fw4's wan
zone just has no devices), whereas a WAN bridged into LAN loops. On this SoC
`eth0` is always the CPU netdev — `rtl819x_eth.c` registers exactly one — so
"LAN=eth0" is the one claim that does not depend on the external chip at all.

**L2 — keep the radio as the second way in.** `07_wireless-2g-rx2lpro` and
`lib/wifi/rtl8192cd.sh` seed the 2.4 GHz AP unconditionally and *refuse* to
seed 5 GHz while RTL8832BR has no driver, because netifd failing one radio
fails the whole wireless bring-up and takes the fallback with it.

**L3 — self-heal at boot.** `etc/init.d/rx2lpro-network-recovery` (START=96,
`boot()` only): if `ubus -t 20 wait_for network.interface.lan` times out, it
rewrites `network.lan.device=eth0`, static 192.168.1.1, enables `dhcp.lan`,
restarts network, forces `radio0.disabled=0` and runs `wifi`. Adapted from the
iptime port's identical service. It cannot fix a broken driver, but it turns
"brick" into "a laptop plugged into any jack gets a lease and LuCI".

**L4 — outside Linux entirely.** `disaster_web` (`ipaddr=192.168.1.1`,
`serverip=192.168.1.99`) and loader `AUTOBURN`, plus OpenWrt **failsafe**: hold
RESET during boot, `mtd`/`sysupgrade` are then available over 192.168.1.1 with
no config loaded. This is the layer that still works when L1–L3 are all wrong,
and it is why the mtd1..mtd7 dumps are the real recovery plan, not the
software.

Rules that follow from the above and should be respected by whoever edits this
next:

* LAN stays on **192.168.1.1** — same subnet as stock and as `disaster_web`,
  so one cable and one bookmark work through every layer.
* Never write `Bootloader`, `CFG`, `CFM`, `CFM_BACKUP`, `MISC2`. `CFG` and
  `CFM` are `read-only` in the DTS, which `mtd` enforces; keep it that way and
  do not "helpfully" drop the flag to make an experiment shorter.
* The image we hand to `sysupgrade` must be the `sysupgrade.bin` we produce,
  not `factory-cr6c.bin`. They differ by the tar + metadata trailer, and
  `REQUIRE_IMAGE_METADATA` means the factory file is rejected rather than
  silently half-applied.

---

## 6. What the image track has to answer before this is final

1. **`mtdsplit_cvimg` vs fixed-partitions.** base A ships *no* mtdsplit parser
   and uses fixed partitions; ours carries the patched parser in
   `files/target/linux/generic/…`. If the rebase adopts base A's
   kernel-partition/rootfs-partition layout, §3's geometry argument disappears
   (offsets become DTS constants), `platform_do_upgrade` gains the safe
   ordering from base A, and this file must be rewritten around two partitions
   instead of one. That is the image track's call, not mine; both variants are
   written down above so the swap is mechanical.
2. **`SUPPORTED_DEVICES` / `compat_version`.** The device profile still says
   `SUPPORTED_DEVICES := rx2l-pro`. On master the identity check is
   `REQUIRE_IMAGE_METADATA=1` + the `supported_devices` list in the metadata +
   `ucidef_set_compat_version` (see base B's `etc/board.d/05_compat-version`,
   the mechanism does not exist in our 4.14 tree). If the board name changes to
   `tenda,rx2l-pro`, an upgrade from an already-flashed OpenWrt will be
   rejected unless `rx2l-pro` stays in the supported list. Whoever owns the
   image recipe should add both, or the first rebase-flasher needs
   `sysupgrade -F`, which is a thing you should not have to tell people to do.
3. **`sysupgrade -T` dry run must be exercised in CI-adjacent testing** — it is
   the only path that runs `platform_check_image` without writing, and every
   bug in §3 is catchable that way on a bench box running our own image.

### Status: implemented, and tested on three fixtures

The check described above was written up while still staged; it is now in the
shipped `platform.sh` and was exercised outside the target against:

| fixture | result |
|---|---|
| container with `hsqs` at `16+len` | `rc=0` — accepted |
| this unit's stock container (`nice` at `16+len`) | `rc=1` — `no squashfs at 16+len (0x3c0812), got 0x6e696365` |
| the same image truncated two bytes past the offset | `rc=1` — `got 0x6873` |

The `nice` rejection is the intended behaviour, not a regression: our kernel has
no vendor magic patch, so a stock container is not something this board can
sysupgrade *to* — the vendor image belongs on the loader/webui path, not here.

`hexdump` is what the check uses to read big-endian fields. That is safe on the
target because OpenWrt's own `get_magic_long()` (which the same function already
called) uses `hexdump -v -e`, so the binary is present in any OpenWrt image; it
was only missing on the host used for this test, where a shim stood in.
