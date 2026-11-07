# Staged package skeleton -- route (a): Realtek G6 "rtk_wifi6" SKD on kernel 6.18.
#
# NOT WIRED IN. This lives under port/06-wifi-5g-8832br/skd-attempt/ as a
# proposal; the orchestrator decides when it enters the tree as
# package/kernel/rtl8832br-rx2lpro/. Nothing here is committed by an agent.
#
# Derived from a build that is known to compile: /tmp/rebase-base
#   package/kernel/rtl8832br-wr1500/Makefile (revision 4, Linux 6.6.114 +
#   backports 6.12.96, mipsel, modpost clean). Read that file before changing
#   this one -- it also documents why autoload was switched OFF (IRQ allocation
#   then timer softirq on real hardware) and why RF files are hash-gated.
#
# Differences vs the toby package, all deliberate:
#   * board name / PCI match widened from "cudy,ax1500-r76" to our SoC generic,
#     because our DTS is new; keep 10ec:b832 as the hard device filter.
#   * RF calibration comes from the stock image at Build/Prepare time and is
#     verified against files/rfe50-manifest.sha256 (our RadioA/B + ByRate
#     differ from the WR1500 ones -- see stock-evidence.md section 5).
#   * no auto-load: `AUTOLOAD` left empty on purpose. A driver that panics in
#     a timer softirq must not be able to brick first boot; risk row 8 of the
#     README depends on 5 GHz being absent, not broken.
#   * kernel 6.18: patches/00xx series has to grow; see skd-attempt/README.md
#     section 3 for what is measured vs predicted.

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=rtl8832br-rx2lpro
PKG_VERSION:=2026.10.22
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only

# Vendor GPL drop containing the G6 Wi-Fi 6 driver subtree. This is the
# archive the 6.6 port is proven against; source URL may move -- keep the hash.
PKG_SOURCE:=GPL_ax1500v3.tar.gz
PKG_SOURCE_URL:=https://static.tp-link.com/upload/gpl-code/2026/202610/20261022/
PKG_HASH:=6d133598b134b6878ce802a53b3061d9f2efb3636ad0d31ec4cb3bcbdf438f2a
PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_PARALLEL:=1
PKG_BUILD_DEPENDS:=mac80211

include $(INCLUDE_DIR)/package.mk

# G6 source subtree inside the archive (strip-components=8 in Build/Prepare).
G6_SUBTREE:=GPL_ax1500v3/rtl8198/linux-4.4.x/drivers/net/wireless/realtek/g6_wifi_driver

# Stock firmware image the RF tables are cut out of. The image agent owns this
# input; we only consume it. Empty => package prints how to supply it and skips.
RX2L_STOCK_IMAGE ?=

RFE50_DIR := $(PKG_BUILD_DIR)/rx2lpro-RFE50
MANIFEST  := ./files/rfe50-manifest.sha256

define KernelPackage/rtl8832br-rx2lpro
  SUBMENU:=Wireless Drivers
  TITLE:=RTL8832BR 5GHz Wi-Fi 6 driver (Realtek G6 SKD, RX2L Pro)
  DEPENDS:=@TARGET_realtek_rtl8197f +kmod-cfg80211 +@DRIVER_11AC_SUPPORT +@DRIVER_11AX_SUPPORT +iw +wireless-regdb
  KCONFIG:=CONFIG_WIRELESS=y
  FILES:=$(PKG_BUILD_DIR)/rtk_wifi6.ko
  # No (AUTOLOAD:=...) line: manual probe only, see header comment.
  MENU:=1
endef

define KernelPackage/rtl8832br-rx2lpro/description
 Native Linux 6.18/OpenWrt cfg80211 port of the Realtek G6 RTL8832BR PCIe
 driver for the Tenda RX2L Pro (RTL8197F, RFE50 eFEM). Binds PCI 10ec:b832
 only. Loads the board's RF tables from /etc/conf/rtl8832bre/RFE50. Firmware
 is the array embedded in the driver tree (no /lib/firmware file exists).
 Does not drive the in-SoC 2.4 GHz radio. EXPERIMENTAL: autoload disabled
 because the 6.6 attempt failed IRQ registration and timer softirq on
 hardware; read port/06-wifi-5g-8832br/skd-attempt/README.md before enabling.
endef

# 1. Cut the G6 subtree.
# 2. Extract this board's RF tables from the stock image and verify each
#    against the manifest. Never fall back to the WR1500 copies.
define Build/Prepare
	$(RM) -r $(PKG_BUILD_DIR)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	tar -xzf $(DL_DIR)/$(PKG_SOURCE) -C $(PKG_BUILD_DIR) --strip-components=8 \
		$(G6_SUBTREE)
	$(SED) 's/\r//g' $(PKG_BUILD_DIR)/core/rtw_mib.c $(PKG_BUILD_DIR)/core/rtw_recv.c
	$(call Prepare/RFE50)
	$(call Build/Patch)
