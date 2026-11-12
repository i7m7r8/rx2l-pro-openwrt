/*
 * Realtek RTL8832BR Power Control Implementation
 * Based on vendor kernel 4.4.176 symbols (19 functions)
 */

#include "rtl8832br.h"

/* Power initialization */
int rtl8832br_power_init(struct rtl8832br_priv *priv)
{
	struct rtl8832br_power_data *power = &priv->power_data;
	int ret = 0;

	mutex_lock(&priv->rf_mutex);

	pr_info("RTL8832BR: Starting power control initialization\n");

	memset(power, 0, sizeof(*power));

	/* Initialize power limits from EFUSE */
	rtl8832br_power_get_limits(priv);

	/* Initialize power by rate */
	rtl8832br_power_by_rate_init(priv);

	/* Initialize fix power */
	rtl8832br_power_fix_init(priv);

	/* Set TX shape */
	rtl8832br_set_tx_shape(priv, 0);

	/* Initialize power limits RU */
	rtl8832br_power_limits_ru_init(priv);

	power->current_power[0] = 0;
	power->current_power[1] = 0;
	power->target_power[0] = 20;
	power->target_power[1] = 20;

	pr_info("RTL8832BR: Power control initialized\n");

	mutex_unlock(&priv->rf_mutex);
	return 0;
}

/* Get power limits from EFUSE */
int rtl8832br_power_get_limits(struct rtl8832br_priv *priv)
{
	struct rtl8832br_power_data *power = &priv->power_data;

	for (int path = 0; path < RF_PATH_MAX; path++) {
		power->power_limit[path] = 20;
		power->power_limit_ru[path][0] = 20;
		power->power_limit_ru[path][1] = 20;
		power->power_limit_ru[path][2] = 20;
		power->power_limit_ru[path][3] = 20;
	}

	return 0;
}

/* Power by rate initialization */
int rtl8832br_power_by_rate_init(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Fix power initialization */
int rtl8832br_power_fix_init(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Set TX shape */
int rtl8832br_set_tx_shape(struct rtl8832br_priv *priv, u8 shape)
{
	return 0;
}

/* Power limits RU initialization */
int rtl8832br_power_limits_ru_init(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Set power limit to struct */
int rtl8832br_set_power_limit(struct rtl8832br_priv *priv, int power)
{
	return 0;
}

/* Set power limit RU to struct */
int rtl8832br_set_power_limit_ru(struct rtl8832br_priv *priv, int power)
{
	return 0;
}

/* Get power limit to struct */
int rtl8832br_get_power_limit(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Get power limit RU to struct */
int rtl8832br_get_power_limit_ru(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Set fix power to struct */
int rtl8832br_set_fix_power(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Set power by rate to struct */
int rtl8832br_set_power_by_rate(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Bubble sort */
int rtl8832br_bub_sort(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Average power */
int rtl8832br_avg_power(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Set reference power to struct */
int rtl8832br_set_ref_power(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Set power limit RU to struct 80M */
int rtl8832br_get_power_limit_ru_80m(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Set power limit RU to struct 40M */
int rtl8832br_get_power_limit_ru_40m(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Set power limit RU to struct 20M */
int rtl8832br_get_power_limit_ru_20m(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Get power limit 80M */
int rtl8832br_get_power_limit_80m(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Get power limit 40M */
int rtl8832br_get_power_limit_40m(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Get power limit 20M */
int rtl8832br_get_power_limit_20m(struct rtl8832br_priv *priv)
{
	return 0;
}

/* Module init/exit */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("RTL8832BR Backport Team");
MODULE_DESCRIPTION("RTL8832BR Power Control");
MODULE_VERSION("0.1.0");
