#!/usr/bin/env bash
# Build OpenWrt for the Tenda RX2L Pro (Realtek RTL8197F, 8 MiB? no: 16 MiB SPI
# NOR, 128 MiB RAM) on the 6.18 base.
#
# Two clones make a build, in this order:
#
#   1. upstream openwrt/openwrt @ BASE_REV          (the tree; kernel 6.18.41)
#   2. Elengal/openwrt-rx23302 @ OVERLAY_REV        (the platform: target/linux/
#      rtl819x, tools/cvimg, rtl8192fe HAL, swconfig rtl8367b, image pipeline)
#   3. ./files/                                     (this port: our board, our
#      geometry, our userspace)
#   4. port/*/overlay, if any                       (staged track output, see
#      EXTRA_OVERLAY_DIRS)
#
# Layer 2 is applied at build time, not vendored into this repo: it is ~1700
# files, most of them the vendor Wi-Fi HAL, and its own provenance/licensing
# story (docs/sources.md in that repo) stays intact at one pinned commit. We
# still have to edit several of its files, so layer 3 copies WHOLE FILES over
# the overlay's and this script prints every path where that happens -- a
# silent "our copy won" is exactly how an upstream fix gets lost (see the
# clobber report below).
#
# RTL8197F is still NOT in upstream OpenWrt: mainline's "realtek" target is the
# RTL838x/930x/931x managed switches and is big-endian, our strap is little-endian
# (FACTS.md §2). The rtl819x target here is a separate target with ARCH:=mipsel.
#
#   ./build.sh                            # full build
#   SKIP_FEEDS=1 ./build.sh               # reuse the already-updated feeds checkout
#   PROFILE=~/rx2lpro-profile ./build.sh  # bake a private preconfigured profile in
#   BIN=bin/targets/rtl819x/rtl8197f ./build.sh   # where to find the produced images
#
# ⚠ TWO OF THE THREE+ IMAGES WRITE FLASH. Back up the whole NOR first; the
# recovery net (the loader's disaster_web + the mtd1..mtd7 dumps) only helps if
# the dumps exist. Nothing here touches the router: this script only builds and
# verifies files on disk.
#
# Build environment: the container in ./Dockerfile (Debian 13 era). OpenWrt
# refuses to build as root and wants umask 022 and a case-sensitive fs.
set -e
cd "$(dirname "$0")"
SELF_DIR="$(pwd)"

# ---- pins -------------------------------------------------------------------
# BASE_REV is the commit rx23302 v0.2.1 was built and hardware-tested against
# (its versions.sh:5-12; kernel 6.18.41 comes from
# target/linux/generic/kernel-6.18 in that same tree). The feeds are pinned to
# the commits that tree was tested with, because a LuCI or packages tip can
# move under us and break the ABI mid-build; override them only as a set.
BASE_REPO="${BASE_REPO:-https://github.com/openwrt/openwrt}"
BASE_REV="${BASE_REV:-82e2e4f492d58b5b6afaf5b55d8286c58da9c224}"

OVERLAY_REPO="${OVERLAY_REPO:-https://github.com/Elengal/openwrt-rx23302}"
OVERLAY_REV="${OVERLAY_REV:-80e62b6c82396ddfa23bef560565af2b5e71686b}"

FEED_PACKAGES_REV="${FEED_PACKAGES_REV:-712ee13a837449fd91b5279048303d3a790d691b}"
FEED_LUCI_REV="${FEED_LUCI_REV:-4c0a4eda5ea17f9e81fc860366d7d04ce3c21b8e}"

BUILD_DIR="${BUILD_DIR:-openwrt}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

