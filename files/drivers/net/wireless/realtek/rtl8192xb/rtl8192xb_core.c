/*
 * Realtek RTL8192XB 2.4GHz WiFi Driver - Core
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include "rtl8192xb.h"

#define DRIVER_NAME "rtl8192xb"
#define DRIVER_VERSION "0.1.0"

static const struct pci_device_id rtl8192xb_pci_ids[] = {
	{ PCI_DEVICE(RTL8192XB_VENDOR_ID, RTL8192XB_DEVICE_ID) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, rtl8192xb_pci_ids);

/* MAC80211 operations */
static const struct ieee80211_ops rtl8192xb_ops = {
	.tx = rtl8192xb_tx,
	.start = rtl8192xb_start,
	.stop = rtl8192xb_stop,
	.config = rtl8192xb_config,
	.add_interface = rtl8192xb_add_interface,
	.remove_interface = rtl8192xb_remove_interface,
	.config_interface = rtl8192xb_config_interface,
	.configure_filter = rtl8192xb_configure_filter,
	.bss_info_changed = rtl8192xb_bss_info_changed,
	.sta_state = rtl8192xb_sta_state,
	.conf_tx = rtl8192xb_conf_tx,
	.get_survey = rtl8192xb_get_survey,
	.set_tim = rtl8192xb_set_tim,
	.set_key = rtl8192xb_set_key,
	.sw_scan_start = rtl8192xb_sw_scan_start,
	.sw_scan_complete = rtl8192xb_sw_scan_complete,
	.get_stats = rtl8192xb_get_stats,
	.get_tkip_seq = rtl8192xb_get_tkip_seq,
	.set_frag_threshold = rtl8192xb_set_frag_threshold,
	.set_rts_threshold = rtl8192xb_set_rts_threshold,
	.sta_add = rtl8192xb_sta_add,
	.sta_remove = rtl8192xb_sta_remove,
};

/* PCI probe */
static int rtl8192xb_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct rtl8192xb_priv *priv;
	struct ieee80211_hw *hw;
	int ret;

	pr_info("%s: probing device %04x:%04x\n", DRIVER_NAME, 
		pdev->vendor, pdev->device);

	/* Enable PCI device */
	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return ret;
	}

	/* Request PCI regions */
	ret = pcim_iomap_regions(pdev, 1 << 0, DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request PCI regions\n");
		return ret;
	}

	/* Allocate hw */
	hw = ieee80211_alloc_hw(sizeof(struct rtl8192xb_priv), &rtl8192xb_ops);
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

	/* Map MMIO */
	priv->mmio = pcim_iomap_table(pdev)[0];
	if (!priv->mmio) {
		dev_err(&pdev->dev, "Failed to map MMIO\n");
		ret = -EIO;
		goto err_free_hw;
	}
	priv->mmio_len = pci_resource_len(pdev, 0);

	/* Set up hw */
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

	/* Supported bands */
	hw->wiphy->bands[NL80211_BAND_2GHZ] = &rtl8192xb_band_2ghz;
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

	/* Enable interrupts */
	ret = devm_request_irq(&pdev->dev, pdev->irq,
			       rtl8192xb_interrupt,
			       IRQF_SHARED, DRIVER_NAME, priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request IRQ\n");
		goto err_free_hw;
	}

	/* Initialize hardware */
	ret = rtl8192xb_init_hw(priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize hardware\n");
		goto err_free_hw;
	}

	/* Load firmware */
	ret = rtl8192xb_load_firmware(priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to load firmware\n");
		goto err_deinit_hw;
	}

	/* Register with mac80211 */
	ret = ieee80211_register_hw(hw);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register hw\n");
		goto err_deinit_hw;
	}

	/* Setup debugfs */
	rtl8192xb_debugfs_init(priv);

	pr_info("%s: probe successful\n", DRIVER_NAME);
	return 0;

err_deinit_hw:
	rtl8192xb_deinit_hw(priv);
err_free_hw:
	ieee80211_free_hw(hw);
	return ret;
}

/* PCI remove */
static void rtl8192xb_remove(struct pci_dev *pdev)
{
	struct rtl8192xb_priv *priv = pci_get_drvdata(pdev);
	struct ieee80211_hw *hw = priv->hw;

	pr_info("%s: removing device\n", DRIVER_NAME);

	/* Unregister hw */
	ieee80211_unregister_hw(hw);

	/* Deinitialize hardware */
	rtl8192xb_deinit_hw(priv);

	/* Release firmware */
	if (priv->fw)
		release_firmware(priv->fw);

	ieee80211_free_hw(hw);
}

/* Hardware initialization */
int rtl8192xb_init_hw(struct rtl8192xb_priv *priv)
{
	int ret;

	pr_info("%s: initializing hardware\n", DRIVER_NAME);

	/* Reset hardware */
	rtl8192xb_write32(priv, 0x0000, 0);
	msleep(10);

	/* Configure MAC */
	ret = rtl8192xb_config_mac(priv);
	if (ret)
		return ret;

	/* Configure BB */
	ret = rtl8192xb_config_bb(priv);
	if (ret)
		return ret;

	/* Configure RF */
	ret = rtl8192xb_config_rf(priv);
	if (ret)
		return ret;

	/* Initialize calibration */
	rtl8192xb_calib_init(priv);

	return 0;
}

/* Hardware deinitialization */
int rtl8192xb_deinit_hw(struct rtl8192xb_priv *priv)
{
	pr_info("%s: deinitializing hardware\n", DRIVER_NAME);

	/* Disable interrupts */
	rtl8192xb_write32(priv, 0x0000, 0);

	return 0;
}

