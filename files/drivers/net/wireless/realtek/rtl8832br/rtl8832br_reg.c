/*
 * Realtek RTL8832BR Register Access Functions
 */

#include "rtl8832br.h"

/* Read 32-bit register */
u32 rtl8832br_read32(struct rtl8832br_priv *priv, u32 addr)
{
	return ioread32(priv->mmio + addr);
}

/* Write 32-bit register */
void rtl8832br_write32(struct rtl8832br_priv *priv, u32 addr, u32 val)
{
	iowrite32(val, priv->mmio + addr);
}

/* Read 8-bit register */
u8 rtl8832br_read8(struct rtl8832br_priv *priv, u32 addr)
{
	return ioread8(priv->mmio + addr);
}

/* Write 8-bit register */
void rtl8832br_write8(struct rtl8832br_priv *priv, u32 addr, u8 val)
{
	iowrite8(val, priv->mmio + addr);
}

/* RF register access */
int rtl8832br_rf_write(struct rtl8832br_priv *priv, u8 path, u32 addr, u32 val)
{
	u32 reg;

	if (path >= RF_PATH_MAX)
		return -EINVAL;

	reg = (path << 20) | (addr << 8) | (val & 0xFF);
	rtl8832br_write32(priv, 0x1000, reg);
	udelay(1);

	return 0;
}

u32 rtl8832br_rf_read(struct rtl8832br_priv *priv, u8 path, u32 addr)
{
	u32 val = 0;
	u32 reg;

	if (path >= RF_PATH_MAX)
		return 0xFFFFFFFF;

	reg = (0x1 << 31) | (path << 20) | (addr << 8);
	rtl8832br_write32(priv, 0x1000, reg);
	udelay(1);
	val = rtl8832br_read32(priv, 0x1004);
	return val & 0xFF;
}

/* BB register access */
int rtl8832br_bb_write(struct rtl8832br_priv *priv, u8 path, u32 addr, u32 val)
{
	if (path >= RF_PATH_MAX)
		return -EINVAL;

	rtl8832br_write32(priv, RTL8832BR_BB_REG_BASE + (path << 16) + addr, val);
	return 0;
}

u32 rtl8832br_bb_read(struct rtl8832br_priv *priv, u8 path, u32 addr)
{
	if (path >= RF_PATH_MAX)
		return 0xFFFFFFFF;

	return rtl8832br_read32(priv, RTL8832BR_BB_REG_BASE + (path << 16) + addr);
}

/* EFUSE access */
int rtl8832br_efuse_read(struct rtl8832br_priv *priv, u32 offset, u8 *data, u32 len)
{
	u32 val;
	u32 i;

	for (i = 0; i < len; i++) {
		rtl8832br_write32(priv, 0x2000, (offset + i) | 0x80000000);
		udelay(10);
		val = rtl8832br_read32(priv, 0x2004);
		data[i] = val & 0xFF;
	}

	return 0;
}

int rtl8832br_efuse_write(struct rtl8832br_priv *priv, u32 offset, u8 *data, u32 len)
{
	return -EPERM;
}

/* Calibration function stubs */
int rtl8832br_iqk_init(struct rtl8832br_priv *priv) { return 0; }
int rtl8832br_tssi_init(struct rtl8832br_priv *priv) { return 0; }
int rtl8832br_txgapk_init(struct rtl8832br_priv *priv) { return 0; }
int rtl8832br_psd_init(struct rtl8832br_priv *priv) { return 0; }
int rtl8832br_power_init(struct rtl8832br_priv *priv) { return 0; }

/* Workqueue callbacks */
void rtl8832br_calib_work(struct work_struct *work)
{
	struct rtl8832br_priv *priv = container_of(work, struct rtl8832br_priv, calib_work);
	rtl8832br_iqk_init(priv);
	rtl8832br_tssi_init(priv);
}

void rtl8832br_periodic_calib(struct work_struct *work)
{
	struct rtl8832br_priv *priv = container_of(work, struct rtl8832br_priv, periodic_calib.work);
	rtl8832br_iqk_init(priv);
	rtl8832br_tssi_init(priv);
	schedule_delayed_work(&priv->periodic_calib, 3600 * HZ);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("RTL8832BR Backport Team");
MODULE_DESCRIPTION("RTL8832BR Register Access");
MODULE_VERSION("0.1.0");