# ---- 1. base tree ------------------------------------------------------------
# Shallow-fetch one commit rather than clone ~15000 revisions: the CI step that
# matters is the compile. GitHub answers fetch-by-SHA; a mirror that does not
# gets the slow full clone instead. The clone lives under .gitignore'd
# /openwrt/, so nothing in it can be accidentally committed here.
[ -d "$BUILD_DIR/.git" ] || git init -q "$BUILD_DIR"
cd "$BUILD_DIR"
git remote add origin "$BASE_REPO" 2>/dev/null || git remote set-url origin "$BASE_REPO"
git config advice.detachedHead false
if ! git fetch -q --depth 1 origin "$BASE_REV" 2>/dev/null; then
	echo ">>> shallow fetch of $BASE_REV refused by $BASE_REPO; falling back to a full clone"
	git fetch -q --unshallow origin 2>/dev/null || git fetch -q origin
fi
# checkout -f: a previous interrupted run can leave overlay edits in the tree,
# and a half-applied layer is worse than a re-applied one.
git checkout -q -f "$BASE_REV"
echo ">>> base: $BASE_REPO @ $BASE_REV"

# ---- 2. platform overlay -----------------------------------------------------
# Cached INSIDE the base tree (hence gitignored) so an accidental `git add -A`
# in this repo can never stage ~40 MB of someone else's GPL drop.
# Absolute, because the stamp below is computed from inside
# "$OVERLAY_CACHE/openwrt": a relative path there resolves to
# .../openwrt/.port-cache/... and the patch silently drops out of the stamp,
# so a patch-only change would keep a warm-but-wrong tree.
OVERLAY_CACHE="$PWD/.port-cache/openwrt-rx23302"
[ -d "$OVERLAY_CACHE/.git" ] || git clone -q "$OVERLAY_REPO" "$OVERLAY_CACHE"
git -C "$OVERLAY_CACHE" fetch -q origin "$OVERLAY_REV" 2>/dev/null || true
git -C "$OVERLAY_CACHE" checkout -q -f "$OVERLAY_REV"

# Stamp = base rev + overlay content + openwrt-upstream.patch. If any byte of
# the port changes, the layers are re-applied from a clean tree; if nothing
# changed, the compile cache in build_dir/staging_dir stays warm.
OV_STAMP="$( (cd "$OVERLAY_CACHE/openwrt" && find . -type f -print0 | sort -z | xargs -0 sha256sum 2>/dev/null; \
	sha256sum "$OVERLAY_CACHE/openwrt-upstream.patch") | sha256sum | cut -d' ' -f1)"
STAMPFILE=".rx2l-applied"
if [ ! -f "$STAMPFILE" ] || [ "$(cat "$STAMPFILE")" != "$BASE_REV:$OV_STAMP" ]; then
	echo ">>> applying overlay $OVERLAY_REV (openwrt/ + openwrt-upstream.patch)"
	# Only the directories the overlay writes into are cleaned. `git clean -fdq`
	# over the whole tree would also delete package/feeds/* (the feed symlinks,
	# untracked) and bin/, and a SKIP_FEEDS run would then build with no feeds
	# at all -- the "image with no web UI, no error anywhere" failure again.
	git clean -fdq -- target tools package scripts include feeds-patches
	cp -a "$OVERLAY_CACHE"/openwrt/. .
	# Three of the four hunks in that patch are things we cannot carry as whole
	# files (they edit upstream files we do not otherwise own):
	#   include/package.mk      -> feeds-patches/, so feed packages (LuCI) can
	#                              take our rtl8192cd patch without writing into
	#                              a pinned feed checkout;
	#   netifd utils.uc         -> the wireless-handler loop that spins at 100%
	#                              CPU on EOF (their fix, upstream PR #25851).
	#                              We DO ship /lib/netifd/wireless/rtl8192cd.sh,
	#                              so without this hunk netifd hangs on first
	#                              boot -- and wireless bring-up fails whole;
	#   iwinfo Makefile + tools/Makefile cvimg -> wext backend for our target,
	#                              and the cvimg host tool our cr6c recipe needs.
	git apply --check "$OVERLAY_CACHE/openwrt-upstream.patch" || {
		echo "ERROR: openwrt-upstream.patch no longer applies cleanly onto" >&2
		echo "       $BASE_REPO @ $BASE_REV. Re-roll it, do not force it:" >&2
		echo "       the netifd hunk is a hang fix, not a nicety." >&2
		exit 1
	}
	git apply "$OVERLAY_CACHE/openwrt-upstream.patch"
	echo "$BASE_REV:$OV_STAMP" > "$STAMPFILE"
