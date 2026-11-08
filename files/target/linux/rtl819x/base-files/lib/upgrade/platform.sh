#
# Copyright (C) 2011 OpenWrt.org
#

. /lib/functions.sh
. /lib/functions/system.sh

PART_NAME=firmware

platform_check_image() {
	local board=$(board_name)

	# board_name on this base is the FIRST string of the DTS `compatible`
	# property (package/base-files/files/lib/preinit/02_sysinfo:
	# `strings /proc/device-tree/compatible | head -1`), i.e. "tenda,rx2l-pro"
	# -- not the bare "rx2l-pro" the 4.14 target used to write from
	# /proc/cpuinfo via realtek_board_detect(). That helper is gone: nothing
	# called it any more, so the old spelling could never match and this
	# cr6c gate was silently skipped (the case fell through to `return 0`).
	# Match both spellings so a future compatible change cannot re-open that.
	case "$board" in
	rx2l-pro|*rx2l-pro)
		# The RX2L Pro's KernelFS is a cvimg image signed with the 'cr6c'
		# magic; the vendor's own images carry exactly this header, so a
		# wrong-target image is the realistic failure mode, not a corrupt one.
		local magic
		magic=$(get_magic_long "$1")
		if [ "$magic" != "63723663" ]; then
			echo "Invalid image: expected cr6c firmware header, got 0x$magic"
			return 1
		fi

		# The magic alone says "right target", not "intact". Check the second
		# half of the container contract BEFORE anything is erased: the rootfs
		# superblock must sit at exactly 16+len, and be a plain `hsqs` (our
		# kernel has no vendor magic patch). A truncated download or a
		# mis-built image otherwise writes a container the loader refuses,
		# which is recoverable only through the bootloader monitor.
		#
		# dd seeks for skip=, so this costs no reads; it reads 4 bytes at two
		# offsets. Values are big-endian, so hexdump's byte order is used
		# rather than od's host-order words.
		local lenb len roff rmagic
		lenb=$(dd if="$1" bs=1 skip=12 count=4 2>/dev/null |
			hexdump -v -e '4/1 "%02x"')
		case "$lenb" in
		""|*[!0-9a-f]*)
			echo "Invalid image: cannot read the container length"
			return 1
			;;
		esac
		len=$((0x$lenb))
		if [ "$len" -lt 16 ] || [ "$((16 + len))" -gt "$(wc -c <"$1")" ]; then
			echo "Invalid image: container length 0x$lenb does not fit the file"
			return 1
		fi
		roff=$((16 + len))
		rmagic=$(dd if="$1" bs=1 skip=$roff count=4 2>/dev/null |
			hexdump -v -e '4/1 "%02x"')
		case "$rmagic" in
		68737173|73717368) ;;	# hsqs / sqsh
		*)
			echo "Invalid image: no squashfs at 16+len ($(printf '0x%x' "$roff"))," \
				"got 0x$rmagic"
			return 1
			;;
		esac
		return 0
		;;
	gwr1200ac-v1)
		# DIR-842 R1: the on-flash firmware is a Realtek "cr6b" image
		# (16-byte header the v3.4.11B loader validates + boots). Reject
		# anything without that signature so a wrong-target image can't be
		# written. The header is at offset 0 even when sysupgrade metadata
		# is appended (metadata rides at the tail), so offset-0 is robust.
		local magic
		magic=$(get_magic_long "$1")
		if [ "$magic" != "63723662" ]; then
			echo "Invalid image: expected cr6b firmware header, got 0x$magic"
			return 1
		fi
		return 0
		;;
	esac

	return 0
}

platform_do_upgrade() {
	# Writes the image to the "firmware" mtd (PART_NAME). The per-device MAC
	# and RF calibration live in separate read-only partitions (boot/MAC) and
	# are never touched; config is preserved by the sysupgrade framework.
	default_do_upgrade "$1"
}
