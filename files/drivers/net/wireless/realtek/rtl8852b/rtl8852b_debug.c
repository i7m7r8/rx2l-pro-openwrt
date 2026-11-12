/*
 * Realtek RTL8852B PHL DebugFS Interface
 */

#include "rtl8852b.h"

#ifdef CONFIG_DEBUG_FS

static int rtl8852b_debugfs_reg_read(struct seq_file *m, void *data)
{
	struct rtl8852b_priv *priv = m->private;
	u32 val;

	mutex_lock(&priv->mutex);
	val = rtl8852b_read32(priv, 0x0000);
	seq_printf(m, "CR (0x0000): 0x%08x\n", val);
	val = rtl8852b_read32(priv, 0x0010);
	seq_printf(m, "MACID (0x0010): 0x%08x\n", val);
	val = rtl8852b_read32(priv, 0x0018);
	seq_printf(m, "BSSID (0x0018): 0x%08x\n", val);
	mutex_unlock(&priv->mutex);

	return 0;
}

static int rtl8852b_debugfs_reg_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtl8852b_debugfs_reg_read, inode->i_private);
}

static const struct file_operations rtl8852b_debugfs_reg_fops = {
	.open = rtl8852b_debugfs_reg_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtl8852b_debugfs_phl_read(struct seq_file *m, void *data)
{
	struct rtl8852b_priv *priv = m->private;

	seq_printf(m, "PHL Initialized: %s\n", priv->phl_com ? "YES" : "NO");
	seq_printf(m, "HALMAC Initialized: %s\n", priv->halmac ? "YES" : "NO");
	seq_printf(m, "Current Channel: %d\n", priv->current_channel);
	seq_printf(m, "Current BW: %d\n", priv->current_bw);
	seq_printf(m, "Current Band: %d\n", priv->current_band);

	return 0;
}

static int rtl8852b_debugfs_phl_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtl8852b_debugfs_phl_read, inode->i_private);
}

static const struct file_operations rtl8852b_debugfs_phl_fops = {
	.open = rtl8852b_debugfs_phl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtl8852b_debugfs_fw_read(struct seq_file *m, void *data)
{
	struct rtl8852b_priv *priv = m->private;

	seq_printf(m, "FW Loaded: %s\n", priv->fw ? "YES" : "NO");
	if (priv->fw) {
		seq_printf(m, "FW Size: %zu bytes\n", priv->fw_size);
	}
	seq_printf(m, "FW WOW Loaded: %s\n", priv->fw_wow ? "YES" : "NO");

	return 0;
}

static int rtl8852b_debugfs_fw_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtl8852b_debugfs_fw_read, inode->i_private);
}

static const struct file_operations rtl8852b_debugfs_fw_fops = {
	.open = rtl8852b_debugfs_fw_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void rtl8852b_debugfs_init(struct rtl8852b_priv *priv)
{
	struct dentry *dir;

	dir = debugfs_create_dir("rtl8852b", NULL);
	if (!dir) {
		pr_err("RTL8852B: Failed to create debugfs directory\n");
		return;
	}

	debugfs_create_file("regs", 0444, dir, priv, &rtl8852b_debugfs_reg_fops);
	debugfs_create_file("phl", 0444, dir, priv, &rtl8852b_debugfs_phl_fops);
	debugfs_create_file("firmware", 0444, dir, priv, &rtl8852b_debugfs_fw_fops);

	priv->debugfs_dir = dir;
}

void rtl8852b_debugfs_cleanup(struct rtl8852b_priv *priv)
{
	if (priv->debugfs_dir)
		debugfs_remove_recursive(priv->debugfs_dir);
}

#else /* CONFIG_DEBUG_FS */

void rtl8852b_debugfs_init(struct rtl8852b_priv *priv) {}
void rtl8852b_debugfs_cleanup(struct rtl8852b_priv *priv) {}

#endif /* CONFIG_DEBUG_FS */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("RTL8852B PHL Backport Team");
MODULE_DESCRIPTION("RTL8852B PHL DebugFS Interface");
MODULE_VERSION("0.1.0");