else
	echo ">>> overlay unchanged - re-application skipped"
fi

# ---- 3. this port's overlay --------------------------------------------------
# Guard, not ceremony: our 4.14 overlay is rooted at target/linux/realtek, and
# OpenWrt discovers targets by scanning target/linux/*/Makefile. Copying that
# directory into this base would register a SECOND, half-populated realtek
# target (no config-6.18, no image/Makefile at 6.18) whose failure mode is a
# confusing silentoldconfig error, not a clean "wrong target". Tracks 02/03 move
# the paths; until they do, this says so in one line.
STALE="$( (cd "$SELF_DIR/files/target/linux" && ls -d */ 2>/dev/null) \
	| grep -vE '^(rtl819x|generic)/$' | tr -d '/' | tr '\n' ' ' || true)"
if [ -n "${STALE// /}" ]; then
	echo "ERROR: files/target/linux/ still carries the 4.14-era target dir(s): $STALE" >&2
	echo "       On this base the platform target is rtl819x; move the content to" >&2
	echo "       files/target/linux/rtl819x/ (dts, image, base-files) and delete the" >&2
	echo "       old tree, or it registers as a broken second target." >&2
	exit 1
fi

echo ">>> overlaying device support from $SELF_DIR/files/"
cp -a "$SELF_DIR"/files/. .

# Where our copy replaces one of the overlay's files, say so out loud. The
# device profile lives in target/linux/rtl819x/image/rtl8197f.mk, which is the
# overlay's file too (it carries Device/electra_rx23302): if they add something
# there in a later OVERLAY_REV, our whole-file copy drops it silently, and the
# only symptom is a missing image or a lost boot fix. Review this list on every
# pin bump.
CLOBBERED=0
if [ -d "$SELF_DIR/files" ]; then
	while IFS= read -r rel; do
		[ -f "$OVERLAY_CACHE/openwrt/$rel" ] || continue
		if ! cmp -s "$SELF_DIR/files/$rel" "$OVERLAY_CACHE/openwrt/$rel"; then
			echo "★ files/ overrides overlay file: $rel"
			CLOBBERED=$((CLOBBERED + 1))
		fi
	done < <(cd "$SELF_DIR/files" && find . -type f | sed 's|^\./||' | sort)
	[ $CLOBBERED -gt 0 ] && echo "★ $CLOBBERED file(s) taken from files/ instead of the overlay"
fi

