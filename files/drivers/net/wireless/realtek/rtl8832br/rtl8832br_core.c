/*
 * Realtek RTL8832BR 5GHz WiFi Driver - Core
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include "rtl8832br.h"

#define DRIVER_NAME "rtl8832br"
#define DRIVER_VERSION "0.1.0"

static const struct pci_device_id rtl8832br_pci_ids[] = {
	{ PCI_DEVICE(RTL8832BR_VENDOR_ID, RTL8832BR_DEVICE_ID) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, rtl8832br_pci_ids);

/* PCI device IDs from vendor */
static const struct pci_device_id rtl8832br_pci_tbl[] = {
	{ PCI_VDEVICE(REALTEK, RTL8832BR_DEVICE_ID) },
	{ 0, }
};

/* MAC80211 operations */
static const struct ieee80211_ops rtl8832br_ops = {
	.tx = rtl8832br_tx,
	.start = rtl8832br_start,
	.stop = rtl8832br_stop,
	.config = rtl8832br_config,
	.add_interface = rtl8832br_add_interface,
	.remove_interface = rtl8832br_remove_interface,
	.config_interface = rtl8832br_config_interface,
	.configure_filter = rtl8832br_configure_filter,
	.bss_info_changed = rtl8832br_bss_info_changed,
	.sta_state = rtl8832br_sta_state,
	.conf_tx = rtl8832br_conf_tx,
	.get_survey = rtl8832br_get_survey,
	.set_tim = rtl8832br_set_tim,
	.set_key = rtl8832br_set_key,
	.sw_scan_start = rtl8832br_sw_scan_start,
	.sw_scan_complete = rtl8832br_sw_scan_complete,
	.get_stats = rtl8832br_get_stats,
	.get_tkip_seq = rtl8832br_get_tkip_seq,
	.set_frag_threshold = rtl8832br_set_frag_threshold,
	.set_rts_threshold = rtl8832br_set_rts_threshold,
	.sta_add = rtl8832br_sta_add,
	.sta_remove = rtl8832br_sta_remove,
};

/* PCI probe */
static int rtl8832br_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct rtl8832br_priv *priv;
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
	hw = ieee80211_alloc_hw(sizeof(struct rtl8832br_priv), &rtl8832br_ops);
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
	init_completion(&priv->fw_load_done);
	INIT_WORK(&priv->calib_work, rtl8832br_calib_work);
	INIT_DELAYED_WORK(&priv->periodic_calib, rtl8832br_periodic_calib);

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
	hw->wiphy->bands[NL80211_BAND_5GHZ] = &rtl8832br_band_5ghz;
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
			       rtl8832br_interrupt,
			       IRQF_SHARED, DRIVER_NAME, priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request IRQ\n");
		goto err_free_hw;
	}

	/* Initialize hardware */
	ret = rtl8832br_init_hw(priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize hardware\n");
		goto err_free_hw;
	}

	/* Load firmware */
	ret = rtl8832br_load_firmware(priv);
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
	rtl8832br_debugfs_init(priv);

	pr_info("%s: probe successful\n", DRIVER_NAME);
	return 0;

err_deinit_hw:
	rtl8832br_deinit_hw(priv);
err_free_hw:
	ieee80211_free_hw(hw);
	return ret;
}

/* PCI remove */
static void rtl8832br_remove(struct pci_dev *pdev)
{
	struct rtl8832br_priv *priv = pci_get_drvdata(pdev);
	struct ieee80211_hw *hw = priv->hw;

	pr_info("%s: removing device\n", DRIVER_NAME);

	/* Cancel work */
	cancel_work_sync(&priv->calib_work);
	cancel_delayed_work_sync(&priv->periodic_calib);

	/* Remove debugfs */
	rtl8832br_debugfs_cleanup(priv);

	/* Unregister hw */
	ieee80211_unregister_hw(hw);

	/* Deinitialize hardware */
	rtl8832br_deinit_hw(priv);

	/* Release firmware */
	if (priv->fw)
		release_firmware(priv->fw);

	ieee80211_free_hw(hw);
}

/* Hardware initialization */
int rtl8832br_init_hw(struct rtl8832br_priv *priv)
{
	int ret;

	pr_info("%s: initializing hardware\n", DRIVER_NAME);

	/* Reset hardware */
	rtl8832br_write32(priv, RTL8832BR_CR, 0);
	msleep(10);

	/* Configure MAC */
	ret = rtl8832br_config_mac(priv);
	if (ret)
		return ret;

	/* Configure BB */
	ret = rtl8832br_config_bb(priv);
	if (ret)
		return ret;

	/* Configure RF */
	ret = rtl8832br_config_rf(priv);
	if (ret)
		return ret;

	/* Initialize calibration */
	rtl8832br_iqk_init(priv);
	rtl8832br_tssi_init(priv);
	rtl8832br_txgapk_init(priv);
	rtl8832br_psd_init(priv);
	rtl8832br_power_init(priv);

	return 0;
}

