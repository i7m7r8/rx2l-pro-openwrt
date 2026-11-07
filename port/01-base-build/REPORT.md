# Track 01 — base, build entrypoint, feeds, seed, CI

Staged output only; nothing here is wired in. Files in this directory:

| file | becomes |
|---|---|
| `build.sh.new` | `build.sh` (needs `chmod +x` on commit — see "Exec bits") |
| `feeds.conf.new` | `feeds.conf` |
| `seed.config.new` | `seed.config` (usable as-is; `seed.config.diff` is the audit of the move) |
| `seed.config.diff` | reference / `git apply` input |
| `Dockerfile.new` | `Dockerfile` |
| `build.yml.new` | `.github/workflows/build.yml` |
| `config-6.18.rx2l.delta` | input for the kernel-config decision (orchestrator + tracks 02/03/04/05/06) |

## Decisions

**1. Base = upstream `openwrt/openwrt@82e2e4f492d58b5b6afaf5b55d8286c58da9c224`, kernel
6.18.41, `target/linux/rtl819x` (`rtl8197f` subtarget, `ARCH:=mipsel`), applied as a
pinned build-time overlay — not vendored.**

`BASE_REV` is the commit the platform repo pins in its own `versions.sh:6`; the kernel
version comes from that base's `target/linux/generic/kernel-6.18`
(`LINUX_VERSION-6.18 = .41`). Endianness is safe on both sides: `rtl819x/Makefile:11`
(`ARCH:=mipsel`), plus `rtl8197f/target.mk:10` (`CPU_TYPE:=24kc`) — matches FACTS §2.
(The 6.6 reference tree says the same thing from its own side:
`/tmp/rebase-base/target/linux/realtek/rtl8197f/target.mk:3 ARCH:=mipsel`.)

Why overlay-at-build-time instead of copying `target/linux/rtl819x` into `files/`:
* The overlay is 1689 files, most of them the vendor Wi-Fi HAL, with its own
  provenance/licensing write-up (`docs/sources.md`) that stays intact at one pinned
  commit; our repo keeps being a device port instead of becoming a redistribution.
