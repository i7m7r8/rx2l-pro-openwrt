#!/usr/bin/env bash
# Build OpenWrt for the Tenda RX2L Pro (Realtek RTL8197F, 16 MiB SPI NOR).
#
# RTL8197F is NOT in upstream OpenWrt -- mainline's "realtek" target is the
# RTL838x/930x managed switches only. The only tree with an rtl8197f subtarget
# is the ggbruno fork, so this script overlays ./files/ (this port's device
# support) onto a pinned checkout of it and builds three images.
#
# ⚠ TWO OF THE THREE IMAGES WRITE FLASH. Back up all 16 MiB of NOR first --
# the recovery net (the loader's disaster_web + mtd1..mtd7 dumps) only helps
# if you have the dumps. Nothing here touches the router: this script only
# builds and verifies files on disk.
#
#   ./build.sh                      # full build
#   SKIP_FEEDS=1 ./build.sh         # reuse an already-updated feeds checkout
#   PROFILE=~/rx2lpro-profile ./build.sh   # bake a private preconfigured profile in
#
# Build environment: a Debian-11-era host, or the container in ./Dockerfile.
# A modern toolchain can fail to build this tree's host tools.
set -e
cd "$(dirname "$0")"
SELF_DIR="$(pwd)"

# ---- base tree ---------------------------------------------------------------
# Two facts drive these two variables:
#   * main builds against ggbruno/openwrt branch "Realtek" @ 8a0ccb9, kernel
#     4.14. That fork's newer branches (openwrt-22.03, openwrt-23.05, master)
#     carry only rtl838x/rtl839x/rtl930x/rtl931x in target/linux/realtek -- the
#     consumer rtl8197f subtarget exists ONLY on the 4.14 branch, verified via
#     the GitHub contents API, not inferred.
#   * upstream OpenWrt has never had RTL8197F router support at all, so a
#     current-kernel base has to come from a tree that implemented the platform.
# Both are overridable because this branch is exactly that experiment; leaving
# them hardcoded is what would make the 4.14 line unbuildable from here.
BASE_REPO="${BASE_REPO:-https://github.com/ggbruno/openwrt}"
BASE_REV="${BASE_REV:-8a0ccb93f3431bcf8f5c5d03d4acc2c8e442de67}"

[ -e openwrt ] || git clone "$BASE_REPO"
cd openwrt
git checkout "$BASE_REV"

echo ">>> overlaying device support from $SELF_DIR/files/"
cp -a "$SELF_DIR"/files/. .

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

# ---- feeds + seed ------------------------------------------------------------
# On main both are 19.07-era BY NECESSITY: the current luci feed is ucode-based
# and dies compiling ucode-mod-html / liblucihttp-ucode against a 4.14 base, and
# the packages feed must be pinned too because master split miniupnpd into
# miniupnpd-iptables/-nftables and dropped cgi-io (see feeds.conf).
# On a newer base the feed pins AND the package *names* change together, so all
# three inputs are overridable rather than hardcoded. The failure mode of
# getting this wrong is silent, which is why it is not just left as-is.
FEEDS="${FEEDS:-$SELF_DIR/feeds.conf}"
SEED="${SEED:-$SELF_DIR/seed.config}"
FEEDS_INSTALL="${FEEDS_INSTALL:-luci luci-base luci-mod-admin-full luci-theme-bootstrap luci-app-firewall luci-app-upnp luci-app-opkg luci-proto-ppp uhttpd uhttpd-mod-ubus cgi-io miniupnpd qos-scripts}"

cp "$FEEDS" feeds.conf
if [ -z "${SKIP_FEEDS:-}" ]; then
	./scripts/feeds update luci packages
fi
# ★ `luci` (the meta-package) must be in this list: seed.config selects
# CONFIG_PACKAGE_luci=y, and if the symbol does not exist at `make defconfig`
# time it is dropped SILENTLY, taking uhttpd and the admin pages with it --
# you get an image with no web UI and no error anywhere.
# Word-splitting is intentional here, so no quotes.
./scripts/feeds install $FEEDS_INSTALL

cp "$SEED" .config
make defconfig
make -j"$(nproc)"

BIN=bin/targets/realtek/rtl8197f
echo
echo ">>> verifying every produced container against the vendor spec"
rc=0
for f in $BIN/*factory-cr6c.bin $BIN/*tenda-plaintext.bin; do
	[ -e "$f" ] || continue
	case "$f" in
	*tenda-plaintext.bin*) args="--tenda" ;;
	*) args="" ;;
	esac
	# The container we build declares our loader's link address, not the
	# vendor's 0x80d00000.
	if ! python3 "$SELF_DIR/tools/check-cr6c.py" $args \
		--expect-start 0x81000000 "$f"; then
		echo "!!! $f FAILED verification"
		rc=1
	fi
	echo
done
[ $rc -eq 0 ] || { echo "BUILD PRODUCED A REJECTED IMAGE - do not flash it." >&2; exit 1; }

sha256sum $BIN/*.bin $BIN/*.itb 2>/dev/null | tee "$SELF_DIR/MANIFEST-built.txt" || true
echo
echo "Build complete. Images are in $(pwd)/$BIN/:"
echo "  *-initramfs-kernel.bin     RAM only, never writes flash - iterate here first"
echo "  *-factory-cr6c.bin         the bare cvimg container (loader AUTOBURN / TFTP)"
echo "  *-tenda-plaintext.bin      same container behind the 64-byte Tenda header"
echo "  *-sysupgrade.bin           OpenWrt sysupgrade"
echo
echo "⚠ All but the initramfs REPLACE stock firmware. See README 'Install paths'."
