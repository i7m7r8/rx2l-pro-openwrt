/*
 * Realtek RTL8852B PHL (PHY Layer) WiFi Driver
 * Header file for RTL8852B/PHL driver
 *
 * Based on vendor kernel 4.4.176 symbols (PHL framework, rtw_phl)
 */

#ifndef __RTL8852B_H__
#define __RTL8852B_H__

#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>

/* Vendor/Product IDs */
#define RTL8852B_VENDOR_ID		0x10EC
#define RTL8852B_DEVICE_ID		0x8852

/* PHL Framework Constants */
#define PHL_MAX_ADAPTERS		4
#define PHL_MAX_MAC_ADDR_NUM	32
#define PHL_MAX_STA_NUM		256

/* Register Offsets */
#define RTL8852B_MAC_REG_BASE		0x0000
#define RTL8852B_RF_REG_BASE		0x1000
#define RTL8852B_BB_REG_BASE		0x2000

/* MAC Registers */
#define RTL8852B_CR			0x0000
#define RTL8852B_MACID			0x0010
#define RTL8852B_BSSID			0x0018

/* Firmware */
#define RTL8852B_FW_NAME		"rtlwifi/rtl8852bfw.bin"
#define RTL8852B_FW_WOW_NAME		"rtlwifi/rtl8852bfw_wow.bin"
#define RTL8852B_FW_MAX_SIZE		(512 * 1024)

/* Channel/Bandwidth */
#define RTL8852B_MAX_CHANNEL_2G		14
#define RTL8852B_MAX_CHANNEL_5G		165
#define RTL8852B_MAX_BW		80

/* RF Paths */
#define RF_PATH_MAX			2

/* Max TX Power */
#define RTL8852B_MAX_TX_POWER		30

/* Driver private data */
struct rtl8852b_priv {
	struct pci_dev *pdev;
	struct ieee80211_hw *hw;
	struct mutex mutex;
	struct mutex rf_mutex;
	
	/* Memory mapped I/O */
	void __iomem *mmio;
	size_t mmio_len;
	
	/* PHL/HALMAC state */
	void *phl_com;
	void *halmac;
	u8 current_channel;
	u8 current_bw;
	u8 current_band;
	
	/* Firmware */
	const struct firmware *fw;
	const struct firmware *fw_wow;
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
int rtl8852b_probe(struct pci_dev *pdev, const struct pci_device_id *id);
void rtl8852b_remove(struct pci_dev *pdev);

/* Core functions */
int rtl8852b_init_hw(struct rtl8852b_priv *priv);
int rtl8852b_deinit_hw(struct rtl8852b_priv *priv);
int rtl8852b_load_firmware(struct rtl8852b_priv *priv);
int rtl8852b_config_mac(struct rtl8852b_priv *priv);
int rtl8852b_config_bb(struct rtl8852b_priv *priv);
int rtl8852b_config_rf(struct rtl8852b_priv *priv);

/* PHL framework functions (from phl_* symbols) */
int rtl8852b_phl_init(struct rtl8852b_priv *priv);
int rtl8852b_phl_deinit(struct rtl8852b_priv *priv);
int rtl8852b_phl_fw_init(struct rtl8852b_priv *priv);
int rtl8852b_phl_trx_init_pcie(struct rtl8852b_priv *priv);
int rtl8852b_phl_trx_deinit_pcie(struct rtl8852b_priv *priv);

/* HALMAC functions */
int rtl8852b_halmac_init(struct rtl8852b_priv *priv);
int rtl8852b_halmac_deinit(struct rtl8852b_priv *priv);

/* Register access */
u32 rtl8852b_read32(struct rtl8852b_priv *priv, u32 addr);
void rtl8852b_write32(struct rtl8852b_priv *priv, u32 addr, u32 val);
u8 rtl8852b_read8(struct rtl8852b_priv *priv, u32 addr);
void rtl8852b_write8(struct rtl8852b_priv *priv, u32 addr, u8 val);

/* RF register access */
int rtl8852b_rf_write(struct rtl8852b_priv *priv, u8 path, u32 addr, u32 val);
u32 rtl8852b_rf_read(struct rtl8852b_priv *priv, u8 path, u32 addr);

/* BB register access */
int rtl8852b_bb_write(struct rtl8852b_priv *priv, u8 path, u32 addr, u32 val);
u32 rtl8852b_bb_read(struct rtl8852b_priv *priv, u8 path, u32 addr);

/* Workqueue callbacks */
void rtl8852b_calib_work(struct work_struct *work);
void rtl8852b_periodic_calib(struct work_struct *work);

#endif /* __RTL8852B_H__ */
