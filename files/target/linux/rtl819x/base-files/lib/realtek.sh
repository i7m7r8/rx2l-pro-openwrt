#!/bin/sh
#
# Copyright (C) 2009-2011 OpenWrt.org
#
# ★ This is the ggbruno base tree's file, copied whole and extended by one
# case entry. The overlay is applied with `cp -a files/. .`, which REPLACES
# whatever is at this path -- so a partial file here would silently delete
# every other board's detection. Keep it in sync when bumping the base rev.
#
# ★ Why the RX2L Pro entry is load-bearing: detection matches the `machine`
# line of /proc/cpuinfo (which the 4.14 realtek kernel fills from the DTB
# `model`), and an unmatched machine string makes this function `return`
# WITHOUT writing /tmp/sysinfo/board_name. board_name then falls back to
# "generic", so board.d/02_network's `rx2l-pro` case never runs -- no LAN and
# no WAN interface are configured, and sysupgrade's SUPPORTED_DEVICES check
# fails too. That is indistinguishable from "the port has no ethernet".
# The model string in dts/RX2LPRO.dts is what this pattern matches; change
# one and you must change the other.
#

realtek_board_detect() {
	local machine
	local name

	machine=$(awk 'BEGIN{FS="[ \t]+:[ \t]"} /machine/ {print $2}' /proc/cpuinfo)

	case "$machine" in
	*"RX2L Pro")
		name="rx2l-pro"
		;;
	"GENERIC")
		name="rlx"
		;;
	*"DIR-815 D1")
		name="dir-815-d1"
		;;
	*"GWR-300N V1")
		name="gwr-300n-v1"
		;;
	*"RE172 V1")
		name="re172-v1"
		;;
	*"RE708 V1")
		name="re708-v1"
		;;
	*"GWR1200AC V1")
		name="gwr1200ac-v1"
		;;
	*"GWR1200AC V2")
		name="gwr1200ac-v2"
		;;
	*"ACTIONRG1200 V1")
		name="actionrg1200-v1"
		;;
	*"ACTIONRF1200 V1")
		name="actionrf1200-v1"
		;;
	esac

	# use generic board detect if no name is set
	[ -z "$name" ] && return

	[ -e "/tmp/sysinfo/" ] || mkdir -p "/tmp/sysinfo/"

	echo "$name" > /tmp/sysinfo/board_name
	echo "$machine" > /tmp/sysinfo/model
}