# ---- 3b. staged track output (optional) --------------------------------------
# Tracks under port/NN-*/ may stage tree content in an overlay/ directory; it
# lands AFTER ./files/ so a track can revise a file this repo also ships.
EXTRA_OVERLAY_DIRS="${EXTRA_OVERLAY_DIRS:-}"
if [ -z "$EXTRA_OVERLAY_DIRS" ]; then
	for d in "$SELF_DIR"/port/*/overlay; do
		[ -d "$d" ] && EXTRA_OVERLAY_DIRS="$EXTRA_OVERLAY_DIRS $d"
	done
fi
for d in $EXTRA_OVERLAY_DIRS; do
	echo ">>> overlaying staged content from $d"
	cp -a "$d"/. .
done

# ---- optional private profile -----------------------------------------------
# OpenWrt copies ./files/ into the rootfs, so a profile directory holding
# etc/config/{network,wireless,firewall,dhcp} bakes the real topology and PSK
# into the image. It lives OUTSIDE this repo; only its path is passed in.
if [ -n "${PROFILE:-}" ]; then
	[ -d "$PROFILE/files" ] || { echo "ERROR: PROFILE=$PROFILE has no files/ dir." >&2; exit 1; }
	echo ">>> PROFILE=$PROFILE: baking private profile into the image"
	mkdir -p files
	cp -a "$PROFILE"/files/. files/
	# git records only the exec bit; a group-writable /etc/dropbear makes
	# dropbear reject the dir and lock SSH out.
	find files -type d -exec chmod 755 {} +
	find files -type f -exec chmod 644 {} +
	grep -rlIZ '^#!' files 2>/dev/null | xargs -0 -r chmod 755
	[ -f files/etc/dropbear/authorized_keys ] && chmod 600 files/etc/dropbear/authorized_keys
fi

# ---- exec bits ----------------------------------------------------------------
# Rootfs content that reached us through tar, a release archive, or Actions'
# checkout can lose its mode bits (this is what produced CI's exit 126 on the
# 4.14 line), and a 0644 /etc/init.d/* or /lib/upgrade/platform.sh does not
# fail the build -- it fails the boot, or silently skips the sysupgrade
# keep-list. Shebang is the honest signal for "this is a script"; the OpenWrt
# script directories are included because init/rc/busybox scripts there are
# invoked by path, and hotplug/uci-defaults files are run by name.
fix_exec_bits() {
	local root="$1"
	[ -d "$root" ] || return 0
	# The directory list is OpenWrt's own "invoked by path" convention: init.d
	# and rc.d are run by procd, uci-defaults by the first-boot runner,
	# hotplug.d by the hotplug agent, board.d by /etc/board.d, and
	# lib/netifd/wireless + lib/wifi by netifd itself (our rtl8192cd handler is
	# executed, not sourced -- a 0644 there means "no wifi device" with nothing
	# in the log). lib/upgrade/platform.sh is deliberately NOT here: it is
	# sourced, and 0644 is right for it.
	find "$root" -type d \( -path '*/etc/init.d' -o -path '*/etc/rc.d' \
		-o -path '*/etc/uci-defaults' -o -path '*/etc/hotplug.d' \
		-o -path '*/etc/board.d' -o -path '*/etc/sysupgrade.d' \
		-o -path '*/lib/upgrade/keep.d' -o -path '*/lib/netifd/wireless' \
		-o -path '*/lib/wifi' -o -path '*/usr/bin' -o -path '*/usr/sbin' \
		-o -path '*/bin' -o -path '*/sbin' \) -prune \
		-exec find {} -type f \; 2>/dev/null | xargs -r -d '\n' chmod 755 || true
	grep -rlIZ '^#!' "$root" 2>/dev/null | xargs -0 -r chmod 755 || true
	# The tools this script itself calls, if they arrived as 0644 (see the
	# exit-126 post-mortem in the 4.14 line's history).
	chmod 755 ./build.sh ../build.sh ../tools/check-cr6c.py ../tests/test_container.py 2>/dev/null || true
}
fix_exec_bits files
# The overlay's base-files land outside ./files/, and they carry the wifi glue
# and the hwnat init script -- exactly the content a mode bit breaks.
fix_exec_bits target/linux/rtl819x/base-files
# Deliberately NOT `fix_exec_bits .`: scanning the whole tree walks
# build_dir/staging_dir (100k+ files on a warm rerun) and the overlay cache, to
# re-assert bits that the base's own git checkout already carries correctly.

# ---- feeds + seed -------------------------------------------------------------
# feeds.conf.new is what carries the gotchas (see that file): the two feeds are
# pinned by commit with the '^rev' selector, and every comment line starts with
# '#!' because scripts/feeds strips comments with s/#.+$// and then calls a
# bare '#' a syntax error.
# NOTE the shape of these two blocks. The obvious one-liner
#	FEEDS="${FEEDS:-$SELF_DIR/feeds.conf.new}"
#	[ -f "$FEEDS" ] || FEEDS="${FEEDS:-$SELF_DIR/feeds.conf}"
# NEVER falls back: the first line has already set FEEDS to a non-empty value,
# so the "${FEEDS:-...}" on the second line expands to itself. With no
# feeds.conf.new in the tree that leaves FEEDS pointing at a file that does not
# exist and the build dies at `cp "$FEEDS" feeds.conf` -- 90 seconds in, after
# the base clone, which is how the 08:37 run failed.
if [ -z "${FEEDS:-}" ]; then
	if [ -f "$SELF_DIR/feeds.conf.new" ]; then
		FEEDS="$SELF_DIR/feeds.conf.new"
	else
		FEEDS="$SELF_DIR/feeds.conf"
	fi
