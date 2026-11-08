# SPDX-License-Identifier: GPL-2.0-only
#
# RX2L Pro sysupgrade. STAGED FOR THE 6.18 REBASE; replaces
# files/target/linux/realtek/base-files/lib/upgrade/platform.sh.
# See ../sysupgrade.md for the reasoning and the open questions.
#
# What is on the flash, and what a sysupgrade.bin therefore is:
#
#   mtd3 "KernelFS"  = the ONE writable partition, 0xf80000 at flash 0x40000,
#                      labelled "firmware" in dts/RX2LPRO.dts.
#   IMAGE/sysupgrade.bin = append-kernel | cvimg-kernel-cr6c | append-rootfs |
#                          cvimg-pad-rootfs | sysupgrade-tar | append-metadata
#   i.e. cr6c header (16 B) | kernel payload | 2-byte ones-complement sum |
#        squashfs | cvimg "-j" jffs2 marker section | tar | fwtool metadata.
#   Kernel and rootfs are NOT separate partitions: squashfs sits at the
#   variable offset 16 + payload_len, which is what mtdsplit_cvimg parses and
#   what mount_root then carves rootfs_data out of.
#
# Consequences written into the code below:
#   * there is no second bank to fall back to (FACTS.md §1: no KernelFS1/2),
#     so no ordering trick can give us rollback -- see the README/FACTS
#     contradiction in sysupgrade.md §4;
#   * every byte between the header and the jffs2 marker is one stream, so we
#     write it with one `mtd write` and validate the geometry BEFORE erasing
#     anything;
#   * the geometry check is not paranoia: if squashfs is not exactly at
#     16+len, the box boots to "VFS: Unable to mount root fs on
#     unknown-block(0,0)" and the only way back is the loader. That is README
#     risk #2 and it was a real bug in this repo (the ALIGN(len,4) off-by-two).

. /lib/functions.sh
. /lib/functions/system.sh
. /lib/realtek.sh

PART_NAME=firmware

# Stage2 runs from an initramfs: /lib/*.sh is copied in automatically by
# stage2's install_file list (so lib/realtek.sh comes with it), and
# mtd/fwtool/get_image are already there. `hexdump` is a busybox applet and
# the 6.18 busybox config is not 19.07's -- prove it once on a RAM boot
# (first-boot-checklist.md item S1) or replace rx2lpro_be32 with `od`.
RAMFS_COPY_BIN=""
RAMFS_COPY_DATA=""

RX2LPRO_PART_BYTES=$((0xf80000))	# mtd3 KernelFS, from the DTS
RX2LPRO_HDR_BYTES=16			# cr6c header
RX2LPRO_SUM_BYTES=2			# cvimg checksum trailing the payload
RX2LPRO_EXPECT_START=0x82000000		# == LZMA_TEXT_START, image/Makefile

rx2lpro_be32() {	# $1 = file, $2 = decimal offset -> hex value
	hexdump -v -n 4 -s "$2" -e '1/1 "%02x"' "$1" 2>/dev/null
}

# Strip the fwtool trailer (and the sysupgrade tar) before any byte offset is
# used: metadata rides at the END of the file, so offsets from 0 are still
# valid, but SIZE checks are not. Same idiom as the iptime port
# (a2004mu_get_image) and base A (fwtool -q -t -i /dev/null).
rx2lpro_payload() {
	get_image "$1" | fwtool -q -i /dev/null -T - 2>/dev/null
}

