/*
 * Realtek RTL8832BR 5GHz WiFi Driver
 * Header file for RTL8832BR driver
 *
 * Based on vendor kernel 4.4.176 symbols extracted from vmlinux
 */

#ifndef __RTL8832BR_H__
#define __RTL8832BR_H__

#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>

/* Vendor/Product IDs */
#define RTL8832BR_VENDOR_ID		0x10EC
#define RTL8832BR_DEVICE_ID		0xb832

/* Register Offsets (from vendor halrf_config_8832br) */
#define RTL8832BR_MAC_REG_BASE		0x0000
#define RTL8832BR_RF_REG_BASE		0x1000
#define RTL8832BR_BB_REG_BASE		0x2000
#define RTL8832BR_EFUSE_BASE		0x3000

/* MAC Registers */
#define RTL8832BR_CR			0x0000
#define RTL8832BR_MACID			0x0010
#define RTL8832BR_BSSID			0x0018
#define RTL8832BR_BCN_PSR_RPT		0x0040
#define RTL8832BR_TIMER0		0x0050
#define RTL8832BR_TIMER1		0x0054
#define RTL8832BR_TSFTR			0x0058
#define RTL8832BR_PSTIMER		0x005C

/* RF Registers (from halrf_config_8832br) */
#define RTL8832BR_RF_PATH_A		0
#define RTL8832BR_RF_PATH_B		1
#define RTL8832BR_RF_NUM		2

/* BB Registers */
#define RTL8832BR_BB_NUM		2

/* EFUSE Offsets (from halrf_tssi_get_efuse_8832br) */
#define RTL8832BR_EFUSE_TSSI_SLOPE	0x00
#define RTL8832BR_EFUSE_THERMAL		0x10
#define RTL8832BR_EFUSE_XTAL		0x20
#define RTL8832BR_EFUSE_TX_PWR		0x30
#define RTL8832BR_EFUSE_IQK		0x40

/* Firmware */
#define RTL8832BR_FW_NAME		"rtlwifi/rtl8832br_fw.bin"
#define RTL8832BR_FW_MAX_SIZE		(256 * 1024)

/* Channel/Bandwidth */
#define RTL8832BR_MAX_CHANNEL		165
#define RTL8832BR_MAX_BW		80

/* RF Paths */
#define RF_PATH_MAX			2

/* Max TX Power */
#define RTL8832BR_MAX_TX_POWER		30

/* Debug */
#define RTL8832BR_DEBUG_REG_NUM		64

/* Driver private data */
struct rtl8832br_priv {
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
	
	/* Calibration data */
	struct rtl8832br_iqk_data iqk_data;
	struct rtl8832br_tssi_data tssi_data;
	struct rtl8832br_txgapk_data txgapk_data;
	struct rtl8832br_psd_data psd_data;
	struct rtl8832br_power_data power_data;
	
	/* Firmware */
	const struct firmware *fw;
	u8 *fw_data;
	size_t fw_size;
	
	/* EFUSE data */
	u8 efuse_data[512];
	bool efuse_valid;
	
	/* Power management */
	bool runtime_suspended;
	
	/* Workqueue for calibration */
	struct work_struct calib_work;
	struct delayed_work periodic_calib;
	
	/* Firmware loading */
	struct completion fw_load_done;
	bool fw_loaded;
	
	/* Statistics */
	u64 tx_bytes;
	u64 rx_bytes;
	u64 tx_packets;
	u64 rx_packets;
	
	/* Debug */
	struct dentry *debugfs_dir;
};

/* IQK Data (from iqk_init_8832br, iqk_preset_8832br) */
struct rtl8832br_iqk_data {
	bool iqk_done;
	u8 iqk_step;
	s32 iqk_xym[4][4];
	s32 iqk_rxk[4];
	s32 iqk_txk[4];
	u8 iqk_preset;
	u8 iqk_sram;
	bool iqk_afebb_restore;
};

/* TSSI Data (from halrf_tssi_init_8832br) */
struct rtl8832br_tssi_data {
	bool tssi_done;
	u8 tssi_step;
	s32 tssi_slope[RF_PATH_MAX];
	s32 tssi_trim[RF_PATH_MAX];
	s32 tssi_efuse_slope[RF_PATH_MAX];
	s32 tssi_efuse_trim[RF_PATH_MAX];
	s32 tssi_cw[RF_PATH_MAX];
	s32 tssi_de[RF_PATH_MAX];
	s32 tssi_slope_cal[RF_PATH_MAX];
	s32 tssi_tmeter[RF_PATH_MAX];
	bool tssi_enable;
};

