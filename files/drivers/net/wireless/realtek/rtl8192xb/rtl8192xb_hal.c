/*
 * Realtek RTL8192XB HAL Implementation
 * Based on vendor kernel 4.4.176 symbols (rtl8192cd)
 */

#include "rtl8192xb.h"

/* MAC register read/write */
u32 rtl8192xb_read32(struct rtl8192xb_priv *priv, u32 addr)
{
	return ioread32(priv->mmio + addr);
}

void rtl8192xb_write32(struct rtl8192xb_priv *priv, u32 addr, u32 val)
{
	iowrite32(val, priv->mmio + addr);
}

u8 rtl8192xb_read8(struct rtl8192xb_priv *priv, u32 addr)
{
	return ioread8(priv->mmio + addr);
}

void rtl8192xb_write8(struct rtl8192xb_priv *priv, u32 addr, u8 val)
{
	iowrite8(val, priv->mmio + addr);
}

/* RF register access */
int rtl8192xb_rf_write(struct rtl8192xb_priv *priv, u8 path, u32 addr, u32 val)
{
	if (path >= RF_PATH_MAX)
		return -EINVAL;

	/* LSSI write */
	u32 reg = (path << 20) | (addr << 8) | (val & 0xFF);
	iowrite32(reg, priv->mmio + 0x1000);
	udelay(1);

	return 0;
}

u32 rtl8192xb_rf_read(struct rtl8192xb_priv *priv, u8 path, u32 addr)
{
	if (path >= RF_PATH_MAX)
		return 0xFFFFFFFF;

	u32 reg = (0x1 << 31) | (path << 20) | (addr << 8);
	iowrite32(reg, priv->mmio + 0x1000);
	udelay(1);
	return ioread32(priv->mmio + 0x1004) & 0xFF;
}

/* BB register access */
int rtl8192xb_bb_write(struct rtl8192xb_priv *priv, u8 path, u32 addr, u32 val)
{
	if (path >= RF_PATH_MAX)
		return -EINVAL;

	iowrite32(val, priv->mmio + 0x2000 + (path << 16) + addr);
	return 0;
}

u32 rtl8192xb_bb_read(struct rtl8192xb_priv *priv, u8 path, u32 addr)
{
	if (path >= RF_PATH_MAX)
		return 0xFFFFFFFF;

	return ioread32(priv->mmio + 0x2000 + (path << 16) + addr);
}

/* Interrupt handler */
static irqreturn_t rtl8192xb_interrupt(int irq, void *dev_id)
{
	struct rtl8192xb_priv *priv = dev_id;
	u32 isr;

	isr = ioread32(priv->mmio + 0x100);
	if (!isr)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

/* MAC80211 callbacks (stubs) */
static int rtl8192xb_start(struct ieee80211_hw *hw) { return 0; }
static void rtl8192xb_stop(struct ieee80211_hw *hw) { }
static int rtl8192xb_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control, struct sk_buff *skb) { return 0; }
static int rtl8192xb_config(struct ieee80211_hw *hw, u32 changed) { return 0; }
static int rtl8192xb_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif) { return 0; }
static void rtl8192xb_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif) { }
static int rtl8192xb_config_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_interface_conf *conf) { return 0; }
static void rtl8192xb_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags, unsigned int *total_flags, u64 multicast) { }
static void rtl8192xb_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_bss_conf *info, u32 changed) { }
static int rtl8192xb_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta, enum ieee80211_sta_state old_state, enum ieee80211_sta_state new_state) { return 0; }
static int rtl8192xb_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u16 queue, const struct ieee80211_tx_queue_params *params) { return 0; }
static int rtl8192xb_get_survey(struct ieee80211_hw *hw, int idx, struct survey_info *survey) { return -ENOENT; }
static void rtl8192xb_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set) { }
static int rtl8192xb_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd, struct ieee80211_vif *vif, struct ieee80211_sta *sta, struct ieee80211_key_conf *key) { return 0; }
static int rtl8192xb_sw_scan_start(struct ieee80211_hw *hw, struct ieee80211_vif *vif) { return 0; }
static void rtl8192xb_sw_scan_complete(struct ieee80211_hw *hw, struct ieee80211_vif *vif) { }
static int rtl8192xb_get_stats(struct ieee80211_hw *hw, struct ieee80211_low_level_stats *stats) { return 0; }
static u64 rtl8192xb_get_tkip_seq(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta, u8 key_idx) { return 0; }
static int rtl8192xb_set_frag_threshold(struct ieee80211_hw *hw, u32 value) { return 0; }
static int rtl8192xb_set_rts_threshold(struct ieee80211_hw *hw, u32 value) { return 0; }
static int rtl8192xb_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta) { return 0; }
static int rtl8192xb_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta) { return 0; }