/* Hardware deinitialization */
int rtl8832br_deinit_hw(struct rtl8832br_priv *priv)
{
	pr_info("%s: deinitializing hardware\n", DRIVER_NAME);

	/* Disable interrupts */
	rtl8832br_write32(priv, RTL8832BR_CR, 0);

	return 0;
}

/* Firmware loading */
int rtl8832br_load_firmware(struct rtl8832br_priv *priv)
{
	const struct firmware *fw;
	int ret;

	pr_info("%s: loading firmware\n", DRIVER_NAME);

	ret = request_firmware(&fw, RTL8832BR_FW_NAME, &priv->pdev->dev);
	if (ret) {
		dev_err(&priv->pdev->dev, "Failed to load firmware %s\n",
			RTL8832BR_FW_NAME);
		return ret;
	}

	priv->fw = fw;
	priv->fw_data = (u8 *)fw->data;
	priv->fw_size = fw->size;

	/* Upload firmware to device */
	/* TODO: Implement firmware upload protocol */

	complete_all(&priv->fw_load_done);
	priv->fw_loaded = true;

	pr_info("%s: firmware loaded (%zu bytes)\n", DRIVER_NAME, fw->size);
	return 0;
}

/* MAC configuration */
int rtl8832br_config_mac(struct rtl8832br_priv *priv)
{
	/* Configure MAC registers */
	rtl8832br_write32(priv, RTL8832BR_CR, 0x1);
	/* TODO: More MAC config */
	return 0;
}

/* BB configuration */
int rtl8832br_config_bb(struct rtl8832br_priv *priv)
{
	/* Configure BB registers */
	/* TODO: More BB config */
	return 0;
}

/* RF configuration (from halrf_config_8832br) */
int rtl8832br_config_rf(struct rtl8832br_priv *priv)
{
	/* Configure RF registers for both paths */
	/* Based on halrf_config_8832br_radio_a_reg/b_reg */
	return 0;
}

/* Interrupt handler */
static irqreturn_t rtl8832br_interrupt(int irq, void *dev_id)
{
	struct rtl8832br_priv *priv = dev_id;
	u32 isr;

	isr = rtl8832br_read32(priv, 0x100); /* ISR register */
	if (!isr)
		return IRQ_NONE;

	/* Handle interrupts */
	/* TODO: Handle specific interrupt types */

	return IRQ_HANDLED;
}

/* MAC80211 callbacks */
static int rtl8832br_start(struct ieee80211_hw *hw)
{
	struct rtl8832br_priv *priv = hw->priv;
	pr_info("%s: start\n", DRIVER_NAME);
	return 0;
}

static void rtl8832br_stop(struct ieee80211_hw *hw)
{
	struct rtl8832br_priv *priv = hw->priv;
	pr_info("%s: stop\n", DRIVER_NAME);
}

static int rtl8832br_tx(struct ieee80211_hw *hw,
			struct ieee80211_tx_control *control,
			struct sk_buff *skb)
{
	/* TODO: Implement TX */
	return 0;
}

static int rtl8832br_config(struct ieee80211_hw *hw, u32 changed)
{
	return 0;
}

static int rtl8832br_add_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	return 0;
}

static void rtl8832br_remove_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
}

static int rtl8832br_config_interface(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_interface_conf *conf)
{
	return 0;
}

static void rtl8832br_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed_flags,
				       unsigned int *total_flags,
				       u64 multicast)
{
}

static void rtl8832br_bss_info_changed(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *info,
				       u32 changed)
{
}

static int rtl8832br_sta_state(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       enum ieee80211_sta_state old_state,
			       enum ieee80211_sta_state new_state)
{
	return 0;
}

static int rtl8832br_conf_tx(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     u16 queue,
			     const struct ieee80211_tx_queue_params *params)
{
	return 0;
}

static int rtl8832br_get_survey(struct ieee80211_hw *hw, int idx,
				struct survey_info *survey)
{
	return -ENOENT;
}

static void rtl8832br_set_tim(struct ieee80211_hw *hw,
			      struct ieee80211_sta *sta, bool set)
{
}

static int rtl8832br_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			     struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			     struct ieee80211_key_conf *key)
{
	return 0;
}

static int rtl8832br_sw_scan_start(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	return 0;
}

static void rtl8832br_sw_scan_complete(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
}

static int rtl8832br_get_stats(struct ieee80211_hw *hw,
			       struct ieee80211_low_level_stats *stats)
{
	return 0;
}