/* TXGAPK Data (from halrf_txgapk_init_8832br) */
struct rtl8832br_txgapk_data {
	bool txgapk_done;
	u8 txgapk_step;
	s32 txgapk_power_table[RF_PATH_MAX][32];
	s32 txgapk_track_table[RF_PATH_MAX][32];
	s32 txgapk_nctl[RF_PATH_MAX][32];
};

/* PSD Data (from halrf_psd_init_8832br) */
struct rtl8832br_psd_data {
	bool psd_done;
	u8 psd_step;
	s32 psd_dac_off[RF_PATH_MAX];
	s32 psd_adc_off[RF_PATH_MAX];
};

/* Power Data (from _halrf_set_power_8832br) */
struct rtl8832br_power_data {
	s32 current_power[RF_PATH_MAX];
	s32 target_power[RF_PATH_MAX];
	s32 power_limit[RF_PATH_MAX];
	s32 power_limit_ru[RF_PATH_MAX][4]; /* 20M, 40M, 80M, 160M */
	s32 fix_power[RF_PATH_MAX];
	s32 power_by_rate[RF_PATH_MAX][32];
};

/* Function prototypes */
int rtl8832br_probe(struct pci_dev *pdev, const struct pci_device_id *id);
void rtl8832br_remove(struct pci_dev *pdev);

/* Core functions */
int rtl8832br_init_hw(struct rtl8832br_priv *priv);
int rtl8832br_deinit_hw(struct rtl8832br_priv *priv);
int rtl8832br_load_firmware(struct rtl8832br_priv *priv);
int rtl8832br_config_mac(struct rtl8832br_priv *priv);
int rtl8832br_config_bb(struct rtl8832br_priv *priv);
int rtl8832br_config_rf(struct rtl8832br_priv *priv);

/* Calibration functions (from vendor symbols) */
int rtl8832br_iqk_init(struct rtl8832br_priv *priv);
int rtl8832br_tssi_init(struct rtl8832br_priv *priv);
int rtl8832br_txgapk_init(struct rtl8832br_priv *priv);
int rtl8832br_psd_init(struct rtl8832br_priv *priv);
int rtl8832br_power_init(struct rtl8832br_priv *priv);

/* Register access */
u32 rtl8832br_read32(struct rtl8832br_priv *priv, u32 addr);
void rtl8832br_write32(struct rtl8832br_priv *priv, u32 addr, u32 val);
u8 rtl8832br_read8(struct rtl8832br_priv *priv, u32 addr);
void rtl8832br_write8(struct rtl8832br_priv *priv, u32 addr, u8 val);

/* RF register access (from halrf_config_8832br_radio_a_reg/b_reg) */
int rtl8832br_rf_write(struct rtl8832br_priv *priv, u8 path, u32 addr, u32 val);
u32 rtl8832br_rf_read(struct rtl8832br_priv *priv, u8 path, u32 addr);

/* BB register access */
int rtl8832br_bb_write(struct rtl8832br_priv *priv, u8 path, u32 addr, u32 val);
u32 rtl8832br_bb_read(struct rtl8832br_priv *priv, u8 path, u32 addr);

/* EFUSE access (from halrf_tssi_get_efuse_8832br) */
int rtl8832br_efuse_read(struct rtl8832br_priv *priv, u32 offset, u8 *data, u32 len);
int rtl8832br_efuse_write(struct rtl8832br_priv *priv, u32 offset, u8 *data, u32 len);

/* Calibration functions (mapped from vendor symbols) */
int rtl8832br_iqk_init(struct rtl8832br_priv *priv);
int rtl8832br_iqk_preset(struct rtl8832br_priv *priv);
int rtl8832br_iqk_sram(struct rtl8832br_priv *priv);

int rtl8832br_tssi_init(struct rtl8832br_priv *priv);
int rtl8832br_tssi_slope_cal(struct rtl8832br_priv *priv);
int rtl8832br_tssi_do_cal(struct rtl8832br_priv *priv);

int rtl8832br_txgapk_init(struct rtl8832br_priv *priv);
int rtl8832br_txgapk_do_txgapk(struct rtl8832br_priv *priv);

int rtl8832br_psd_init(struct rtl8832br_priv *priv);
int rtl8832br_psd_query(struct rtl8832br_priv *priv);

int rtl8832br_power_init(struct rtl8832br_priv *priv);
int rtl8832br_set_tx_power(struct rtl8832br_priv *priv, int power);
int rtl8832br_set_tx_shape(struct rtl8832br_priv *priv, u8 shape);

#endif /* __RTL8832BR_H__ */
