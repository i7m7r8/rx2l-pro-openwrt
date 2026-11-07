# SPDX-License-Identifier: GPL-2.0-only
#
# kmod-rtl8192cd — Realtek RTL8197F integrated 2.4 GHz WMAC, Linux 6.18.
#
# PORT (path: files/package/kernel/rtl8192cd/Makefile)
#
# Driver base: the GPL RTL8192CD release published by iptime for the A2004MU
# (same SoC: RTL8197F integrated 2.4 GHz), which that project rebuilt from
# Cudy's official `GPL_for_GP3000` drop for Linux 6.18.  Provenance and the
# per-file licence audit are in port/05-wifi-2g/route-decision.md; the firmware
# blob is the archive's rtl8197Ffw.bin, byte-verified by SHA-256 there.
#
# Packaging model — deliberately DIFFERENT from the 4.14 line this replaces:
#   * OUT OF TREE package, not `files-4.14/drivers/net/wireless/rtl8192cd/`
#     spliced into the kernel.  The 4.14 copy is 1117 files / 792k lines and
#     carries two hard 6.x breakages (self-defined PDE_DATA, raw
#     file_operations handed to proc_create_data) plus the const dev_addr
#     problem; keeping it in-tree means every kernel bump re-breaks the target.
#   * cfg80211/nl80211 control plane (`-DCONFIG_CFG80211_MODULE`), so the radio
#     registers a real wiphy.  `wifi detect`, `iw`, `iwinfo`, LuCI and hostapd
#     all work with no vendor-specific netifd handler and no WEXT.  The 4.14
#     line needed /lib/netifd/wireless/rtl8192cd.sh + a seeding uci-default
#     precisely because it had no wiphy.
#
# Build-flags below mirror the two published 6.x RTL8197F HAL builds where
# they agree, and say where they do not:
#   CONFIG_WLAN_HAL_8197F      both (iptime 6.18, toby 6.6).  rx23302 uses
#                              CONFIG_WLAN_HAL_8197G — WRONG chip for us.
#   CONFIG_BAND_2G_ON_WLAN0    both.
#   CONFIG_SOC_RFE_TYPE_*      iptime: TYPE_1 + EXT_PA + EXT_LNA (A2004MU has
#                              an external front end).  THIS BOARD: our
#                              factory apmib ships HW_RFE_TYPE_WLAN0=0 and the
#                              6.18 in-tree sibling (rx23302) also uses
#                              RFE_TYPE_0, so the default here is TYPE_0 and
#                              EXT_PA/EXT_LNA are OFF.  See calibration.md §5 —
#                              this is the one RF knob we could not settle from
#                              dumps, and it is overridable per-build.
#
# Source: PKG_SOURCE_NAME is a repacked tarball of the GPL tree plus the
# 6.18 porting delta, produced by `tools/vendor-rtl8192cd.sh` (not in this
# directory: the vendored sources go into the repo's distfiles cache, and the
# SHA-256 below is what CI verifies).  Do not point PKG_SOURCE_URL at a
# third-party mirror.

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=rtl8192cd
PKG_VERSION:=3.8.0
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-rx2lpro-1.tar.xz
PKG_SOURCE_URL:=LOCAL_MIRROR
PKG_HASH:=REPLACE_WITH_SHA256_OF_REPACKED_TARBALL

PKG_LICENSE:=GPL-2.0-only ISC RSA-MD SSLeay-standalone LicenseRef-Brian-Gladman-1999
PKG_LICENSE_FILES:=COPYING 8192cd_cfg80211.c 1x_md5c.c 1x_kmsm_aes.c 1x_rc4.c
PKG_MAINTAINER:=Tenda RX2L Pro port <none>

PKG_BUILD_PARALLEL:=1
PKG_FLAGS:=nonshared

include $(INCLUDE_DIR)/package.mk

# --- knobs the image recipe / seed.config may override -----------------------
# 0 = on-SoC PA/LNA only, 1 = external PA + external LNA.
RX2LPRO_RFE_TYPE?=0
ifeq ($(RX2LPRO_RFE_TYPE),1)
  RX2LPRO_RFE_VARS:=CONFIG_SOC_RFE_TYPE_1=y CONFIG_SOC_EXT_PA=y CONFIG_SOC_EXT_LNA=y
