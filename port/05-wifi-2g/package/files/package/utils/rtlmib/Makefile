# SPDX-License-Identifier: GPL-2.0-only
#
# rtlmib — `iwpriv <if> set_mib/get_mib' replacement for the vendor
# rtl8192cd HAL.
#
# TAKEN FROM: openwrt-rx23302 (base A), package/utils/rtlmib, GPL-2.0-only,
# which is itself a from-scratch 62-line program.  Only the header comment,
# the DEPENDS line and this note are changed for our tree.
#
# WHY WE NEED IT AT ALL, even though our adopted driver is the cfg80211 build:
#   1. `iwpriv' is gone — wireless-tools is not in current OpenWrt's packages
#      feed, and stock's entire calibration path (/bin/rfhw_convert ->
#      /tmp/rfhw.sh -> /bin/iwpriv wlanX set_mib ...) is unusable as written.
#   2. The MIB interface the calibration values target is NOT nl80211.  xcap,
#      thermal1, pwrlevelCCK_A and friends are entries in the driver's own
#      rtl8192cd_mib_addr[] table (8192cd_ioctl.c), reached through
#      SIOCDEVPRIVATE, and cfg80211 has no attribute for any of them.
#   3. The driver keeps .ndo_do_ioctl = rtl8192cd_ioctl in the cfg80211 build
#      too, so this stays the only way in.
#
# PATH: files/package/utils/rtlmib/  (build.sh overlays files/ onto the base
# tree, so this lands at package/utils/rtlmib/).

include $(TOPDIR)/rules.mk

PKG_NAME:=rtlmib
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/rtlmib
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=set_mib/get_mib for the Realtek rtl8192cd Wi-Fi driver
  DEPENDS:=@TARGET_realtek_rtl8197f
endef

define Package/rtlmib/description
  Small replacement for "iwpriv <if> set_mib/get_mib", used by rfhw-convert
  and by any RTL8192CD netifd handler on RTL8197F boards. Talks SIOCDEVPRIVATE
  directly, which is what the vendor driver's own ioctl dispatcher serves.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -Wall \
		-o $(PKG_BUILD_DIR)/rtlmib $(PKG_BUILD_DIR)/rtlmib.c
endef

define Package/rtlmib/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/rtlmib $(1)/usr/sbin/
endef

$(eval $(call BuildPackage,rtlmib))