/* Firmware loading */
int rtl8192xb_load_firmware(struct rtl8192xb_priv *priv)
{
	const struct firmware *fw;
	int ret;

	pr_info("%s: loading firmware\n", DRIVER_NAME);

	ret = request_firmware(&fw, RTL8192XB_FW_NAME, &priv->pdev->dev);
	if (ret) {
		dev_err(&priv->pdev->dev, "Failed to load firmware %s\n",
			RTL8192XB_FW_NAME);
		return ret;
	}

	priv->fw = fw;
	priv->fw_data = (u8 *)fw->data;
	priv->fw_size = fw->size;

	/* Upload firmware to device */
	/* TODO: Implement firmware upload protocol */

	pr_info("%s: firmware loaded (%zu bytes)\n", DRIVER_NAME, fw->size);
	return 0;
}

/* MAC configuration */
int rtl8192xb_config_mac(struct rtl8192xb_priv *priv)
{
	/* Configure MAC registers */
	rtl8192xb_write32(priv, 0x0000, 0x1);
	return 0;
}

/* BB configuration */
int rtl8192xb_config_bb(struct rtl8192xb_priv *priv)
{
	return 0;
}

/* RF configuration */
int rtl8192xb_config_rf(struct rtl8192xb_priv *priv)
{
	return 0;
}

/* Interrupt handler */
static irqreturn_t rtl8192xb_interrupt(int irq, void *dev_id)
{
	struct rtl8192xb_priv *priv = dev_id;
	u32 isr;

	isr = rtl8192xb_read32(priv, 0x100);
	if (!isr)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

/* MAC80211 callbacks */
static int rtl8192xb_start(struct ieee80211_hw *hw)
{
	return 0;
}

static void rtl8192xb_stop(struct ieee80211_hw *hw)
{
}

static int rtl8192xb_tx(struct ieee80211_hw *hw,
			struct ieee80211_tx_control *control,
			struct sk_buff *skb)
{
	return 0;
}

static int rtl8192xb_config(struct ieee80211_hw *hw, u32 changed)
{
	return 0;
}

static int rtl8192xb_add_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	return 0;
}

static void rtl8192xb_remove_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
}

static int rtl8192xb_config_interface(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_interface_conf *conf)
{
	return 0;
}

static void rtl8192xb_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed_flags,
				       unsigned int *total_flags,
				       u64 multicast)
{
}

static void rtl8192xb_bss_info_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *info,
				       u32 changed)
{
}

static int rtl8192xb_sta_state(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       enum ieee80211_sta_state old_state,
			       enum ieee80211_sta_state new_state)
{
	return 0;
}

static int rtl8192xb_conf_tx(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     u16 queue,
			     const struct ieee80211_tx_queue_params *params)
{
	return 0;
}

static int rtl8192xb_get_survey(struct ieee80211_hw *hw, int idx,
				struct survey_info *survey)
{
	return -ENOENT;
}

static void rtl8192xb_set_tim(struct ieee80211_hw *hw,
			      struct ieee80211_sta *sta, bool set)
{
}

static int rtl8192xb_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			     struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			     struct ieee80211_key_conf *key)
{
	return 0;
}

static int rtl8192xb_sw_scan_start(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	return 0;
}

static void rtl8192xb_sw_scan_complete(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
}

static int rtl8192xb_get_stats(struct ieee80211_hw *hw,
			       struct ieee80211_low_level_stats *stats)
{
	return 0;
}

static u64 rtl8192xb_get_tkip_seq(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  u8 key_idx)
{
	return 0;
}

static int rtl8192xb_set_frag_threshold(struct ieee80211_hw *hw, u32 value)
{
	return 0;
}

static int rtl8192xb_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	return 0;
}

static int rtl8192xb_sta_add(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta)
{
	return 0;
}

static int rtl8192xb_sta_remove(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta)
{
	return 0;
}

/* 2.4GHz band definition */
static const struct ieee80211_channel rtl8192xb_2ghz_channels[] = {
	CHAN2G_1(20), CHAN2G_2(20), CHAN2G_3(20), CHAN2G_4(20),
	CHAN2G_5(20), CHAN2G_6(20), CHAN2G_7(20), CHAN2G_8(20),
	CHAN2G_9(20), CHAN2G_10(20), CHAN2G_11(20), CHAN2G_12(20),
	CHAN2G_12(20), CHAN2G_13(20), CHAN2G_14(20),
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

static struct pci_driver rtl8192xb_pci_driver = {
	.name = "rtl8192xb",
	.id_table = rtl8192xb_pci_ids,
	.probe = rtl8192xb_probe,
	.remove = rtl8192xb_remove,
	.driver.pm = &rtl8192xb_pm_ops,
};

static struct pci_device_id rtl8192xb_pci_ids[] = {
	{ PCI_DEVICE(0x10EC, 0x8192) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, rtl8192xb_pci_ids);

/* Power management */
static const struct dev_pm_ops rtl8192xb_pm_ops = {
	.suspend = rtl8192xb_suspend,
	.resume = rtl8192xb_resume,
	.freeze = rtl8192xb_freeze,
	.thaw = rtl8192xb_thaw,
	.poweroff = rtl8192xb_poweroff,
	.restore = rtl8192xb_restore,
};

static int rtl8192xb_suspend(struct device *dev)
{
	return 0;
}

static int rtl8192xb_resume(struct device *dev)
{
	return 0;
}

static int rtl8192xb_freeze(struct device *dev)
{
	return rtl8192xb_suspend(dev);
}

static int rtl8192xb_thaw(struct device *dev)
{
	return rtl8192xb_resume(dev);
}

static int rtl8192xb_poweroff(struct device *dev)
{
	return rtl8192xb_suspend(dev);
}

static int rtl8192xb_restore(struct device *dev)
{
	return rtl8192xb_resume(dev);
}

/* Module init/exit */
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