fi
if [ -z "${SEED:-}" ]; then
	if [ -f "$SELF_DIR/seed.config.new" ]; then
		SEED="$SELF_DIR/seed.config.new"
	else
		SEED="$SELF_DIR/seed.config"
	fi
fi
[ -f "$FEEDS" ] || { echo "ERROR: no feeds file at $FEEDS" >&2; exit 1; }
[ -f "$SEED" ] || { echo "ERROR: no seed file at $SEED" >&2; exit 1; }

# Default to installing EVERYTHING from both feeds, which is what the base's
# own build.sh does, and why ours can stop lying: with a curated list, a symbol
# that was never installed is dropped by `make defconfig` SILENTLY -- the
# failure the 4.14 build.sh spelled out in capitals ("no web UI and no error
# anywhere"). The list this replaces was also wrong on the new base: `luci-app-opkg` is now
# luci-app-package-manager, `miniupnpd` is now only
# Package/miniupnpd-{iptables,nftables} (feed net/miniupnpd/Makefile:42,54),
# and `qos-scripts` is an iptables-era package that firewall4 does not drive.
# Set FEEDS_INSTALL to a name list if you want the old behaviour back; word
# splitting is intentional, so no quotes.
FEEDS_INSTALL="${FEEDS_INSTALL:--a}"

cp "$FEEDS" feeds.conf
# The pins live in feeds.conf, together with the '#!' comment rule that file
# explains. An override therefore rewrites them THERE instead of fighting the
# file: substitute the SHA after the '^' selector for each named feed.
sed -i -E "s|^(src-git +packages +[^ ^]+)\^[^ ^]+|\1^$FEED_PACKAGES_REV|" feeds.conf
sed -i -E "s|^(src-git +luci +[^ ^]+)\^[^ ^]+|\1^$FEED_LUCI_REV|" feeds.conf
# One-second gate, deliberately before `update`: scripts/feeds parses the file
# at line 971, before any command runs, and a bare '#' comment line dies with
# "Syntax error in feeds.conf, line N" -- but only after the feeds have been
# cloned, which is minutes of CI and an hour of a workstation.
./scripts/feeds list -n >/dev/null || {
	echo "ERROR: feeds.conf is not parseable by scripts/feeds." >&2
	echo "       Every comment line must start with '#!' (s/#.+\$// needs" >&2
	echo "       at least one character after the hash). See the header." >&2
	exit 1
}
if [ -z "${SKIP_FEEDS:-}" ]; then
	./scripts/feeds update -a
fi
./scripts/feeds install $FEEDS_INSTALL

cp "$SEED" .config
make defconfig

