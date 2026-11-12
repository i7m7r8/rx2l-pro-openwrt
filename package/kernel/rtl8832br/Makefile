#
# Copyright (C) 2024 RTL8832BR Backport Team
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=rtl8832br
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/RTL8832BR/rtl8832br.git
PKG_SOURCE_VERSION:=v$(PKG_VERSION)
PKG_MIRROR_HASH:=skip

PKG_LICENSE:=GPL-2.0
PKG_LICENSE_FILES:=LICENSE

PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/package.mk

define KernelPackage/rtl8832br
  SUBMENU:=Wireless Drivers
  TITLE:=Realtek RTL8832BR 5GHz WiFi Driver
  DEPENDS:=+kmod-mac80211 +kmod-cfg80211 +kmod-crc32 +kmod-firmware-loader @PCI_SUPPORT
  FILES:=$(PKG_BUILD_DIR)/rtl8832br.ko
  AUTOLOAD:=$(call AutoProbe,rtl8832br)
  PROVIDES:=kmod-rtl8832br
endef

define KernelPackage/rtl8832br/description
  Realtek RTL8832BR 5GHz WiFi driver for RTL8197F based routers
  (e.g. Tenda RX2L Pro). Supports PCIe interface, firmware loading,
  RF calibration (IQK, TSSI, TXGAPK, PSD), and power management.
endef

define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C "$(LINUX_DIR)" \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		ARCH="$(LINUX_KARCH)" \
		SUBDIRS="$(PKG_BUILD_DIR)" \
		modules
endef

$(eval $(call KernelPackage,rtl8832br))