/* 2.4GHz band definition */
static const struct ieee80211_channel rtl8192xb_2ghz_channels[] = {
	CHAN2G_1(20), CHAN2G_2(20), CHAN2G_3(20), CHAN2G_4(20),
	CHAN2G_5(20), CHAN2G_6(20), CHAN2G_7(20), CHAN2G_8(20),
	CHAN2G_9(20), CHAN2G_10(20), CHAN2G_11(20), CHAN2G_12(20),
	CHAN2G_13(20), CHAN2G_14(20),
};

static const struct ieee80211_rate rtl8192xb_2ghz_rates[] = {
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

static struct ieee80211_supported_band rtl8192xb_band_2ghz = {
	.channels = rtl8192xb_2ghz_channels,
	.n_channels = ARRAY_SIZE(rtl8192xb_2ghz_channels),
	.bitrates = rtl8192xb_2ghz_rates,
	.n_bitrates = ARRAY_SIZE(rtl8192xb_2ghz_rates),
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
};

/* PCI driver */
static int rtl8192xb_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct rtl8192xb_priv *priv;
	struct ieee80211_hw *hw;
	int ret;

	pr_info("rtl8192xb: probing device %04x:%04x\n", pdev->vendor, pdev->device);

	ret = pcim_enable_device(pdev);
	if (ret) return ret;

	ret = pcim_iomap_regions(pdev, 1 << 0, "rtl8192xb");
	if (ret) return ret;

	hw = ieee80211_alloc_hw(sizeof(struct rtl8192xb_priv), &rtl8192xb_ops);
	if (!hw) return -ENOMEM;

	priv = hw->priv;
	priv->pdev = pdev;
	priv->hw = hw;
	pci_set_drvdata(pdev, priv);

	mutex_init(&priv->mutex);
	mutex_init(&priv->rf_mutex);

	priv->mmio = pcim_iomap_table(pdev)[0];
	if (!priv->mmio) {
		ret = -EIO;
		goto err_free_hw;
	}
	priv->mmio_len = pci_resource_len(pdev, 0);

	SET_IEEE80211_DEV(hw, &pdev->dev);
	hw->queues = 4;
	hw->wiphy->bands[NL80211_BAND_2GHZ] = &rtl8192xb_band_2ghz;
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				      BIT(NL80211_IFTYPE_AP) |
				      BIT(NL80211_IFTYPE_ADHOC) |
				      BIT(NL80211_IFTYPE_MESH_POINT);

	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_RX_INCLUDES_FCS |
		    IEEE80211_HW_QUEUE_CONTROL |
		    IEEE80211_HW_SUPPORTS_PS;

	ret = devm_request_irq(&pdev->dev, pdev->irq, rtl8192xb_interrupt, IRQF_SHARED, "rtl8192xb", NULL);
	if (ret) goto err_free_hw;

	ret = ieee80211_register_hw(hw);
	if (ret) goto err_free_hw;

	return 0;

err_free_hw:
	ieee80211_free_hw(hw);
	return ret;
}

static void rtl8192xb_remove(struct pci_dev *pdev)
{
	ieee80211_unregister_hw(pci_get_drvdata(pdev)->hw);
}

static struct pci_driver rtl8192xb_pci_driver = {
	.name = "rtl8192xb",
	.id_table = rtl8192xb_pci_ids,
	.probe = rtl8192xb_probe,
	.remove = rtl8192xb_remove,
};

static struct pci_device_id rtl8192xb_pci_ids[] = {
	{ PCI_DEVICE(0x10EC, 0x8192) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, rtl8192xb_pci_ids);

static int __init rtl8192xb_init(void)
{
	return pci_register_driver(&rtl8192xb_pci_driver);
}

static void __exit rtl8192xb_exit(void)
{
	pci_unregister_driver(&rtl8192xb_pci_driver);
}

module_init(rtl8192xb_init);
module_exit(rtl8192xb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("RTL8192XB Backport Team");
MODULE_DESCRIPTION("Realtek RTL8192XB 2.4GHz WiFi Driver");
MODULE_VERSION("0.1.0");
MODULE_FIRMWARE("rtlwifi/rtl8192xbfw.bin");
