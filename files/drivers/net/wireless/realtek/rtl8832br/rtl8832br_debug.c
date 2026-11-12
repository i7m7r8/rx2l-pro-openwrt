/*
 * Realtek RTL8832BR DebugFS Interface
 */

#include "rtl8832br.h"

#ifdef CONFIG_DEBUG_FS

static int rtl8832br_debugfs_reg_read(struct seq_file *m, void *data)
{
	struct rtl8832br_priv *priv = m->private;
	u32 val;

	mutex_lock(&priv->mutex);
	val = rtl8832br_read32(priv, 0x0000);
	seq_printf(m, "CR (0x0000): 0x%08x\n", val);
	val = rtl8832br_read32(priv, 0x0010);
	seq_printf(m, "MACID (0x0010): 0x%08x\n", val);
	val = rtl8832br_read32(priv, 0x0018);
	seq_printf(m, "BSSID (0x0018): 0x%08x\n", val);
	mutex_unlock(&priv->mutex);

	return 0;
}

static int rtl8832br_debugfs_reg_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtl8832br_debugfs_reg_read, inode->i_private);
}

static const struct file_operations rtl8832br_debugfs_reg_fops = {
	.open = rtl8832br_debugfs_reg_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtl8832br_debugfs_iqk_read(struct seq_file *m, void *data)
{
	struct rtl8832br_priv *priv = m->private;
	struct rtl8832br_iqk_data *iqk = &priv->iqk_data;

	seq_printf(m, "IQK Done: %s\n", iqk->iqk_done ? "YES" : "NO");
	seq_printf(m, "IQK Step: %d\n", iqk->iqk_step);
	seq_printf(m, "IQK Preset: %d\n", iqk->iqk_preset);
	seq_printf(m, "IQK SRAM: %d\n", iqk->iqk_sram);

	for (int i = 0; i < 4; i++) {
		seq_printf(m, "XYM[%d]: %d %d %d %d\n", i,
			iqk->iqk_xym[i][0], iqk->iqk_xym[i][1],
			iqk->iqk_xym[i][2], iqk->iqk_xym[i][3]);
	}

	return 0;
}

static int rtl8832br_debugfs_iqk_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtl8832br_debugfs_iqk_read, inode->i_private);
}

static const struct file_operations rtl8832br_debugfs_iqk_fops = {
	.open = rtl8832br_debugfs_iqk_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtl8832br_debugfs_tssi_read(struct seq_file *m, void *data)
{
	struct rtl8832br_priv *priv = m->private;
	struct rtl8832br_tssi_data *tssi = &priv->tssi_data;

	seq_printf(m, "TSSI Done: %s\n", tssi->tssi_done ? "YES" : "NO");
	seq_printf(m, "TSSI Step: %d\n", tssi->tssi_step);
	seq_printf(m, "TSSI Enable: %s\n", tssi->tssi_enable ? "YES" : "NO");

	for (int i = 0; i < RF_PATH_MAX; i++) {
		seq_printf(m, "Slope[%d]: %d\n", i, tssi->tssi_slope[i]);
		seq_printf(m, "Trim[%d]: %d\n", i, tssi->tssi_trim[i]);
		seq_printf(m, "EFUSE Slope[%d]: %d\n", i, tssi->tssi_efuse_slope[i]);
		seq_printf(m, "EFUSE Trim[%d]: %d\n", i, tssi->tssi_efuse_trim[i]);
		seq_printf(m, "CW[%d]: %d\n", i, tssi->tssi_cw[i]);
		seq_printf(m, "DE[%d]: %d\n", i, tssi->tssi_de[i]);
	}

	return 0;
}

static int rtl8832br_debugfs_tssi_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtl8832br_debugfs_tssi_read, inode->i_private);
}

static const struct file_operations rtl8832br_debugfs_tssi_fops = {
	.open = rtl8832br_debugfs_tssi_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtl8832br_debugfs_txgapk_read(struct seq_file *m, void *data)
{
	struct rtl8832br_priv *priv = m->private;
	struct rtl8832br_txgapk_data *txgapk = &priv->txgapk_data;

	seq_printf(m, "TXGAPK Done: %s\n", txgapk->txgapk_done ? "YES" : "NO");
	seq_printf(m, "TXGAPK Step: %d\n", txgapk->txgapk_step);

	return 0;
}

static int rtl8832br_debugfs_txgapk_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtl8832br_debugfs_txgapk_read, inode->i_private);
}

