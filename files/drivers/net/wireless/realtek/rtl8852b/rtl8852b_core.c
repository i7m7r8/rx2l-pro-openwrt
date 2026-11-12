/*
 * Realtek RTL8852B PHL WiFi Driver - Core
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include "rtl8852b.h"

#define DRIVER_NAME "rtl8852b"
#define DRIVER_VERSION "0.1.0"

static const struct pci_device_id rtl8852b_pci_ids[] = {
	{ PCI_DEVICE(RTL8852B_VENDOR_ID, RTL8852B_DEVICE_ID) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, rtl8852b_pci_ids);

/* MAC80211 operations */
static const struct ieee80211_ops rtl8852b_ops = {
	.tx = rtl8852b_tx,
	.start = rtl8852b_start,
	.stop = rtl8852b_stop,
	.config = rtl8852b_config,
	.add_interface = rtl8852b_add_interface,
	.remove_interface = rtl8852b_remove_interface,
	.config_interface = rtl8852b_config_interface,
	.configure_filter = rtl8852b_configure_filter,
	.bss_info_changed = rtl8852b_bss_info_changed,
	.sta_state = rtl8852b_sta_state,
	.conf_tx = rtl8852b_conf_tx,
	.get_survey = rtl8852b_get_survey,
	.set_tim = rtl8852b_set_tim,
	.set_key = rtl8852b_set_key,
	.sw_scan_start = rtl8852b_sw_scan_start,
	.sw_scan_complete = rtl8852b_sw_scan_complete,
	.get_stats = rtl8852b_get_stats,
	.get_tkip_seq = rtl8852b_get_tkip_seq,
	.set_frag_threshold = rtl8852b_set_frag_threshold,
	.set_rts_threshold = rtl8852b_set_rts_threshold,
	.sta_add = rtl8852b_sta_add,
	.sta_remove = rtl8852b_sta_remove,
};

/* PCI probe */
static int rtl8852b_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct rtl8852b_priv *priv;
	struct ieee80211_hw *hw;
	int ret;

	pr_info("%s: probing device %04x:%04x\n", DRIVER_NAME, 
		pdev->vendor, pdev->device);

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return ret;
	}

	ret = pcim_iomap_regions(pdev, 1 << 0, DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request PCI regions\n");
		return ret;
	}

	hw = ieee80211_alloc_hw(sizeof(struct rtl8852b_priv), &rtl8852b_ops);
	if (!hw) {
		dev_err(&pdev->dev, "Failed to allocate hw\n");
		return -ENOMEM;
	}

	priv = hw->priv;
	priv->pdev = pdev;
	priv->hw = hw;
	pci_set_drvdata(pdev, priv);

	mutex_init(&priv->mutex);
	mutex_init(&priv->rf_mutex);

	priv->mmio = pcim_iomap_table(pdev)[0];
	if (!priv->mmio) {
		dev_err(&pdev->dev, "Failed to map MMIO\n");
		ret = -EIO;
		goto err_free_hw;
	}
	priv->mmio_len = pci_resource_len(pdev, 0);

	SET_IEEE80211_DEV(hw, &pdev->dev);
	hw->queues = 4;
	hw->max_listen_interval = 10;
	hw->max_rate_tries = 7;
	hw->max_rx_aggregation_subframes = 64;
	hw->max_tx_aggregation_subframes = 64;
	hw->offchannel_tx_hw_queue = 3;
	hw->max_tx_fragments = 16;
	hw->max_tx_antenna = 2;
	hw->max_rx_antenna = 2;

	hw->wiphy->bands[NL80211_BAND_2GHZ] = &rtl8852b_band_2ghz;
	hw->wiphy->bands[NL80211_BAND_5GHZ] = &rtl8852b_band_5ghz;
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				      BIT(NL80211_IFTYPE_AP) |
				      BIT(NL80211_IFTYPE_ADHOC) |
				      BIT(NL80211_IFTYPE_MESH_POINT);

	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_RX_INCLUDES_FCS |
		    IEEE80211_HW_QUEUE_CONTROL |
		    IEEE80211_HW_SUPPORTS_PS |
		    IEEE80211_HW_SUPPORTS_DYNAMIC_PS |
		    IEEE80211_HW_MFP_CAPABLE |
		    IEEE80211_HW_REPORTS_TX_ACK_STATUS;

	ret = devm_request_irq(&pdev->dev, pdev->irq,
			       rtl8852b_interrupt,
			       IRQF_SHARED, DRIVER_NAME, priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request IRQ\n");
		goto err_free_hw;
	}

	ret = rtl8852b_init_hw(priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize hardware\n");
		goto err_free_hw;
	}

	ret = rtl8852b_load_firmware(priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to load firmware\n");
		goto err_deinit_hw;
	}

	ret = ieee80211_register_hw(hw);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register hw\n");
		goto err_deinit_hw;
	}

	rtl8852b_debugfs_init(priv);

	pr_info("%s: probe successful\n", DRIVER_NAME);
	return 0;