# ---- defconfig gate ------------------------------------------------------------
# `make defconfig` drops a symbol it cannot resolve and reports nothing. These
# are the ones whose absence is a broken router rather than a smaller one, so
# they are checked HERE, before hours of compiling. Names are as they resolve at
# BASE_REV + overlay; if a track renames the device profile, update this list
# with it (port/README.md: track 01 owns the seed).
rc=0
# When a symbol goes missing, say WHY. kconfig drops a package without a word,
# and the reason is always in its own DEPENDS line or in the resolved state of
# one of those dependencies -- both of which cost nothing to print here and cost
# a 4-minute CI round trip to discover otherwise.
why_dropped() {
	pkg="${1#CONFIG_PACKAGE_}"; pkg="${pkg%=y}"
	mk="$(grep -rl --include=Makefile -E "^ *define Package/${pkg}\$" \
		package feeds 2>/dev/null | head -1)"
	if [ -n "$mk" ]; then
		echo "    defined in: $mk" >&2
		awk -v p="define Package/${pkg}" '
			index($0,p){f=1}
			f && /DEPENDS/ {print "    "$0; exit}
			f && /^endef/ {exit}' "$mk" >&2
		# and the resolved state of each dependency we can name
		for dep in $(awk -v p="define Package/${pkg}" '
				index($0,p){f=1}
				f && /DEPENDS/ {gsub(/[+:=]|@|\|\||&&|\(|\)/," ");print;exit}
				f && /^endef/ {exit}' "$mk" 2>/dev/null); do
			grep -qE "^(# )?CONFIG_PACKAGE_${dep}(=| is not set)" .config \
				&& grep -E "^(# )?CONFIG_PACKAGE_${dep}(=| is not set)" .config \
				| sed 's/^/    dep /' >&2
		done
	else
		echo "    no Makefile in this tree defines Package/${pkg}" >&2
	fi
}
for sym in \
	"CONFIG_TARGET_rtl819x_rtl8197f=y" \
	"CONFIG_PACKAGE_luci=y" \
	"CONFIG_PACKAGE_uhttpd=y" \
	"CONFIG_PACKAGE_firewall4=y" \
	"CONFIG_PACKAGE_wifi-scripts=y"
do
	grep -qx "$sym" .config || {
		echo "!!! defconfig did not keep $sym" >&2
		why_dropped "$sym"
		rc=1
	}
done
grep -q '^CONFIG_TARGET_rtl819x_rtl8197f_DEVICE_' .config || {
	echo "!!! no DEVICE_* profile selected - the image recipe (track 02/03) and" >&2
	echo "    the seed disagree; nothing would be built for this board." >&2
	rc=1
}
[ $rc -eq 0 ] || { echo "SEED DOES NOT RESOLVE ON THIS BASE - fix the seed, not the check." >&2; exit 1; }

# ---- build ---------------------------------------------------------------------
make -j"$JOBS" download
make -j"$JOBS"

# ---- verify the produced containers --------------------------------------------
# The board's boot contract is header-driven: 'cr6c'/'cs6c' magic at +0 and an
# additive BE-u16 checksum over len bytes from +0x10 summing to 0 (FACTS.md §3).
# A container that misses it bricks to the loader's TFTP prompt, so an image is
# not "built" until it passes.
BIN="${BIN:-bin/targets/rtl819x/rtl8197f}"
echo
echo ">>> verifying every produced container against the vendor spec"
found=0
for f in $BIN/*factory-cr6c.bin $BIN/*tenda-plaintext.bin $BIN/*.itb; do
	[ -e "$f" ] || continue
	found=1
	case "$f" in
	*tenda-plaintext.bin*) args="--tenda" ;;
	*) args="" ;;
	esac
	# The container we build declares our loader's link address (LZMA_TEXT_START
	# in target/linux/rtl819x/image/Makefile:27 == CVIMG_LOADER_START_ADDR in
	# the device profile), not the vendor's 0x80d00000.
	if ! python3 "$SELF_DIR/tools/check-cr6c.py" $args \
		--expect-start "${EXPECT_START:-0x81000000}" "$f"; then
		echo "!!! $f FAILED verification"
		rc=1
	fi
	echo
done
if [ $found -eq 0 ]; then
	echo "!!! no factory-cr6c/tenda-plaintext container under $BIN - the image" >&2
	echo "    recipe produced nothing flashable. Check the DEVICE_* profile name" >&2
	echo "    and IMAGE/* entries before trusting this build." >&2
	exit 1
fi
[ $rc -eq 0 ] || { echo "BUILD PRODUCED A REJECTED IMAGE - do not flash it." >&2; exit 1; }

sha256sum $BIN/*.bin $BIN/*.itb 2>/dev/null | tee "$SELF_DIR/MANIFEST-built.txt" || true
echo
echo "Build complete. Images are in $(pwd)/$BIN/:"
echo "  *initramfs-kernel.bin      RAM only, never writes flash - iterate here first"
echo "  *factory-cr6c.bin          the bare cvimg container (loader AUTOBURN / TFTP)"
echo "  *tenda-plaintext.bin       same container behind the 64-byte Tenda header"
echo "  *sysupgrade.bin            OpenWrt sysupgrade"
echo
echo "⚠ All but the initramfs REPLACE stock firmware. See README 'Install paths'."