static const struct file_operations rtl8832br_debugfs_txgapk_fops = {
	.open = rtl8832br_debugfs_txgapk_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtl8832br_debugfs_psd_read(struct seq_file *m, void *data)
{
	struct rtl8832br_priv *priv = m->private;
	struct rtl8832br_psd_data *psd = &priv->psd_data;

	seq_printf(m, "PSD Done: %s\n", psd->psd_done ? "YES" : "NO");
	seq_printf(m, "PSD Step: %d\n", psd->psd_step);
	seq_printf(m, "DAC Off[0]: %d\n", psd->dac_off[0]);
	seq_printf(m, "DAC Off[1]: %d\n", psd->dac_off[1]);
	seq_printf(m, "ADC Off[0]: %d\n", psd->adc_off[0]);
	seq_printf(m, "ADC Off[1]: %d\n", psd->adc_off[1]);

	return 0;
}

static int rtl8832br_debugfs_psd_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtl8832br_debugfs_psd_read, inode->i_private);
}

static const struct file_operations rtl8832br_debugfs_psd_fops = {
	.open = rtl8832br_debugfs_psd_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtl8832br_debugfs_power_read(struct seq_file *m, void *data)
{
	struct rtl8832br_priv *priv = m->private;
	struct rtl8832br_power_data *power = &priv->power_data;

	seq_printf(m, "Current Power[0]: %d\n", power->current_power[0]);
	seq_printf(m, "Current Power[1]: %d\n", power->current_power[1]);
	seq_printf(m, "Target Power[0]: %d\n", power->target_power[0]);
	seq_printf(m, "Target Power[1]: %d\n", power->target_power[1]);

	for (int i = 0; i < RF_PATH_MAX; i++) {
		seq_printf(m, "Power Limit[%d]: %d\n", i, power->power_limit[i]);
		for (int j = 0; j < 4; j++) {
			seq_printf(m, "Power Limit RU[%d][%d]: %d\n", i, j, power->power_limit_ru[i][j]);
		}
	}

	return 0;
}

static int rtl8832br_debugfs_power_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtl8832br_debugfs_power_read, inode->i_private);
}

static const struct file_operations rtl8832br_debugfs_power_fops = {
	.open = rtl8832br_debugfs_power_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtl8832br_debugfs_efuse_read(struct seq_file *m, void *data)
{
	struct rtl8832br_priv *priv = m->private;

	seq_printf(m, "EFUSE Valid: %s\n", priv->efuse_valid ? "YES" : "NO");

	for (int i = 0; i < 256; i += 16) {
		seq_printf(m, "%04x: ", i);
		for (int j = 0; j < 16; j++) {
			seq_printf(m, "%02x ", priv->efuse_data[i + j]);
		}
		seq_printf(m, "\n");
	}

	return 0;
}

static int rtl8832br_debugfs_efuse_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtl8832br_debugfs_efuse_read, inode->i_private);
}

static const struct file_operations rtl8832br_debugfs_efuse_fops = {
	.open = rtl8832br_debugfs_efuse_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void rtl8832br_debugfs_init(struct rtl8832br_priv *priv)
{
	struct dentry *dir;

	dir = debugfs_create_dir("rtl8832br", NULL);
	if (!dir) {
		pr_err("RTL8832BR: Failed to create debugfs directory\n");
		return;
	}

	debugfs_create_file("regs", 0444, dir, priv, &rtl8832br_debugfs_reg_fops);
	debugfs_create_file("iqk", 0444, dir, priv, &rtl8832br_debugfs_iqk_fops);
	debugfs_create_file("tssi", 0444, dir, priv, &rtl8832br_debugfs_tssi_fops);
	debugfs_create_file("txgapk", 0444, dir, priv, &rtl8832br_debugfs_txgapk_fops);
	debugfs_create_file("psd", 0444, dir, priv, &rtl8832br_debugfs_psd_fops);
	debugfs_create_file("power", 0444, dir, priv, &rtl8832br_debugfs_power_fops);
	debugfs_create_file("efuse", 0444, dir, priv, &rtl8832br_debugfs_efuse_fops);

	priv->debugfs_dir = dir;
}

void rtl8832br_debugfs_cleanup(struct rtl8832br_priv *priv)
{
	if (priv->debugfs_dir)
		debugfs_remove_recursive(priv->debugfs_dir);
}

#else /* CONFIG_DEBUG_FS */

void rtl8832br_debugfs_init(struct rtl8832br_priv *priv) {}
void rtl8832br_debugfs_cleanup(struct rtl8832br_priv *priv) {}

#endif /* CONFIG_DEBUG_FS */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("RTL8832BR Backport Team");
MODULE_DESCRIPTION("RTL8832BR DebugFS Interface");
MODULE_VERSION("0.1.0");