err_deinit_hw:
	rtl8852b_deinit_hw(priv);
err_free_hw:
	ieee80211_free_hw(hw);
	return ret;
}

static void rtl8852b_remove(struct pci_dev *pdev)
{
	struct rtl8852b_priv *priv = pci_get_drvdata(pdev);
	struct ieee80211_hw *hw = priv->hw;

	pr_info("%s: removing device\n", DRIVER_NAME);

	ieee80211_unregister_hw(hw);
	rtl8852b_deinit_hw(priv);

	if (priv->fw)
		release_firmware(priv->fw);
	if (priv->fw_wow)
		release_firmware(priv->fw_wow);

	ieee80211_free_hw(hw);
}

int rtl8852b_init_hw(struct rtl8852b_priv *priv)
{
	int ret;

	pr_info("%s: initializing hardware\n", DRIVER_NAME);

	rtl8852b_write32(priv, 0x0000, 0);
	msleep(10);

	ret = rtl8852b_config_mac(priv);
	if (ret) return ret;

	ret = rtl8852b_config_bb(priv);
	if (ret) return ret;

	ret = rtl8852b_config_rf(priv);
	if (ret) return ret;

	/* Initialize PHL framework */
	ret = rtl8852b_phl_init(priv);
	if (ret) return ret;

	/* Initialize HALMAC */
	ret = rtl8852b_halmac_init(priv);
	if (ret) return ret;

	return 0;
}

int rtl8852b_deinit_hw(struct rtl8852b_priv *priv)
{
	pr_info("%s: deinitializing hardware\n", DRIVER_NAME);

	rtl8852b_phl_deinit(priv);
	rtl8852b_halmac_deinit(priv);
	rtl8852b_write32(priv, 0x0000, 0);

	return 0;
}

int rtl8852b_load_firmware(struct rtl8852b_priv *priv)
{
	const struct firmware *fw;
	int ret;

	pr_info("%s: loading firmware\n", DRIVER_NAME);

	ret = request_firmware(&fw, RTL8852B_FW_NAME, &priv->pdev->dev);
	if (ret) {
		dev_err(&priv->pdev->dev, "Failed to load firmware %s\n",
			RTL8852B_FW_NAME);
		return ret;
	}

	priv->fw = fw;
	priv->fw_data = (u8 *)fw->data;
	priv->fw_size = fw->size;

	/* Load WOW firmware if available */
	request_firmware(&fw, RTL8852B_FW_WOW_NAME, &priv->pdev->dev);
	if (fw) {
		priv->fw_wow = fw;
	}

	pr_info("%s: firmware loaded (%zu bytes)\n", DRIVER_NAME, priv->fw_size);
	return 0;
}

int rtl8852b_config_mac(struct rtl8852b_priv *priv) { return 0; }
int rtl8852b_config_bb(struct rtl8852b_priv *priv) { return 0; }
int rtl8852b_config_rf(struct rtl8852b_priv *priv) { return 0; }

static irqreturn_t rtl8852b_interrupt(int irq, void *dev_id)
{
	struct rtl8852b_priv *priv = dev_id;
	u32 isr;

	isr = rtl8852b_read32(priv, 0x100);
	if (!isr)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

/* MAC80211 callbacks (stubs) */
static int rtl8852b_start(struct ieee80211_hw *hw) { return 0; }
static void rtl8852b_stop(struct ieee80211_hw *hw) { }
static int rtl8852b_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control, struct sk_buff *skb) { return 0; }
static int rtl8852b_config(struct ieee80211_hw *hw, u32 changed) { return 0; }
static int rtl8852b_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif) { return 0; }
static void rtl8852b_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif) { }
static int rtl8852b_config_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_interface_conf *conf) { return 0; }
static void rtl8852b_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags, unsigned int *total_flags, u64 multicast) { }
static void rtl8852b_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_bss_conf *info, u32 changed) { }
static int rtl8852b_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta, enum ieee80211_sta_state old_state, enum ieee80211_sta_state new_state) { return 0; }
static int rtl8852b_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u16 queue, const struct ieee80211_tx_queue_params *params) { return 0; }
static int rtl8852b_get_survey(struct ieee80211_hw *hw, int idx, struct survey_info *survey) { return -ENOENT; }
static void rtl8852b_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set) { }
static int rtl8852b_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd, struct ieee80211_vif *vif, struct ieee80211_sta *sta, struct ieee80211_key_conf *key) { return 0; }
static int rtl8852b_sw_scan_start(struct ieee80211_hw *hw, struct ieee80211_vif *vif) { return 0; }
static void rtl8852b_sw_scan_complete(struct ieee80211_hw *hw, struct ieee80211_vif *vif) { }
static int rtl8852b_get_stats(struct ieee80211_hw *hw, struct ieee80211_low_level_stats *stats) { return 0; }
static u64 rtl8852b_get_tkip_seq(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta, u8 key_idx) { return 0; }
static int rtl8852b_set_frag_threshold(struct ieee80211_hw *hw, u32 value) { return 0; }
static int rtl8852b_set_rts_threshold(struct ieee80211_hw *hw, u32 value) { return 0; }
static int rtl8852b_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta) { return 0; }
static int rtl8852b_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta) { return 0; }

