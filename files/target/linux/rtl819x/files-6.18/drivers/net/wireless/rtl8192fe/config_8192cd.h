/*
 * config_8192cd.h -- vendor feature macros for the rtl8192fe HAL.
 *
 * This copy differs from the platform's in exactly one line:
 * CONFIG_RTL_COMAPI_CFGFILE.
 *
 * WHY. The netifd handler this tree ships (base-files/lib/netifd/wireless/
 * rtl8192cd.sh) configures the radio by writing /etc/Wireless/RTL8192CD.dat
 * and then bringing the interface up. That only works if the driver reads the
 * file: both call sites of CfgFileProc() are inside
 * "#ifdef CONFIG_RTL_COMAPI_CFGFILE" (8192cd_osdep.c:10011 inside
 * rtl8192cd_open(), and the SIOCCOMAPIFILE ioctl in 8192cd_ioctl.c:19667).
 * The platform leaves the macro undefined, so on this base the generated file
 * was silently ignored and the radio would have come up on driver defaults --
 * no SSID, i.e. the 2.4 GHz AP that is the only reachable path on a first boot
 * with an unconfigured switch.
 *
 * If the platform later enables it (or switches to rtlmib), delete this file.
 */
#define CONFIG_RTL8192CD 1
#define CONFIG_WIFI_HCI 1
#define CONFIG_PCI_HCI 1
#define CONFIG_WLAN_HAL 1
#define CONFIG_WLAN_HAL_88XX 1
#define CONFIG_RTL_ODM_WLAN_DRIVER 1
#define CONFIG_RTL_DISABLE_WLAN_MIPS16 1
#define CONFIG_SOC_WIFI 1
#define CONFIG_WLAN_HAL_8197G 1
#define CONFIG_SOC_RFE_TYPE_0 1
#define CONFIG_BAND_2G_ON_WLAN0 1
#define CONFIG_2G_ON_WLAN0 1
#define CONFIG_RTL_VAP_SUPPORT 1
#define CONFIG_RTL_CLIENT_MODE_SUPPORT 1
#define CONFIG_RTL_REPEATER_MODE_SUPPORT 1
#define CONFIG_RTL_WPS2_SUPPORT 1
#define CONFIG_RTL_80211D_SUPPORT 1
#define CONFIG_RTL_HW_ENC_GROUP_CIPHER 1
#define CONFIG_TXPWR_LMT 1
#define CONFIG_RTL_8197F_VG 1
#define CONFIG_RTL_COMAPI_CFGFILE 1