endef

define Prepare/RFE50
	@set -eu; \
	if [ -z "$(RX2L_STOCK_IMAGE)" ] || [ ! -f "$(RX2L_STOCK_IMAGE)" ]; then \
		echo "rtl8832br-rx2lpro: RX2L_STOCK_IMAGE unset; cannot supply this board's RF tables." >&2; \
		echo "  Pass the decrypted stock KernelFS/rootfs (unblob -> squashfs) so" >&2; \
		echo "  etc_ro/wlan/rtl8832bre/RFE50 can be extracted. See stock-evidence.md." >&2; \
		exit 1; \
	fi; \
	$(INSTALL_DIR) $(RFE50_DIR)/AP_5G; \
	$(TOOL_DIR)/unblob-extractor ... $(RFE50_DIR) ; \
	count=0; \
	while IFS=: read -r expected relative; do \
		case "$$expected" in '#'*|'') continue;; esac; \
		expected=$${expected%% *}; \
		file=$(RFE50_DIR)/$${relative%% *}; \
		[ -s "$$file" ] || { echo "RFE50 ERROR: missing $$file" >&2; exit 1; }; \
		actual=$$(sha256sum "$$file"); actual=$${actual%% *}; \
		[ "$$actual" = "$$expected" ] || { echo "RFE50 ERROR: SHA mismatch $$relative (got $$actual)" >&2; exit 1; }; \
		count=$$((count+1)); \
	done < $(MANIFEST); \
	[ "$$count" -eq 16 ] || { echo "RFE50 ERROR: expected 16 verified files, got $$count" >&2; exit 1; }; \
	echo "RFE50: 16 RX2L Pro calibration files verified"
endef

# Flags below are the measured 6.6 set. The -Wno-error list is where a 6.18
# build will start failing first; keep them explicit rather than -w.
define Build/Compile
	+$(KERNEL_MAKE) M="$(PKG_BUILD_DIR)" \
		CONFIG_RTLWIFI6=m CONFIG_RTL8832BR=y CONFIG_SOC_RTL8197F=y \
		CONFIG_BR_EXT=n CONFIG_TXPWR_LIMIT=y CONFIG_TXPWR_LIMIT_EN=y \
		CONFIG_FILE_FWIMG=n CONFIG_EFUSE_CONFIG_FILE=n CONFIG_PROC_DEBUG=n \
		CONFIG_APPEND_VENDOR_IE_ENABLE=n \
		USER_EXTRA_CFLAGS="-DCONFIG_RTL8832BR -DCONFIG_WLAN0_RFE_TYPE_50 -DCONFIG_WLAN0_AP_5G -Wno-error=address -Wno-error=implicit-fallthrough -Wno-error=enum-conversion -Wno-error=misleading-indentation" \
		NOSTDINC_FLAGS="$(KERNEL_NOSTDINC_FLAGS) \
		-I$(STAGING_DIR)/usr/include/mac80211-backport \
		-I$(STAGING_DIR)/usr/include/mac80211-backport/uapi \
		-I$(STAGING_DIR)/usr/include/mac80211 \
		-I$(STAGING_DIR)/usr/include/mac80211/uapi \
		-include $(PKG_BUILD_DIR)/include/openwrt_g6.h -include backport/backport.h" \
		modules
endef

define KernelPackage/rtl8832br-rx2lpro/install
	$(INSTALL_DIR) $(1)/etc/conf/rtl8832bre/RFE50/AP_5G
	$(INSTALL_DATA) $(RFE50_DIR)/*.txt $(1)/etc/conf/rtl8832bre/RFE50/
	$(INSTALL_DATA) $(RFE50_DIR)/AP_5G/*.txt $(1)/etc/conf/rtl8832bre/RFE50/AP_5G/
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/8832-bringup-diag $(1)/usr/sbin/8832-bringup-diag
	# The driver loads RF tables from /etc/conf/rtl8832bre/RFE50 (+ AP_5G).
	# Bring-up is deliberately NOT wired into netifd/wifi: mac80211.sh's
	# `iw phy <phy> interface add` hangs this driver until a power cycle
	# (mr1500x-openwrt/files/etc/init.d/mr1500x-wifi header). A validated
	# port needs its own hostapd-driven service, reviewed separately.
endef

$(eval $(call KernelPackage,rtl8832br-rx2lpro))