/* 2.4GHz band definition */
static const struct ieee80211_channel rtl8852b_2ghz_channels[] = {
	CHAN2G_1(20), CHAN2G_2(20), CHAN2G_3(20), CHAN2G_4(20),
	CHAN2G_5(20), CHAN2G_6(20), CHAN2G_7(20), CHAN2G_8(20),
	CHAN2G_9(20), CHAN2G_10(20), CHAN2G_11(20), CHAN2G_12(20),
	CHAN2G_13(20), CHAN2G_14(20),
};

static const struct ieee80211_rate rtl8852b_2ghz_rates[] = {
	{ .bitrate = 10, .hw_value = 0, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 20, .hw_value = 1, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 55, .hw_value = 2, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 110, .hw_value = 3, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 60, .hw_value = 4, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 90, .hw_value = 5, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 120, .hw_value = 6, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 180, .hw_value = 7, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 240, .hw_value = 8, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 360, .hw_value = 9, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 480, .hw_value = 10, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 540, .hw_value = 11, .flags = IEEE80211_RATE_MANDATORY_A },
};

static struct ieee80211_supported_band rtl8852b_band_2ghz = {
	.channels = rtl8852b_2ghz_channels,
	.n_channels = ARRAY_SIZE(rtl8852b_2ghz_channels),
	.bitrates = rtl8852b_2ghz_rates,
	.n_bitrates = ARRAY_SIZE(rtl8852b_2ghz_rates),
	.ht_cap = {
		.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_GRN_FLD |
		       IEEE80211_HT_CAP_SGI_20 |
		       IEEE80211_HT_CAP_SGI_40 |
		       IEEE80211_HT_CAP_DSSSCCK40,
		.ht_supported = true,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
		.mcs = {
			.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
			.rx_highest = 300,
			.tx_params = IEEE80211_HT_TXPARAM_DEFAULT,
		},
	},
	.vht_cap = {
		.vht_supported = true,
		.cap = IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80 |
		       IEEE80211_VHT_CAP_SHORT_GI_80 |
		       IEEE80211_VHT_CAP_SHORT_GI_160 |
		       IEEE80211_VHT_CAP_TXSTBC |
		       IEEE80211_VHT_CAP_RXSTBC_1 |
		       IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454,
		.vht_mcs = {
			.rx_mcs_map = cpu_to_le16(0xfffa),
			.tx_mcs_map = cpu_to_le16(0xfffa),
			.rx_highest = cpu_to_le16(867),
			.tx_highest = cpu_to_le16(867),
		},
	},
};

/* 5GHz band definition */
static const struct ieee80211_channel rtl8852b_5ghz_channels[] = {
	CHAN5G_36(20), CHAN5G_40(20), CHAN5G_44(20), CHAN5G_48(20),
	CHAN5G_52(20), CHAN5G_56(20), CHAN5G_60(20), CHAN5G_64(20),
	CHAN5G_100(20), CHAN5G_104(20), CHAN5G_108(20), CHAN5G_112(20),
	CHAN5G_116(20), CHAN5G_120(20), CHAN5G_124(20), CHAN5G_128(20),
	CHAN5G_132(20), CHAN5G_136(20), CHAN5G_140(20), CHAN5G_144(20),
	CHAN5G_149(20), CHAN5G_153(20), CHAN5G_157(20), CHAN5G_161(20),
	CHAN5G_165(20),
};

static const struct ieee80211_rate rtl8852b_5ghz_rates[] = {
	{ .bitrate = 60, .hw_value = 0, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 90, .hw_value = 1, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 120, .hw_value = 2, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 180, .hw_value = 3, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 240, .hw_value = 4, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 360, .hw_value = 5, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 480, .hw_value = 6, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 540, .hw_value = 7, .flags = IEEE80211_RATE_MANDATORY_A },
};

