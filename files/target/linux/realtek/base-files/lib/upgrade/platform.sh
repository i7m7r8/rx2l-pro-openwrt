#
# Copyright (C) 2011 OpenWrt.org
#

. /lib/functions.sh
. /lib/functions/system.sh
. /lib/realtek.sh

PART_NAME=firmware

platform_check_image() {
	local board=$(board_name)

	case "$board" in
	rx2l-pro)
		# The RX2L Pro's KernelFS is a cvimg image signed with the 'cr6c'
		# magic; the vendor's own images carry exactly this header, so a
		# wrong-target image is the realistic failure mode, not a corrupt one.
		local magic
		magic=$(get_magic_long "$1")
		if [ "$magic" != "63723663" ]; then
			echo "Invalid image: expected cr6c firmware header, got 0x$magic"
			return 1
		fi
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
