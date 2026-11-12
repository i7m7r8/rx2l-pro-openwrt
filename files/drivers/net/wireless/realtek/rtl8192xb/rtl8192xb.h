/*
 * Realtek RTL8192XB 2.4GHz WiFi Driver
 * Header file for RTL8192XB driver
 *
 * Based on vendor kernel 4.4.176 symbols (rtl8192cd, rtl8192xb)
 */

#ifndef __RTL8192XB_H__
#define __RTL8192XB_H__

#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>

/* Vendor/Product IDs */
#define RTL8192XB_VENDOR_ID		0x10EC
#define RTL8192XB_DEVICE_ID		0x8192

/* Register Offsets */
#define RTL8192XB_MAC_REG_BASE		0x0000
#define RTL8192XB_RF_REG_BASE		0x1000
#define RTL8192XB_BB_REG_BASE		0x2000

/* MAC Registers */
#define RTL8192XB_CR			0x0000
#define RTL8192XB_MACID			0x0010
#define RTL8192XB_BSSID			0x0018

/* Firmware */
#define RTL8192XB_FW_NAME		"rtlwifi/rtl8192xbfw.bin"
#define RTL8192XB_FW_MAX_SIZE		(256 * 1024)

/* Channel/Bandwidth */
#define RTL8192XB_MAX_CHANNEL		14
#define RTL8192XB_MAX_BW		40

/* RF Paths */
#define RF_PATH_MAX			2

/* Max TX Power */
#define RTL8192XB_MAX_TX_POWER		30

/* Driver private data */
struct rtl8192xb_priv {
	struct pci_dev *pdev;
	struct ieee80211_hw *hw;
	struct mutex mutex;
	struct mutex rf_mutex;
	
	/* Memory mapped I/O */
	void __iomem *mmio;
	size_t mmio_len;
	
	/* RF/BB state */
	u8 rf_path;
	u8 bb_path;
	u8 current_channel;
	u8 current_bw;
	u8 current_band;
	
	/* Firmware */
	const struct firmware *fw;
	u8 *fw_data;
	size_t fw_size;
	
	/* Statistics */
	u64 tx_bytes;
	u64 rx_bytes;
	u64 tx_packets;
	u64 rx_packets;
	
	/* Debug */
	struct dentry *debugfs_dir;
};

/* Function prototypes */
int rtl8192xb_probe(struct pci_dev *pdev, const struct pci_device_id *id);
void rtl8192xb_remove(struct pci_dev *pdev);

/* Core functions */
int rtl8192xb_init_hw(struct rtl8192xb_priv *priv);
int rtl8192xb_deinit_hw(struct rtl8192xb_priv *priv);
int rtl8192xb_load_firmware(struct rtl8192xb_priv *priv);
int rtl8192xb_config_mac(struct rtl8192xb_priv *priv);
int rtl8192xb_config_bb(struct rtl8192xb_priv *priv);
int rtl8192xb_config_rf(struct rtl8192xb_priv *priv);

/* Register access */
u32 rtl8192xb_read32(struct rtl8192xb_priv *priv, u32 addr);
void rtl8192xb_write32(struct rtl8192xb_priv *priv, u32 addr, u32 val);
u8 rtl8192xb_read8(struct rtl8192xb_priv *priv, u32 addr);
void rtl8192xb_write8(struct rtl8192xb_priv *priv, u32 addr, u8 val);

/* RF register access */
int rtl8192xb_rf_write(struct rtl8192xb_priv *priv, u8 path, u32 addr, u32 val);
u32 rtl8192xb_rf_read(struct rtl8192xb_priv *priv, u8 path, u32 addr);

/* BB register access */
int rtl8192xb_bb_write(struct rtl8192xb_priv *priv, u8 path, u32 addr, u32 val);
u32 rtl8192xb_bb_read(struct rtl8192xb_priv *priv, u8 path, u32 addr);

/* Calibration function stubs */
int rtl8192xb_calib_init(struct rtl8192xb_priv *priv);

/* Workqueue callbacks */
void rtl8192xb_calib_work(struct work_struct *work);
void rtl8192xb_periodic_calib(struct work_struct *work);

#endif /* __RTL8192XB_H__ */
