#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# First-boot enumeration of the vendor rtl8192cd radios, for `wifi config`.
#
# PROVENANCE: structure and the phyBandSelect band test are taken from base A,
#   /tmp/rebase/openwrt-rx23302/openwrt/target/linux/rtl819x/base-files/lib/wifi/rtl8192cd.sh
# which itself credits sowarden/hh71vm-openwrt (commit 8881e78, GPL-2.0).
# Everything Tenda-specific below (CFG MAC keys, defaults, SSIDs, RU->none)
# is ours.
#
# WHY THIS FILE EXISTS ON THE NEW BASE
# ---------------------------------------------------------------------------
# `wifi config` on master runs the ucode detector (wifi-detect.uc +
# mac80211.uc), which only enumerates cfg80211 wiphys. Then /sbin/wifi does
#   include /lib/wifi
#   for driver in $DRIVERS; do detect_$driver; done
# (/tmp/rebase-base/package/network/config/wifi-scripts/files/sbin/wifi:70-81).
# The rtl8192cd HAL in its WEXT configuration registers no wiphy, so the ucode
# half produces nothing for it and `detect_rtl8192cd` below is the ONLY thing
# that creates the wifi-device stanza. Without it the image boots with no
# wireless at all -- and on this board that removes the documented fallback
# access path (see 02_network MODEL C).
#
# `wifi detect` is NOT the way any more: the same /sbin/wifi exits 1 with
# "Wifi detect is deprecated. Use wifi config instead" (:58-61).
#
# ★ THIS FILE IS THE ONLY WRITER OF THE STANZA. The per-unit radio MAC,
# width migration and the /etc/Wireless symlink live in
# etc/uci-defaults/07_wireless-2g-rx2lpro, and they touch different keys.
# Two writers for one option is the bug the 4.14 line already paid for
# (non-deterministic MACs across boots), so: creation here, policy there.

append DRIVERS "rtl8192cd"

rtl8192cd_radios() {
	local dir
	for dir in /proc/wlan[0-9]; do
		[ -d "$dir" ] || continue
		echo "${dir##*/}"
	done
}

# The driver's own answer about which band this netdev is, so nothing here
# depends on load order or on the vendor's self-contradictory wlan0/wlan1
# naming. 1 = 2.4 GHz, 2 = 5 GHz (rtl8192cd_is_5g in base A).
rtl8192cd_is_5g() {
	grep -qE 'phyBandSelect: *2' "/proc/$1/mib_rf" 2>/dev/null
}

rtl8192cd_known() {
	local ifname
	config_get ifname "$1" ifname
	[ "$ifname" = "$2" ] && known=1
}

detect_rtl8192cd() {
	local dev devidx band channel htmode hwmode ssid

	config_load wireless
	for dev in $(rtl8192cd_radios); do
		known=0
		config_foreach rtl8192cd_known wifi-device "$dev"
		[ "$known" = 1 ] && continue

		devidx="${dev#wlan}"
		if rtl8192cd_is_5g "$dev"; then
			band=5g; channel=36; htmode=VHT80; hwmode=11a; ssid=Tenda-5G
		else
			band=2g; channel=6; htmode=HT20; hwmode=11g; ssid=Tenda-2.4G
		fi

		uci -q batch <<-EOF
			set wireless.radio${devidx}=wifi-device
			set wireless.radio${devidx}.type=rtl8192cd
			set wireless.radio${devidx}.phy=${dev}
			set wireless.radio${devidx}.ifname=${dev}
			set wireless.radio${devidx}.band=${band}
			set wireless.radio${devidx}.hwmode=${hwmode}
			set wireless.radio${devidx}.channel=${channel}
			set wireless.radio${devidx}.htmode=${htmode}
			set wireless.radio${devidx}.disabled=0
			set wireless.default_radio${devidx}=wifi-iface
			set wireless.default_radio${devidx}.device=radio${devidx}
			set wireless.default_radio${devidx}.network=lan
			set wireless.default_radio${devidx}.mode=ap
			set wireless.default_radio${devidx}.ssid=${ssid}
			set wireless.default_radio${devidx}.encryption=none
EOF
		uci -q commit wireless
	done

	return 0
}