else
  RX2LPRO_RFE_VARS:=CONFIG_SOC_RFE_TYPE_0=y
endif

define KernelPackage/rtl8192cd
  SUBMENU:=Wireless Drivers
  TITLE:=Realtek RTL8197F integrated 2.4 GHz WMAC (vendor HAL, cfg80211)
  DEPENDS:=@TARGET_realtek_rtl8197f +kmod-cfg80211 +rtlmib
  KCONFIG:= \
	CONFIG_CFG80211=y \
	CONFIG_MAC80211=n \
	CONFIG_WIRELESS_EXT=y \
	CONFIG_WEXT_PRIV=y
  FILES:=$(PKG_BUILD_DIR)/rtl8192cd.ko
  AUTOLOAD:=$(call AutoProbe,rtl8192cd)
  $(call AddDepends/wireless)
endef

define KernelPackage/rtl8192cd/description
  Realtek's GPL RTL8192CD HAL (rtl8192fe 3.8.0 family) for the RTL8197F's
  integrated 2x2 2.4 GHz MAC/PHY, ported to Linux 6.18.  Mainline has no
  driver for this block: it is not a PCIe function, it is a memory-mapped
  WMAC at 0x18640000 (uncached 0xb8640000) on CPU IRQ 6, with the PHY tables
  compiled in rather than requested from /lib/firmware.

  The 5 GHz RTL8832BR is NOT this driver's business.
endef

STAMP_CONFIGURED_DEPENDS := \
	$(STAGING_DIR)/usr/include/mac80211-backport/backport/autoconf.h

# Same trick the upstream mac80211 backports use: compile against the
# backport headers so the driver sees a stable cfg80211 API surface.
NOSTDINC_FLAGS := \
	$(KERNEL_NOSTDINC_FLAGS) \
	-I$(STAGING_DIR)/usr/include/mac80211-backport/uapi \
	-I$(STAGING_DIR)/usr/include/mac80211-backport \
	-I$(STAGING_DIR)/usr/include/mac80211/uapi \
	-I$(STAGING_DIR)/usr/include/mac80211 \
	-include backport/backport.h

# Vendor code is not warning-clean and never will be; the 6.x trees we mined
# all end with -Wno-error.  Real errors still stop the build.
EXTRA_KCONFIG:= \
	CONFIG_RTL8192CD=m \
	CONFIG_RTL_8197F=y \
	CONFIG_SOC_WIFI=y \
	CONFIG_PCI_HCI=y \
	CONFIG_WLAN_HAL=y \
	CONFIG_WLAN_HAL_88XX=y \
	CONFIG_WLAN_HAL_8197F=y \
	CONFIG_RTL_ODM_WLAN_DRIVER=y \
	CONFIG_BAND_2G_ON_WLAN0=y \
	CONFIG_OPENWRT_SDK=y \
	CONFIG_RTL_COMAPI_WLTOOLS=y \
	$(RX2LPRO_RFE_VARS)

EXTRA_CFLAGS:= \
	-Wno-error \
	-DCONFIG_RTL_DISABLE_WLAN_MIPS16 \
	-DP2PS_SUPPORT \

# CI GATE (see REPORT.md): on >= 5.17 PDE_DATA() is pde_data().  The upstream
# 6.18 build this package is derived from resolves it through the backport
# headers; if the build log shows an implicit declaration of `PDE_DATA', add
# `-DPDE_DATA\(inode\)=pde_data\(inode\)' here instead of patching the driver.

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) \
		M="$(PKG_BUILD_DIR)" \
		NOSTDINC_FLAGS="$(NOSTDINC_FLAGS)" \
		RTL8197F_BP_BACKPORT_DIR="$(STAGING_DIR)/usr/include/mac80211-backport" \
		RTL8197F_BP_KERNEL_DIR="$(STAGING_DIR)/usr/include/mac80211" \
		EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
		$(EXTRA_KCONFIG) \
		modules
endef

$(eval $(call KernelPackage,rtl8192cd))