platform_check_image() {
	local img="$1" burn len sq magic off size hdr_start

	[ "$#" -gt 1 ] && {
		echo "Sysupgrade takes exactly one image file."
		return 1
	}

	rx2lpro_is_board || {
		echo "Sysupgrade is not supported on $(rx2lpro_board_name)."
		return 1
	}

	# The partition we are about to erase must actually be there. On a
	# mis-partitioned board find_mtd_part returns nothing and `mtd write`
	# would fail AFTER stage2 has already copied itself into RAM.
	[ -n "$(find_mtd_part "$PART_NAME")" ] || {
		echo "No '$PART_NAME' partition in /proc/mtd -- wrong image or wrong flash map."
		return 1
	}

	magic=$(get_magic_long "$img")
	case "$magic" in
	63723663) : ;;	# "cr6c"
	*)
		echo "Invalid image: expected cr6c firmware header, got 0x$magic."
		echo "Use openwrt-rtl819x-rtl8197f-tenda_rx2lpro-squashfs-sysupgrade.bin."
		return 1
		;;
	esac

	# start_addr must name OUR loader's link address. The vendor images
	# carry 0x80d00000; a vendor file would pass a magic test and then
	# never boot, because Tenda's stage-1 jumps to the header's value.
	hdr_start=$(rx2lpro_be32 "$img" 4)
	[ "$hdr_start" = "81000000" ] || {
		echo "Image start_addr 0x$hdr_start is not 0x$RX2LPRO_EXPECT_START - built for a different loader."
		return 1
	}

	# burn_addr (bytes 8..11) must name the offset of the partition we
	# write. NOTE: FACTS.md §3 says Tenda's own stage-1 reads start_addr and
	# ignores burn_addr (and byteswaps it), so this is a *build identity*
	# check, not a boot requirement -- base A uses the same field that way
	# (platform.sh:48-53 checks burn == 00030000).
	burn=$(rx2lpro_be32 "$img" 8)
	[ "$burn" = "00040000" ] || {
		echo "Image burn_addr 0x$burn is not 00040000 (mtd3 offset) - wrong target."
		return 1
	}

	len=$(rx2lpro_be32 "$img" 12)
	len=$((0x$len))
	[ "$len" -gt 0 ] && [ "$len" -lt $((RX2LPRO_PART_BYTES - 0x10000)) ] || {
		echo "Implausible kernel payload length $len in the cr6c header."
		return 1
	}

	# ★ THE check. squashfs must sit at exactly 16 + len: that is where
	# mtdsplit_cvimg looks first, and where a mismatch costs the boot.
	# bs=1 skip= is slow but unambiguous; the alternative (dd skip in blocks)
	# is exactly the arithmetic that bit us once already.
	off=$((RX2LPRO_HDR_BYTES + len))
	sq=$(rx2lpro_payload "$img" | dd bs=1 skip=$off count=4 2>/dev/null |
		hexdump -v -n 4 -e '1/1 "%02x"')
	# hsqs (LE) or sqsh (BE); the vendor mangles theirs to "nice", OURS MUST
	# NOT be mangled -- README risk #4, and tools/check-cr6c.py gates it in CI.
	case "$sq" in
	68737173|73717368) : ;;
	*)
		echo "No squashfs superblock at 0x$(printf %x $off) (got 0x$sq)."
		echo "Root device would not be found at boot. Image rejected."
		return 1
		;;
	esac

	# Total payload must fit the partition, otherwise we write a truncated
	# image over a live system.
	size=$(rx2lpro_payload "$img" | wc -c)
	[ "$size" -gt 0 ] && [ "$size" -le "$RX2LPRO_PART_BYTES" ] || {
		echo "Image size $size does not fit '$PART_NAME' ($RX2LPRO_PART_BYTES bytes)."
		return 1
	}

	# The trailing cvimg "-j" jffs2 marker section is what becomes
	# rootfs_data and therefore where the config backup lands. Its presence
	# is a build-time guarantee of IMAGE/sysupgrade.bin (cvimg-pad-rootfs)
	# and is asserted by tools/check-cr6c.py in CI, not re-derived here --
	# re-scanning for deadc0de in stage2 risks a false positive inside the
	# squashfs. If a vendor-built file ever reaches here without it, the
	# upgrade still succeeds; the restore silently finds nothing.

	echo "Image OK: cr6c, start 0x$hdr_start, kernel $len bytes, rootfs at 0x$(printf %x $off), total $size."
	return 0
}

platform_do_upgrade() {
	local board=$(rx2lpro_board_name)

	case "$board" in
	tenda,rx2l-pro|rx2l-pro)
		# One stream into one partition: no kernel/rootfs ordering
		# trick is available (single bank, interleaved layout), so the
		# safety lives entirely in platform_check_image above.
		# default_do_upgrade does:
		#   get_image "$1" | mtd ${UPGRADE_BACKUP:+-j "$UPGRADE_BACKUP"} \
		#                  write - "$PART_NAME"
		# -j writes the saved sysupgrade.tgz into the trailing jffs2
		# area the cvimg "-j" marker opened, which is what mount_root
		# formats as rootfs_data and what preinit unpacks on boot.
		sync
		default_do_upgrade "$1"
		;;
	*)
		echo "Nothing to do on $board."
		return 1
		;;
	esac
}

# Nothing board-specific to copy: the framework hands us $UPGRADE_BACKUP and
# default_do_upgrade already embeds it. Keeping an explicit hook is the
# base-A pattern (their platform_copy_config writes the tar into rootfs_data
# with `mtd jffs2write`) and it is the right place to add a second slot if
# the dual-bank question in sysupgrade.md §4 ever resolves to "yes".