static struct ieee80211_supported_band rtl8852b_band_5ghz = {
	.channels = rtl8852b_5ghz_channels,
	.n_channels = ARRAY_SIZE(rtl8852b_5ghz_channels),
	.bitrates = rtl8852b_5ghz_rates,
	.n_bitrates = ARRAY_SIZE(rtl8852b_5ghz_rates),
	.ht_cap = {
		.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_GRN_FLD |
		       IEEE80211_HT_CAP_SGI_20 |
		       IEEE80211_HT_CAP_SGI_40 |
		       IEEE80211_HT_CAP_DSSSCCK40,
		.ht_supported = true,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
		.mcs = {
			.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
			.rx_highest = 300,
			.tx_params = IEEE80211_HT_TXPARAM_DEFAULT,
		},
	},
	.vht_cap = {
		.vht_supported = true,
		.cap = IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80 |
		       IEEE80211_VHT_CAP_SHORT_GI_80 |
		       IEEE80211_VHT_CAP_SHORT_GI_160 |
		       IEEE80211_VHT_CAP_TXSTBC |
		       IEEE80211_VHT_CAP_RXSTBC_1 |
		       IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454,
		.vht_mcs = {
			.rx_mcs_map = cpu_to_le16(0xfffa),
			.tx_mcs_map = cpu_to_le16(0xfffa),
			.rx_highest = cpu_to_le16(867),
			.tx_highest = cpu_to_le16(867),
		},
	},
};

/* PHL framework functions (stubs) */
int rtl8852b_phl_init(struct rtl8852b_priv *priv) { return 0; }
int rtl8852b_phl_deinit(struct rtl8852b_priv *priv) { return 0; }
int rtl8852b_phl_fw_init(struct rtl8852b_priv *priv) { return 0; }
int rtl8852b_phl_trx_init_pcie(struct rtl8852b_priv *priv) { return 0; }
int rtl8852b_phl_trx_deinit_pcie(struct rtl8852b_priv *priv) { return 0; }

/* HALMAC functions (stubs) */
int rtl8852b_halmac_init(struct rtl8852b_priv *priv) { return 0; }
int rtl8852b_halmac_deinit(struct rtl8852b_priv *priv) { return 0; }

/* Register access */
u32 rtl8852b_read32(struct rtl8852b_priv *priv, u32 addr) { return ioread32(priv->mmio + addr); }
void rtl8852b_write32(struct rtl8852b_priv *priv, u32 addr, u32 val) { iowrite32(val, priv->mmio + addr); }
u8 rtl8852b_read8(struct rtl8852b_priv *priv, u32 addr) { return ioread8(priv->mmio + addr); }
void rtl8852b_write8(struct rtl8852b_priv *priv, u32 addr, u8 val) { iowrite8(val, priv->mmio + addr); }

int rtl8852b_rf_write(struct rtl8852b_priv *priv, u8 path, u32 addr, u32 val)
{
	if (path >= RF_PATH_MAX) return -EINVAL;
	u32 reg = (path << 20) | (addr << 8) | (val & 0xFF);
	rtl8852b_write32(priv, 0x1000, reg);
	udelay(1);
	return 0;
}

u32 rtl8852b_rf_read(struct rtl8852b_priv *priv, u8 path, u32 addr)
{
	if (path >= RF_PATH_MAX) return 0xFFFFFFFF;
	u32 reg = (0x1 << 31) | (path << 20) | (addr << 8);
	rtl8852b_write32(priv, 0x1000, reg);
	udelay(1);
	return rtl8852b_read32(priv, 0x1004) & 0xFF;
}

int rtl8852b_bb_write(struct rtl8852b_priv *priv, u8 path, u32 addr, u32 val)
{
	if (path >= RF_PATH_MAX) return -EINVAL;
	rtl8852b_write32(priv, 0x2000 + (path << 16) + addr, val);
	return 0;
}

u32 rtl8852b_bb_read(struct rtl8852b_priv *priv, u8 path, u32 addr)
{
	if (path >= RF_PATH_MAX) return 0xFFFFFFFF;
	return rtl8852b_read32(priv, 0x2000 + (path << 16) + addr);
}

/* Workqueue callbacks */
void rtl8852b_calib_work(struct work_struct *work) { }
void rtl8852b_periodic_calib(struct work_struct *work) { }

/* PCI driver */
static struct pci_driver rtl8852b_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = rtl8852b_pci_ids,
	.probe = rtl8852b_probe,
	.remove = rtl8852b_remove,
};

static int __init rtl8852b_init(void)
{
	return pci_register_driver(&rtl8852b_pci_driver);
}

static void __exit rtl8852b_exit(void)
{
	pci_unregister_driver(&rtl8852b_pci_driver);
}

module_init(rtl8852b_init);
module_exit(rtl8852b_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("RTL8852B PHL Backport Team");
MODULE_DESCRIPTION("Realtek RTL8852B PHL WiFi Driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_FIRMWARE(RTL8852B_FW_NAME);
MODULE_FIRMWARE(RTL8852B_FW_WOW_NAME);
