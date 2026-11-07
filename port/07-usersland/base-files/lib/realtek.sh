# rx2lpro board-name / capability helpers.
#
# STAGED FOR THE 6.18 REBASE. Replaces files/target/linux/realtek/base-files/
# lib/realtek.sh (the ggbruno base tree's `realtek_board_detect()` plus one
# hand-added `*"RX2L Pro"` case).
#
# WHY THE OLD FILE IS DEAD WEIGHT ON THE NEW BASE
# ---------------------------------------------------------------------------
# The 4.14 realtek target wrote /tmp/sysinfo/board_name itself: `02_network`
# did `board=$(cat /tmp/sysinfo/board_name)`, and nothing else produced that
# file except `realtek_board_detect()`, which string-matched the `machine`
# line of /proc/cpuinfo. Miss a pattern and the board silently became
# `generic`, so 02_network configured nothing -- the #1 entry in README's
# first-boot risk table.
#
# Neither 6.x reference tree ships a `*_board_detect()` for this family:
#   * base A: /tmp/rebase/openwrt-rx23302/openwrt/target/linux/rtl819x/
#     base-files/ contains only board.d/02_network, lib/wifi, lib/netifd and
#     lib/upgrade -- no lib/*.sh at all -- yet its uci-defaults compare
#     `board_name` against `electra,rx23302` (uci-defaults/06_switch_vlan_ids:16).
#   * base B: /tmp/rebase-base/target/linux/realtek/base-files/etc/board.d/
#     02_network:21 uses `board=$(board_name)` and matches `tenda,ac23`,
#     `cudy,wr1500`, ... ; its only lib/ file is lib/functions/
#     rtl8197f-board.sh, which just *reads* `cat /tmp/sysinfo/board_name`.
# Conclusion: on the 6.18 base the name comes from the flattened devicetree
# (`/proc/device-tree/compatible`, first entry, `vendor,model`), the target
# does not own it, and the failure mode moved from "configure nothing" to
# "package/base-files falls back to 99-default_network (LAN=eth0)" -- safer.
#
# WHAT THIS FILE IS FOR NOW
# ---------------------------------------------------------------------------
# One thing: make the *rename* survivable. board names appear as literal
# strings in 02_network, three uci-defaults and three init.d scripts, and the
# rebase changes the convention (`rx2l-pro` -> `tenda,rx2l-pro`) at the same
# moment the DTS is rewritten. Matching through one helper means one place to
# fix, and it accepts both spellings plus the raw compatible, so a DTS/base
# mismatch degrades to "wrong board" instead of "no configuration".
#
# If the port later tracks upstream `realtek` (base B) instead of `rtl819x`,
# nothing here changes -- that is the point.

# Echo the canonical board name, resolving through every mechanism that could
# have produced it. Never returns empty: an empty name is how the 4.14 base
# silently lost its network config, so callers get `generic` and can log it.
rx2lpro_board_name() {
	local name

	name=$(board_name 2>/dev/null)
	[ -n "$name" ] && [ "$name" != "generic" ] || {
		# board_name() fell through to "generic": take the compatible
		# directly. NUL-separated list, first entry is the specific one.
		name=$(tr '\0' '\n' < /proc/device-tree/compatible 2>/dev/null | head -n1)
	}
	[ -n "$name" ] || name=$(cat /tmp/sysinfo/board_name 2>/dev/null)

	echo "${name:-generic}"
}

# True on this board, under either spelling.
#   if rx2lpro_is_board; then ... ; fi
rx2lpro_is_board() {
	case "$(rx2lpro_board_name)" in
	tenda,rx2l-pro|rx2l-pro) return 0 ;;
	esac
	return 1
}

# Read a per-unit ASCII key out of the vendor `CFG` apmib text store
# (mtd2, partition label "CFG" per dts/RX2LPRO.dts partition@30000).
# Values here are keyed by NAME, never by byte offset: the offsets
# (0x22d / 0x5a5 / 0x5c7 on the unit we dumped) drift whenever the vendor
# rewrites the config, the key names do not.
#   rx2lpro_cfg_get lan_mac        -> $TENDA.lan_mac value
rx2lpro_cfg_get() {
	local key="TENDA.$1" part val

	part=$(find_mtd_part CFG)
	[ -n "$part" ] && [ -r "$part" ] || return 1

	# Prefer master's own helper when it is present (base B uses exactly
	# this: mtd_get_mac_ascii factory TENDA.lan_mac). Fall back to strings
	# so the helper still works from an old initramfs during bring-up.
	if type mtd_get_mac_ascii >/dev/null 2>&1; then
		val=$(mtd_get_mac_ascii CFG "$key" 2>/dev/null)
		[ -n "$val" ] && { echo "$val"; return 0; }
	fi

	val=$(strings -n 8 "$part" 2>/dev/null |
		grep -m1 -o "^$key=[0-9A-Fa-f:]\{17\}$" | cut -d= -f2 |
		tr 'A-F' 'a-f')
	[ -n "$val" ] || return 1
	echo "$val"
}

# Which switching model actually materialised, for the same scripts to gate
# on. Keeps 02_network's runtime decision from having to be re-derived
# everywhere else (the hwnat nft snippet needs it too: `devices = { "eth0" }`
# is only right when eth0 is the untagged CPU netdev).
#   echo dsa | swconfig | none
rx2lpro_netmodel() {
	if [ -d /sys/class/net/lan1 ] || [ -d /sys/class/net/wan ]; then
		echo dsa
	elif [ -e /sys/class/switch/switch0 ]; then
		echo swconfig
	else
		echo none
	fi
}
