/***************************************************************
** Copyright (C),  2020,  OPPO Mobile Comm Corp.,  Ltd
** VENDOR_EDIT
** File : oppo_display_panel_power.c
** Description : oppo display panel power control
** Version : 1.0
** Date : 2020/06/13
** Author : Li.Sheng@MULTIMEDIA.DISPLAY.LCD
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/06/13        1.0           Build this moudle
******************************************************************/
#include "oplus_display_panel_power.h"
#include <linux/printk.h>
#include <linux/string.h>

PANEL_VOLTAGE_BAK panel_vol_bak[PANEL_VOLTAGE_ID_MAX] = {{0}, {0}, {2, 0, 1, 2, ""}};
u32 panel_need_recovery = 0;
struct drm_panel *p_node = NULL;

int oplus_export_drm_panel(struct drm_panel *panel_node)
{
	printk("%s panel_node = %p\n", __func__, panel_node);
	if (panel_node) {
		p_node = panel_node;
		return 0;
	} else {
		printk("%s error :panel node is null, check the lcm driver!\n", __func__);
		return -1;
	}
}
EXPORT_SYMBOL(oplus_export_drm_panel);

int oplus_panel_set_vg_base(unsigned int panel_vol_value)
{
	int ret = 0;

	if (panel_vol_value > panel_vol_bak[PANEL_VOLTAGE_ID_VG_BASE].voltage_max ||
		panel_vol_value < panel_vol_bak[PANEL_VOLTAGE_ID_VG_BASE].voltage_min) {
		printk("%s error: panel_vol exceeds the range\n", __func__);
		panel_need_recovery = 0;
		return -EINVAL;
	}

	if (panel_vol_value == panel_vol_bak[PANEL_VOLTAGE_ID_VG_BASE].voltage_current) {
		printk("%s panel_vol the same as before\n", __func__);
		panel_need_recovery = 0;
	} else {
		printk("%s set panel_vol = %d\n", __func__, panel_vol_value);
		panel_vol_bak[PANEL_VOLTAGE_ID_VG_BASE].voltage_current = panel_vol_value;
		panel_need_recovery = 1;
	}

	return ret;
}

int dsi_panel_parse_panel_power_cfg(struct panel_voltage_bak *panel_vol)
{
	int ret = 0;
	printk("%s test", __func__);
	if (panel_vol == NULL) {
		printk("%s error handle", __func__);
		return -1;
	}

	memcpy((void *)panel_vol_bak, panel_vol, sizeof(struct panel_voltage_bak)*PANEL_VOLTAGE_ID_MAX);

	return ret;
}
EXPORT_SYMBOL(dsi_panel_parse_panel_power_cfg);

int oplus_panel_need_recovery(unsigned int panel_vol_value)
{
	int ret = 0;

	if (panel_need_recovery == 1) {
		printk("%s \n", __func__);
		ret = 1;
	}

	return ret;
}
EXPORT_SYMBOL(oplus_panel_need_recovery);