* It is one variable (`OVERLAY_REV`) to move when they fix something — and their
  fixes are load-bearing: `openwrt-upstream.patch` contains the netifd
  `utils.uc` handler-loop fix (upstream PR #25851) which we *need*, since we ship
  `/lib/netifd/wireless/rtl8192cd.sh` and their measured failure mode is netifd at
  100 % CPU during wireless bring-up.
* Cost, and the mitigation: we must also edit some of their files (the device
  profile lives in `image/rtl8197f.mk`, which also carries their
  `Device/electra_rx23302`). Our layer copies whole files over theirs, which
  silently drops any new content in the pinned-lagging copy. `build.sh.new`
  therefore prints a **clobber report** — every `files/` path that also exists in
  the overlay and differs. Review that list on every pin bump; it already found
  one real regression (below).
* The overlay clone lands at `openwrt/.port-cache/`, i.e. inside the gitignored
  base tree, so `git add -A` in this repo can never stage ~40 MB of someone
  else's GPL drop.

**2. Feeds: pinned by commit with `^<sha>`, two entries only.**
`scripts/feeds` at BASE_REV supports `URL^commit` (`update_feed_via`: with a commit it
clones `--depth 1`, fetches that SHA, and *never* updates again). I verified both
servers answer fetch-by-SHA: `git fetch --depth=1 origin <sha>` from
`git.openwrt.org/feed/packages.git` and `.../project/luci.git`, rc=0 (they ignore
`--filter`, which is fine).

**3. FEEDS_INSTALL default flips to `-a` (install everything from both feeds).**
The curated list existed to work around 19.07-era pins. On this base a curated list is
an active hazard: an uninstalled package's symbol is dropped by `make defconfig`
**silently** — the failure we documented once already. `-a` is what the platform repo
does (`build.sh:57`). The variable still accepts a name list.

**4. Kernel config: the base ships `rtl8197f/config-6.18`, and it already carries most
of what we need** (SWCONFIG + RTL8367B_PHY + RTL8366_SMI + PHYLINK + NET_RTL819X +
MTD_SPI_NOR + MTD_ROOTFS_ROOT_DEV, all `=y`), so the seed has no kmod lines for them.
`config-6.18.rx2l.delta` lists the six things that are genuinely missing or decided,
with line numbers.

## What `build.sh.new` adds over the 4.14 one

Same variables (`BASE_REPO`, `BASE_REV`, `FEEDS`, `SEED`, `FEEDS_INSTALL`,
`SKIP_FEEDS`, `PROFILE`), same shape (clone → overlay → feeds → seed → build →
verify the container → manifest), plus four gates, each one for a failure we have
already paid for once:

1. **stale-target guard** — refuses to copy `files/` if it still contains
   `target/linux/realtek`, because OpenWrt discovers targets by scanning
   `target/linux/*/Makefile` and a 4.14-shaped copy registers a broken second
   target. This is the first thing the orchestrator will hit: it is deliberate.
2. **clobber report** — prints every `files/` path that replaces an overlay file
   with different content (found the mtdsplit regression, risk 1 below).
3. **parse gate before `feeds update`** — `./scripts/feeds list -n` costs a second
   and catches the bare-`#` line before either feed is cloned.
4. **defconfig gate** — greps the five symbols whose absence is a broken router
   (`TARGET_rtl819x_rtl8197f`, `luci`, `uhttpd`, `firewall4`, `wifi-scripts`)
   plus "some `DEVICE_*` was selected", then refuses to compile. Also refuses to
   call a build successful if no `*factory-cr6c.bin` / `*tenda-plaintext.bin`
   was produced under `bin/targets/rtl819x/rtl8197f` (previously the glob just
   matched nothing and the run looked fine).

Exec bits are restored by `fix_exec_bits()` for `files/` *and* the tree, covering
`etc/{init.d,rc.d,uci-defaults,hotplug.d,board.d}`, `lib/{netifd/wireless,wifi}`,
`usr/{bin,sbin}`, `bin`, `sbin`, plus any file whose first line is a shebang.
`lib/upgrade/platform.sh` is left at 0644 on purpose: it is sourced.

## Verification actually run (all read-only, no build)

Reproduce exactly:

```sh
# 0. the three trees this audit quotes
cd /tmp && for r in "openwrt https://github.com/openwrt/openwrt 82e2e4f492d58b5b6afaf5b55d8286c58da9c224" \
                    "packages https://github.com/openwrt/packages 712ee13a837449fd91b5279048303d3a790d691b" \
                    "luci https://github.com/openwrt/luci 4c0a4eda5ea17f9e81fc860366d7d04ce3c21b8e"; do
  set -- $r; git init -q $1 && git -C $1 remote add origin $2 && git -C $1 fetch -q --depth 1 --filter=blob:none origin $3
done
# NOTE: the GitHub mirrors carry the same SHAs as git.openwrt.org, which is what makes
# this cheap; feeds.conf.new itself points at git.openwrt.org (proven above).

# 1. feeds.conf.new parses with the BASE's own scripts/feeds (not a look-alike)
cd /tmp/openwrt && git sparse-checkout set scripts && git checkout -q -b work 82e2e4f4
cp /root/rx2lpro/rx2l-pro-openwrt/port/01-base-build/feeds.conf.new feeds.conf
./scripts/feeds list -n            # -> "packages\nluci", rc=0
./scripts/feeds list -s            # -> URLs intact, ^sha preserved
# negative control, the documented trap (scripts/feeds:54 s/#.+$//):
{ head -1 feeds.conf; printf '#\n'; tail -n +2 feeds.conf; } > f2 && cp f2 feeds.conf
./scripts/feeds list -n            # -> "Syntax error in feeds.conf, line 2", rc=25
# and inline comments after a src line are stripped correctly (rc=0).

# 2. the substitution + parse gate that build.sh.new runs before `feeds update`
cp /root/rx2lpro/rx2l-pro-openwrt/port/01-base-build/feeds.conf.new feeds.conf
sed -i -E "s|^(src-git +packages +[^ ^]+)\^[^ ^]+|\1^deadbeef123|" feeds.conf
sed -i -E "s|^(src-git +luci +[^ ^]+)\^[^ ^]+|\1^cafe456|"  feeds.conf
./scripts/feeds list -n && ./scripts/feeds list -s   # rc=0, carets intact

# 3. scripts + workflow sanity, and the CI feeds gate run locally
bash -n /root/rx2lpro/rx2l-pro-openwrt/port/01-base-build/build.sh.new      # SYNTAX OK
python3 -c "import yaml;yaml.safe_load(open('build.yml.new'))"              # YAML OK
perl -e '...the same 6 lines scripts/feeds:54-61 uses...' feeds.conf        # ok line 40/41

# 4. seed.config.diff is a real diff, proven applyable and byte-reproducible
mkdir -p /tmp/pt && cd /tmp/pt && cp /root/rx2lpro/rx2l-pro-openwrt/seed.config .
patch -p1 < /root/rx2lpro/rx2l-pro-openwrt/port/01-base-build/seed.config.diff  # patching file seed.config
cmp seed.config /root/rx2lpro/rx2l-pro-openwrt/port/01-base-build/seed.config.new  # identical

# 5. the exec-bit helper, against a deliberately 0644 copy of our files/:
#    etc/init.d/* came back 0755; lib/upgrade/platform.sh stayed 0644 (it is sourced)
# 6. every CONFIG_* line of seed.config.new grepped for in the overlay tree,
#    BASE, /tmp/rebase-base and our files/ -- see the misses listed below
```

## Seed audit — symbol by symbol

Renamed / changed (all resolve at BASE_REV + overlay, evidence quoted inline in
`seed.config.new`):

| 4.14 seed | 6.18 seed | evidence |
|---|---|---|
| `CONFIG_TARGET_realtek=y` | `CONFIG_TARGET_rtl819x=y` | `rtl819x/Makefile:12` `BOARD:=rtl819x` |
| `CONFIG_TARGET_realtek_rtl8197f` | `CONFIG_TARGET_rtl819x_rtl8197f` | `SUBTARGETS:=rtl8197f`, same file:14 |
| `..._DEVICE_RX2LPRO` | `..._DEVICE_tenda_rx2lpro` | pattern proven by overlay's `rx23302.diffconfig:3` ↔ `image/rtl8197f.mk:11` |
| `kmod-rtl8192cd` | *dropped* | 0 hits for `rtl8192cd` in BASE tree listing; HAL is built-in at overlay `rtl8197f/config-6.18:238-246` |
| `kmod-ipt-offload` | `kmod-nft-offload` | BASE `netfilter.mk:327` vs `:1381`; hwnat is a `TC_SETUP_FT` flowtable backend |
| `qos-scripts` | *dropped* (qosify/sqm-scripts if ever) | BASE `qos-scripts/Makefile:24` still depends on `+iptables` |
| `miniupnpd` | `miniupnpd-nftables` | feed `net/miniupnpd/Makefile:35` `PROVIDES:=miniupnpd`, `:54-60` variants + `DEFAULT_VARIANT` |
| `luci-app-opkg` | `luci-app-package-manager` | feed has no `applications/luci-app-opkg`; has `luci-app-package-manager` |
| `cgi-io` | *dropped* | feed `net/cgi-io/Makefile:26` **still exists** at this pin — the 4.14 note in `feeds.conf` that master "dropped cgi-io" is wrong and should be corrected when the pins are replaced |
| `# CONFIG_KERNEL_CRASHLOG is not set` | *dropped* | no `CONFIG_CRASHLOG` line in BASE `generic/config-6.18`; no crashlog patch in `hack-6.18/` |
| `ppp`, `ppp-mod-pppoe`, `odhcp6c`, `odhcpd-ipv6only`, `tcpdump-mini`, `iperf3`, `kmod-leds-gpio`, `kmod-gpio-button-hotplug`, `swconfig`, `iwinfo`, `luci`, `uhttpd`, `luci-mod-admin-full` | unchanged | BASE `services/ppp`, `ipv6/odhcp6c`, `services/odhcpd/Makefile:53`, `utils/tcpdump/Makefile:41`, feed `net/iperf3`, `modules/leds.mk:10`, `package/kernel/gpio-button-hotplug/Makefile:11`, `modules/netdevices.mk:1040`, BASE `iwinfo` (overlay adds the wext backend for our target), feed `collections/luci`, BASE `services/uhttpd`, feed `modules/luci-mod-admin-full` |

Symbols I could NOT find in any tree available locally, and where I looked
(the honest list; each needs CI or a feed lookup at merge time):

1. `CONFIG_TARGET_rtl819x_rtl8197f_DEVICE_tenda_rx2lpro` — exists only once track 02/03
   adds a `Device/` block. Checked: `grep -rl DEVICE_tenda` over
   `/tmp/rebase/openwrt-rx23302/openwrt`, `/tmp/rebase-base`,
   `/tmp/rebase/{iptime,hh71vm,mr1500x}-openwrt`, our `files/` — 0 hits. `build.sh.new`
   fails the build if no `DEVICE_*` is selected rather than producing an empty run.
2. `CONFIG_PACKAGE_rtlmib` — the *package* exists
   (`/tmp/rebase/openwrt-rx23302/openwrt/package/utils/rtlmib/Makefile`, plus
   `target/linux/rtl819x/Makefile:25` puts it in DEFAULT_PACKAGES), but no tree contains
   the literal string `PACKAGE_rtlmib`; my checker greps symbol text, so it is unproven
   until `make defconfig` in CI.
3. `CONFIG_PACKAGE_miniupnpd-nftables` — proven from the feed Makefile structure
   (variants above), not from a rendered config. Same caveat.
4. `CONFIG_KALLSYMS` / `CONFIG_LEDS_GPIO` / `CONFIG_PCI` — present in BASE
   (`generic/config-6.18:3181`, `:3322`, and `PCI` off at overlay `rtl8197f/config-6.18:244`)
   but they are *decisions*, not facts; they are in the delta file, not asserted in the seed.

Machine-checked: all 37 `CONFIG_*=` lines of `seed.config.new` were grepped against the
overlay tree, BASE, `/tmp/rebase-base` (6.6 tree) and our `files/`; the three above are
the only misses.

## Risks / gotchas found while auditing

1. **Cross-target regression waiting to happen.** Our
   `files/target/linux/generic/files/drivers/mtd/mtdsplit/{Kconfig,Makefile}` are
   whole-file replacements of a *shared* directory: ours declares 16 symbols, BASE
   declares 22; copying 4.14's deletes
   `MTD_SPLIT_{BCM63XX,CFE_BOOTFS,ELF,H3C_VFS,SEIL,MSTC_BOOT,OPENWRT_PROLOG}_FW`.
   Fix proposed in the delta file §1: ship only `mtdsplit_cvimg.c` (additive) + a patch
   appending the two stanzas — legal because `include/quilt.mk:92,96` copies `files/`
   *before* `:102-105` applies the patch dirs. **Owner: track 02.**
2. **`iwpriv` does not exist in this feed pin** (no `net/wireless-tools` at
   `712ee13a`). Stock's 2.4 GHz calibration path is `iwpriv wlan0 flash_set ...`; the
   overlay answers this with `package/utils/rtlmib`. **Owner: track 05 — confirm the
   RF-calibration glue uses rtlmib, or we must add wireless-tools back as a package.**
3. **A built-in driver against a modular flowtable.** `CONFIG_NET_RTL819X=y`
   (overlay `:225`) builds `rtl819x_hwnat.c` in; `NF_FLOW_TABLE` comes from
   kmod-nft-offload. If the first CI build complains, switch NET_RTL819X to `=m` and
   add a kmod — do not patch around it.
4. **PCI is off** in the overlay subtarget config (`:244`) while `PCI_HCI=y` (`:243`);
   fine for the embedded 2.4 GHz radio, wrong for PCIe RTL8832BR. **Owner: track 06.**
5. **FACTS.md §1 contradicts the measured flash map**: it says "8 MiB SPI NOR", but the
   `/proc/mtd`-derived table (README) and our `IMAGE_SIZE := 15872k` (`0xF80000` KernelFS
   at `0x40000`) say **16 MiB**. It changes the size budget in the delta file
   (`CONFIG_KALLSYMS`, `CONFIG_DEBUG_INFO`) and the small-flash choices. Please correct
   FACTS.md — I did not silently adopt either number.
6. `DEFAULT_PACKAGES.router` at BASE already ships firewall4, nftables, kmod-nft-offload,
   odhcp6c, odhcpd-ipv6only, ppp, ppp-mod-pppoe (`include/target.mk:52-60`), so those
   seed lines are intent-documentation, not selection — worth knowing before someone
   "removes the duplication".
7. CI disk: a 6.18 master build plus `CONFIG_DEBUG_INFO=y` (overlay
   `rtl8197f/config-6.18:37`) fits a runner only after the dotnet/android/ghc cleanup
   step added in `build.yml.new`. If the overlay later drops DEBUG_INFO, keep the step —
   the base tree clone is shallow now (fetch-by-SHA `--depth 1`), which is what bought
   the headroom back.

## Needs from elsewhere

* **track 02 (images)** — the `Device/` profile name (I assume `tenda_rx2lpro`); fix the
  mtdsplit cross-target file copies (risk 1); confirm the produced filenames still match
  `*factory-cr6c.bin` / `*tenda-plaintext.bin`, which is what `build.sh.new`'s
  verification glob and the "no container produced" gate key on. Output dir expected at
  `bin/targets/rtl819x/rtl8197f` (`BOARD:=rtl819x` × `SUBTARGETS:=rtl8197f`).
* **track 02/03 (dts)** — `files/target/linux/realtek/**` must move to
  `files/target/linux/rtl819x/**`: `RX2LPRO.dts` → `dts/rtl8197f_tenda_rx2lpro.dts`
  (their `Device/Default` sets `DEVICE_DTS = $(SOC)_$(1)`, `image/Makefile:100`, so the
  file name is derived from the profile), and the old `files-4.14/` tree is superseded by
  the overlay's `files-6.18/` — keep only what genuinely differs, or the clobber report
  will be unreadable.
* **track 05 (2.4 GHz)** — answer risk 2 (rtlmib vs iwpriv); if the HAL is to be a
  module, also change the seed and `config-6.18` (§3 of the delta) together.
* **track 06 (5 GHz)** — delta §8 (PCI) before any 8832BR attempt.
* **orchestrator** — the integration order that makes CI green on the first try:
  (1) move `files/target/linux/realtek/**` → `files/target/linux/rtl819x/**`
  (otherwise `build.sh.new` stops at the stale-target guard, by design);
  (2) delete `files/target/linux/generic/hack-4.14/` — dead on this base (patch
  dirs are `pending-6.18`/`backport-6.18`/`hack-6.18`), and its
  `650-netfilter-add-xt_OFFLOAD-target.patch` is not the symbol the new hwnat
  uses; (3) resync `files/target/linux/generic/files/drivers/mtd/mtdsplit/` per
  risk 1; (4) only then swap the four root files from this directory.
* **orchestrator** — add `/.port-cache/`? Not needed: the overlay clone lives inside the
  already-ignored `/openwrt/`. Do **not** add it to `.gitignore` under the repo root, or
  someone will eventually vendor it by accident. Also: `build.sh.new` must be committed
  executable, and `tools/check-cr6c.py` is still invoked with
  `--expect-start 0x81000000` (our loader link address, `image/Makefile:27`).
* **orchestrator** — `feeds.conf`'s "master dropped cgi-io / miniupnpd does not resolve"
  story is now false on this pin and `feeds.conf.new` says so; the 19.07 branches are gone
  from the picture entirely.