static u64 rtl8832br_get_tkip_seq(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  u8 key_idx)
{
	return 0;
}

static int rtl8832br_set_frag_threshold(struct ieee80211_hw *hw, u32 value)
{
	return 0;
}

static int rtl8832br_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	return 0;
}

static int rtl8832br_sta_add(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta)
{
	return 0;
}

static int rtl8832br_sta_remove(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta)
{
	return 0;
}

/* 5GHz band definition */
static const struct ieee80211_channel rtl8832br_5ghz_channels[] = {
	CHAN5G_36(20), CHAN5G_40(20), CHAN5G_44(20), CHAN5G_48(20),
	CHAN5G_52(20), CHAN5G_56(20), CHAN5G_60(20), CHAN5G_64(20),
	CHAN5G_100(20), CHAN5G_104(20), CHAN5G_108(20), CHAN5G_112(20),
	CHAN5G_116(20), CHAN5G_120(20), CHAN5G_124(20), CHAN5G_128(20),
	CHAN5G_132(20), CHAN5G_136(20), CHAN5G_140(20), CHAN5G_144(20),
	CHAN5G_149(20), CHAN5G_153(20), CHAN5G_157(20), CHAN5G_161(20),
	CHAN5G_165(20),
};

static const struct ieee80211_rate rtl8832br_5ghz_rates[] = {
	{ .bitrate = 60, .hw_value = 0, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 90, .hw_value = 1, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 120, .hw_value = 2, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 180, .hw_value = 3, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 240, .hw_value = 4, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 360, .hw_value = 5, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 480, .hw_value = 6, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 540, .hw_value = 7, .flags = IEEE80211_RATE_MANDATORY_A },
	{ .bitrate = 60, .hw_value = 8, .flags = 0 },
	{ .bitrate = 90, .hw_value = 9, .flags = 0 },
	{ .bitrate = 120, .hw_value = 10, .flags = 0 },
	{ .bitrate = 180, .hw_value = 11, .flags = 0 },
	{ .bitrate = 240, .hw_value = 12, .flags = 0 },
	{ .bitrate = 360, .hw_value = 13, .flags = 0 },
	{ .bitrate = 480, .hw_value = 14, .flags = 0 },
	{ .bitrate = 540, .hw_value = 15, .flags = 0 },
};

static struct ieee80211_supported_band rtl8832br_band_5ghz = {
	.channels = rtl8832br_5ghz_channels,
	.n_channels = ARRAY_SIZE(rtl8832br_5ghz_channels),
	.bitrates = rtl8832br_5ghz_rates,
	.n_bitrates = ARRAY_SIZE(rtl8832br_5ghz_rates),
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

static struct pci_driver rtl8832br_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = rtl8832br_pci_ids,
	.probe = rtl8832br_probe,
	.remove = rtl8832br_remove,
	.driver.pm = &rtl8832br_pm_ops,
};

static struct pci_device_id rtl8832br_pci_ids[] = {
	{ PCI_DEVICE(RTL8832BR_VENDOR_ID, RTL8832BR_DEVICE_ID) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, rtl8832br_pci_ids);

/* Power management */
static const struct dev_pm_ops rtl8832br_pm_ops = {
	.suspend = rtl8832br_suspend,
	.resume = rtl8832br_resume,
	.freeze = rtl8832br_freeze,
	.thaw = rtl8832br_thaw,
	.poweroff = rtl8832br_poweroff,
	.restore = rtl8832br_restore,
};

static int rtl8832br_suspend(struct device *dev)
{
	/* TODO: Implement suspend */
	return 0;
}

static int rtl8832br_resume(struct device *dev)
{
	/* TODO: Implement resume */
	return 0;
}

static int rtl8832br_freeze(struct device *dev)
{
	return rtl8832br_suspend(dev);
}

static int rtl8832br_thaw(struct device *dev)
{
	return rtl8832br_resume(dev);
}

static int rtl8832br_poweroff(struct device *dev)
{
	return rtl8832br_suspend(dev);
}

static int rtl8832br_restore(struct device *dev)
{
	return rtl8832br_resume(dev);
}

/* Module init/exit */
static int __init rtl8832br_init(void)
{
	return pci_register_driver(&rtl8832br_pci_driver);
}

static void __exit rtl8832br_exit(void)
{
	pci_unregister_driver(&rtl8832br_pci_driver);
}

module_init(rtl8832br_init);
module_exit(rtl8832br_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("RTL8832BR Backport Team");
MODULE_DESCRIPTION("Realtek RTL8832BR 5GHz WiFi Driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_FIRMWARE(RTL8832BR_FW_NAME);
