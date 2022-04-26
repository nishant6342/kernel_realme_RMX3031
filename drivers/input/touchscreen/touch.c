/***************************************************
 * File:touch.c
 * VENDOR_EDIT
 * Copyright (c)  2008- 2030  Oppo Mobile communication Corp.ltd.
 * Description:
 *             tp dev
 * Version:1.0:
 * Date created:2016/09/02
 * Author: hao.wang@Bsp.Driver
 * TAG: BSP.TP.Init
*/

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include "oppo_touchscreen/tp_devices.h"
#include "oppo_touchscreen/touchpanel_common.h"
#include <soc/oplus/system/oplus_project.h>
#include <soc/oppo/device_info.h>
#include "touch.h"

#define MAX_LIMIT_DATA_LENGTH         100
extern char *saved_command_line;
int g_tp_dev_vendor = TP_UNKNOWN;
int g_tp_prj_id = 0;
static bool is_tp_type_got_in_match = false;    /*indicate whether the tp type is specified in the dts*/

/*if can not compile success, please update vendor/oppo_touchsreen*/
struct tp_dev_name tp_dev_names[] = {
	{TP_OFILM, "OFILM"},
	{TP_BIEL, "BIEL"},
	{TP_TRULY, "TRULY"},
	{TP_BOE, "BOE"},
	{TP_G2Y, "G2Y"},
	{TP_TPK, "TPK"},
	{TP_JDI, "JDI"},
	{TP_TIANMA, "TIANMA"},
	{TP_SAMSUNG, "SAMSUNG"},
	{TP_DSJM, "DSJM"},
	{TP_BOE_B8, "BOEB8"},
	{TP_INNOLUX, "INNOLUX"},
	{TP_HIMAX_DPT, "DPT"},
	{TP_AUO, "AUO"},
	{TP_DEPUTE, "DEPUTE"},
	{TP_HUAXING, "HUAXING"},
	{TP_HLT, "HLT"},
	{TP_DJN, "DJN"},
	{TP_UNKNOWN, "UNKNOWN"},
};

typedef enum {
	TP_INDEX_NULL,
	himax_83112a,
	himax_83112f,
	ili9881_auo,
	ili9881_tm,
	nt36525b_boe,
	nt36525b_hlt,
	nt36672c,
	ili9881_inx,
	goodix_gt9886,
	focal_ft3518,
	td4330,
	himax_83112b
} TP_USED_INDEX;
TP_USED_INDEX tp_used_index  = TP_INDEX_NULL;

#define GET_TP_DEV_NAME(tp_type) ((tp_dev_names[tp_type].type == (tp_type))?tp_dev_names[tp_type].name:"UNMATCH")

#ifndef CONFIG_MTK_FB
void primary_display_esd_check_enable(int enable)
{
	return;
}
EXPORT_SYMBOL(primary_display_esd_check_enable);
#endif /*CONFIG_MTK_FB*/

bool __init tp_judge_ic_match(char *tp_ic_name)
{
	return true;
}

bool  tp_judge_ic_match_commandline(struct panel_info *panel_data)
{
	int prj_id = 0;
	int i = 0;
	bool ic_matched = false;
	prj_id = get_project();
	pr_err("[TP] get_project() = %d \n", prj_id);
	pr_err("[TP] boot_command_line = %s \n", saved_command_line);

	for (i = 0; i < panel_data->project_num; i++) {
		if (prj_id == panel_data->platform_support_project[i]) {
			g_tp_prj_id = panel_data->platform_support_project_dir[i];

			if (strstr(saved_command_line, panel_data->platform_support_commandline[i])
					|| strstr("default_commandline", panel_data->platform_support_commandline[i])) {
				pr_err("[TP] Driver match the project\n");
				ic_matched = true;
			}
		}
	}

	if (!ic_matched) {
		pr_err("[TP] Driver does not match the project\n");
		pr_err("Lcd module not found\n");
		return false;
	}

	switch (prj_id) {
	case 20615:
	case 20662:
	case 20619:
	case 21609:
	case 21651:
		pr_info("[TP] case 20615/20662/20619/21609/21651\n");
		is_tp_type_got_in_match = true;
		pr_err("[TP] touch ic = nt36672c_tianma \n");
		tp_used_index = focal_ft3518;
		g_tp_dev_vendor = TP_SAMSUNG;
		break;
	case 19131:
	case 19132:
	case 19133:
	case 19420:
	case 19421:

#ifdef CONFIG_MACH_MT6873
		pr_info("[TP] case 19131\n");
		is_tp_type_got_in_match = true;

		if (strstr(saved_command_line, "nt36672c_fhdp_dsi_vdo_auo_cphy_90hz_tianma")) {
			pr_err("[TP] touch ic = nt36672c_tianma \n");
			tp_used_index = nt36672c;
			g_tp_dev_vendor = TP_TIANMA;
		}

		if (strstr(saved_command_line, "nt36672c_fhdp_dsi_vdo_auo_cphy_120hz_tianma")) {
			pr_err("[TP] touch ic = nt36672c_tianma \n");
			tp_used_index = nt36672c;
			g_tp_dev_vendor = TP_TIANMA;
		}

		if (strstr(saved_command_line, "nt36672c_fhdp_dsi_vdo_auo_cphy_90hz_jdi")) {
			pr_err("[TP] touch ic = nt36672c_jdi \n");
			tp_used_index = nt36672c;
			g_tp_dev_vendor = TP_JDI;
		}

		if (strstr(saved_command_line, "hx83112f_fhdp_dsi_vdo_auo_cphy_90hz_jdi")) {
			pr_err("[TP] touch ic = hx83112f_jdi \n");
			tp_used_index = himax_83112f;
			g_tp_dev_vendor = TP_JDI;
		}

#endif
		break;

	case 20001:
	case 20002:
	case 20003:
	case 20200:
		pr_info("[TP] case 20001\n");
		is_tp_type_got_in_match = true;

		if (strstr(saved_command_line, "nt36672c")) {
			pr_err("[TP] touch ic = nt36672c_jdi \n");
			tp_used_index = nt36672c;
			g_tp_dev_vendor = TP_JDI;
		}

		if (strstr(saved_command_line, "hx83112f")) {
			pr_err("[TP] touch ic = hx83112f_tianma \n");
			tp_used_index = himax_83112f;
			g_tp_dev_vendor = TP_TIANMA;
		}

		break;

	case 20682:
		pr_info("[TP] case 20682\n");

		if (strstr(saved_command_line, "nt36672c_tianma")) {
			g_tp_dev_vendor = TP_TIANMA;

		} else if (strstr(saved_command_line, "nt36672c_jdi")) {
			g_tp_dev_vendor = TP_JDI;

		} else {
			g_tp_dev_vendor = TP_UNKNOWN;
		}

		pr_err("[TP] g_tp_dev_vendor: %s\n", tp_dev_names[g_tp_dev_vendor].name);
        break;
	case 18531:
		is_tp_type_got_in_match = true;

		if (strstr(saved_command_line, "dsjm_jdi_himax83112b")) {
			tp_used_index = himax_83112b;

		} else if (strstr(saved_command_line, "jdi_himax83112a")) {
			tp_used_index = himax_83112a;
			g_tp_dev_vendor = TP_JDI;

		} else if (strstr(saved_command_line, "tianma_himax83112a")) {
			tp_used_index = himax_83112a;
			g_tp_dev_vendor = TP_TIANMA;

		} else if (strstr(saved_command_line, "dsjm_himax83112")) {
			tp_used_index = himax_83112a;
			g_tp_dev_vendor = TP_DSJM;

		} else if (strstr(saved_command_line, "dsjm_jdi_td4330")) {
			tp_used_index = td4330;
			g_tp_dev_vendor = TP_DSJM;

		} else if (strstr(saved_command_line, "dpt_jdi_td4330")) {
			tp_used_index = td4330;
			g_tp_dev_vendor = TP_HIMAX_DPT;

		} else if (strstr(saved_command_line, "tianma_td4330")) {
			tp_used_index = td4330;
			g_tp_dev_vendor = TP_TIANMA;

		} else if (strstr(saved_command_line, "tm_nt36670a")) {
			pr_info("chip_name is nt36672a_nf, vendor is tianma\n");
		}

		pr_err("[TP] g_tp_dev_vendor: %s\n", tp_dev_names[g_tp_dev_vendor].name);
		break;

	default:
		pr_info("other project, no need process here!\n");
		break;
	}

	pr_info("[TP]ic:%d, vendor:%d\n", tp_used_index, g_tp_dev_vendor);
	return true;
}

int tp_util_get_vendor(struct hw_resource *hw_res,
		       struct panel_info *panel_data)
{
	char *vendor;
	int prj_id = 0;
	prj_id = get_project();

	panel_data->test_limit_name = kzalloc(MAX_LIMIT_DATA_LENGTH, GFP_KERNEL);

	if (panel_data->test_limit_name == NULL) {
		pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
	}

	panel_data->extra = kzalloc(MAX_LIMIT_DATA_LENGTH, GFP_KERNEL);

	if (panel_data->extra == NULL) {
		pr_err("[TP]panel_data.extra kzalloc error\n");
	}

	if (is_tp_type_got_in_match) {
		panel_data->tp_type = g_tp_dev_vendor;
	}

	if (panel_data->tp_type == TP_UNKNOWN) {
		pr_err("[TP]%s type is unknown\n", __func__);
		return 0;
	}

	if (prj_id == 19165 || prj_id == 19166) {
	    memcpy(panel_data->manufacture_info.version, "0xBD3100000", 11);
	}
	if (prj_id == 20075 || prj_id == 20076) {
	    memcpy(panel_data->manufacture_info.version, "0xRA5230000", 11);
	}

	vendor = GET_TP_DEV_NAME(panel_data->tp_type);

	if (prj_id == 20131 || prj_id == 20133 || prj_id == 20255 || prj_id == 20257) {
		memcpy(panel_data->manufacture_info.version, "0xbd3520000", 11);
	}
	if (prj_id == 20615 || prj_id == 20662 || prj_id == 20619 || prj_id == 21609|| prj_id == 21651) {
		memcpy(panel_data->manufacture_info.version, "focalt_", 7);
	}
	if (strstr(saved_command_line, "oppo20131_tianma_nt37701_1080p_dsi_cmd")) {
		hw_res->TX_NUM = 18;
		hw_res->RX_NUM = 40;
		vendor = "TIANMA";
	}


	strcpy(panel_data->manufacture_info.manufacture, vendor);

	switch (prj_id) {
	case 20615:
	case 20662:
	case 20619:
	case 21609:
	case 21651:
		pr_info("[TP] enter case OPPO_20615\n");
		snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
				"tp/20615/FW_%s_%s.img",
				"FT3518", vendor);

		if (panel_data->test_limit_name) {
			snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
				"tp/20615/LIMIT_%s_%s.img",
				"FT3518", vendor);
		}

		if (panel_data->extra) {
			snprintf(panel_data->extra, MAX_LIMIT_DATA_LENGTH,
				"tp/20615/BOOT_FW_%s_%s.ihex",
				"FT3518", vendor);
		}

		panel_data->manufacture_info.fw_path = panel_data->fw_name;
	case 19131:
	case 19132:
	case 19133:
	case 19420:
	case 19421:
#ifdef CONFIG_MACH_MT6873
		pr_info("[TP] enter case OPPO_19131\n");

		if (tp_used_index == nt36672c) {
			snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
				 "tp/19131/FW_%s_%s.img",
				 "NF_NT36672C", vendor);

			if (panel_data->test_limit_name) {
				snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
					 "tp/19131/LIMIT_%s_%s.img",
					 "NF_NT36672C", vendor);
			}

			if (panel_data->extra) {
				snprintf(panel_data->extra, MAX_LIMIT_DATA_LENGTH,
					 "tp/19131/BOOT_FW_%s_%s.ihex",
					 "NF_NT36672C", vendor);
			}

			panel_data->manufacture_info.fw_path = panel_data->fw_name;

			if ((tp_used_index == nt36672c) && (g_tp_dev_vendor == TP_JDI)) {
				pr_info("[TP]: firmware_headfile = FW_19131_NF_NT36672C_JDI_fae_jdi\n");
				memcpy(panel_data->manufacture_info.version, "0xDD300JN200", 12);
				/*panel_data->firmware_headfile.firmware_data = FW_19131_NF_NT36672C_JDI;
				panel_data->firmware_headfile.firmware_size = sizeof(FW_19131_NF_NT36672C_JDI);*/
				panel_data->firmware_headfile.firmware_data = FW_19131_NF_NT36672C_JDI_fae_jdi;
                panel_data->firmware_headfile.firmware_size = sizeof(FW_19131_NF_NT36672C_JDI_fae_jdi);
			}

			if ((tp_used_index == nt36672c) && (g_tp_dev_vendor == TP_TIANMA)) {
				pr_info("[TP]: firmware_headfile = FW_19131_NF_NT36672C_TIANMA_fae_tianma\n");
				memcpy(panel_data->manufacture_info.version, "0xDD300TN000", 12);
				/*panel_data->firmware_headfile.firmware_data = FW_19131_NF_NT36672C_TIANMA_realme;
				panel_data->firmware_headfile.firmware_size = sizeof(FW_19131_NF_NT36672C_TIANMA_realme);*/
                panel_data->firmware_headfile.firmware_data = FW_19131_NF_NT36672C_TIANMA_fae_tianma;
                panel_data->firmware_headfile.firmware_size = sizeof(FW_19131_NF_NT36672C_TIANMA_fae_tianma);
			}
		}

		if (tp_used_index == himax_83112f) {
			snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
				 "tp/19131/FW_%s_%s.img",
				 "NF_HX83112F", vendor);

			if (panel_data->test_limit_name) {
				snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
					 "tp/19131/LIMIT_%s_%s.img",
					 "NF_HX83112F", vendor);
			}

			if (panel_data->extra) {
				snprintf(panel_data->extra, MAX_LIMIT_DATA_LENGTH,
					 "tp/19131/BOOT_FW_%s_%s.ihex",
					 "NF_HX83112F", vendor);
			}

			panel_data->manufacture_info.fw_path = panel_data->fw_name;

			if (tp_used_index == himax_83112f) {
				pr_info("[TP]: firmware_headfile = FW_19131_NF_HX83112F_JDI\n");
				memcpy(panel_data->manufacture_info.version, "0xDD300JH000", 12);
				panel_data->firmware_headfile.firmware_data = FW_19131_NF_HX83112F_JDI;
				panel_data->firmware_headfile.firmware_size = sizeof(FW_19131_NF_HX83112F_JDI);
			}
		}

#endif
		break;

	case 20051:
		pr_info("[TP] enter case OPPO_20051\n");

		if (tp_used_index == nt36672c) {
			snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
				 "tp/20051/FW_%s_%s.img",
				 "NF_NT36672C", "JDI");

			if (panel_data->test_limit_name) {
				snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
					 "tp/20051/LIMIT_%s_%s.img",
					 "NF_NT36672C", "JDI");
			}

			if (panel_data->extra) {
				snprintf(panel_data->extra, MAX_LIMIT_DATA_LENGTH,
					 "tp/20051/BOOT_FW_%s_%s.ihex",
					 "NF_NT36672C", "JDI");
			}

			panel_data->manufacture_info.fw_path = panel_data->fw_name;

			if ((tp_used_index == nt36672c) && (g_tp_dev_vendor == TP_JDI)) {
				pr_info("[TP]: firmware_headfile = FW_20051_NF_NT36672C_JDI_fae_jdi\n");
				memcpy(panel_data->manufacture_info.version, "0xBD358JN200", 12);
				panel_data->firmware_headfile.firmware_data = FW_20051_NF_NT36672C_JDI_fae_jdi;
                panel_data->firmware_headfile.firmware_size = sizeof(FW_20051_NF_NT36672C_JDI_fae_jdi);
			}
		}

		break;

	case 20001:
	case 20002:
	case 20003:
	case 20200:
		pr_info("[TP] enter case OPPO_20001\n");

		if (tp_used_index == nt36672c) {
			snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
				 "tp/20001/FW_%s_%s.img",
				 "NF_NT36672C", vendor);

			if (panel_data->test_limit_name) {
				snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
					 "tp/20001/LIMIT_%s_%s.img",
					 "NF_NT36672C", vendor);
			}

			if (panel_data->extra) {
				snprintf(panel_data->extra, MAX_LIMIT_DATA_LENGTH,
					 "tp/20001/BOOT_FW_%s_%s.ihex",
					 "NF_NT36672C", vendor);
			}

			panel_data->manufacture_info.fw_path = panel_data->fw_name;

			if (tp_used_index == nt36672c) {
				pr_info("[TP]: firmware_headfile = FW_20001_NF_NT36672C_JDI_fae_jdi\n");
				memcpy(panel_data->manufacture_info.version, "0xFA219DN", 9);
				panel_data->firmware_headfile.firmware_data = FW_20001_NF_NT36672C_JDI;
				panel_data->firmware_headfile.firmware_size = sizeof(FW_20001_NF_NT36672C_JDI);
			}
		}

		if (tp_used_index == himax_83112f) {
			snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
				 "tp/20001/FW_%s_%s.img",
				 "NF_HX83112F", vendor);

			if (panel_data->test_limit_name) {
				snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
					 "tp/20001/LIMIT_%s_%s.img",
					 "NF_HX83112F", vendor);
			}

			if (panel_data->extra) {
				snprintf(panel_data->extra, MAX_LIMIT_DATA_LENGTH,
					 "tp/20001/BOOT_FW_%s_%s.ihex",
					 "NF_HX83112F", vendor);
			}

			panel_data->manufacture_info.fw_path = panel_data->fw_name;

			if (tp_used_index == himax_83112f) {
				pr_info("[TP]: firmware_headfile = FW_20001_NF_HX83112F_TIANMA\n");
				memcpy(panel_data->manufacture_info.version, "0xFA219TH", 9);
				panel_data->firmware_headfile.firmware_data = FW_20001_NF_HX83112F_TIANMA;
                panel_data->firmware_headfile.firmware_size = sizeof(FW_20001_NF_HX83112F_TIANMA);
			}
		}

		break;

	case 132780:
		pr_info("[TP] enter case 132780\n");
        snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH, "tp/206AC/FW_NF_NT36525B_BOE.bin", panel_data->chip_name, 2);
        panel_data->manufacture_info.fw_path = panel_data->fw_name;
        panel_data->firmware_headfile.firmware_data = FW_206AC_NF_NT36525B_BOE;
        panel_data->firmware_headfile.firmware_size = sizeof(FW_206AC_NF_NT36525B_BOE);
		break;

	case 20682:
		pr_info("[TP] enter case 20682\n");

		if (g_tp_dev_vendor == TP_TIANMA) {
            snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH, "tp/%d/FW_NT36672C_NF_TIANMA.bin", g_tp_prj_id, panel_data->chip_name, vendor);
			panel_data->manufacture_info.fw_path = panel_data->fw_name;
			memcpy(panel_data->manufacture_info.version, "A539TM", 6);
			panel_data->firmware_headfile.firmware_data = FW_20682_NT36672C_TIANMA;
			panel_data->firmware_headfile.firmware_size = sizeof(FW_20682_NT36672C_TIANMA);
			pr_info("[TP] 20682 tp vendor tianma\n");

		} else if (g_tp_dev_vendor == TP_JDI) {
            snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH, "tp/%d/FW_NT36672C_NF_JDI.bin", g_tp_prj_id, panel_data->chip_name, vendor);
			panel_data->manufacture_info.fw_path = panel_data->fw_name;
			memcpy(panel_data->manufacture_info.version, "A539JDI", 7);
			panel_data->firmware_headfile.firmware_data = FW_20682_NT36672C_JDI;
			panel_data->firmware_headfile.firmware_size = sizeof(FW_20682_NT36672C_JDI);
			pr_info("[TP] 20682 tp vendor jdi\n");

		} else {
			pr_info("[TP] 20682 tp ic not found\n");
		}

		break;

	case 18531:
		pr_info("[TP] project:%d\n", g_tp_prj_id);

		if (tp_used_index == himax_83112a) {
			if (g_tp_dev_vendor == TP_DSJM) {
                snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH, "tp/%d/FW_HX83112A_NF_DSJM.img", g_tp_prj_id);
				panel_data->manufacture_info.fw_path = panel_data->fw_name;
				memcpy(panel_data->manufacture_info.version, "0xBD1203", 8);
				panel_data->firmware_headfile.firmware_data = FW_18311_HX83112A_NF_DSJM;
				panel_data->firmware_headfile.firmware_size = sizeof(FW_18311_HX83112A_NF_DSJM);

			} else if (g_tp_dev_vendor == TP_JDI) {
                snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH, "tp/%d/FW_HX83112A_NF_JDI.img", g_tp_prj_id);
				panel_data->manufacture_info.fw_path = panel_data->fw_name;
				memcpy(panel_data->manufacture_info.version, "0xDD0750", 8);
				panel_data->firmware_headfile.firmware_data = FW_18531_HX83112A_NF_JDI;
				panel_data->firmware_headfile.firmware_size = sizeof(FW_18531_HX83112A_NF_JDI);

			} else if (g_tp_dev_vendor == TP_TIANMA) {
                snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH, "tp/%d/FW_HX83112A_NF_TIANMA.img", g_tp_prj_id);
				panel_data->manufacture_info.fw_path = panel_data->fw_name;
				memcpy(panel_data->manufacture_info.version, "0xDD0751", 8);
				panel_data->firmware_headfile.firmware_data = FW_18531_HX83112A_NF_TM;
				panel_data->firmware_headfile.firmware_size = sizeof(FW_18531_HX83112A_NF_TM);
			}

		} else if (tp_used_index == himax_83112b) {
            snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH, "tp/%d/FW_HX83112B_NF_DSJM.img", g_tp_prj_id);
			panel_data->manufacture_info.fw_path = panel_data->fw_name;
			memcpy(panel_data->manufacture_info.version, "0xDD0755", 8);
			panel_data->firmware_headfile.firmware_data = FW_18531_HX83112B_NF_DSJM;
			panel_data->firmware_headfile.firmware_size = sizeof(FW_18531_HX83112B_NF_DSJM);

		} else if (tp_used_index == td4330) {
			if (g_tp_dev_vendor == TP_DSJM) {
                snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH, "tp/%d/FW_TD4330_NF_DSJM.img", g_tp_prj_id);
				panel_data->manufacture_info.fw_path = panel_data->fw_name;
				memcpy(panel_data->manufacture_info.version, "0xDD075E", 8);
				panel_data->firmware_headfile.firmware_data = FW_18531_TD4330_NF_DSJM;
				panel_data->firmware_headfile.firmware_size = sizeof(FW_18531_TD4330_NF_DSJM);

			} else if (g_tp_dev_vendor == TP_HIMAX_DPT) {
                snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH, "tp/%d/FW_TD4330_NF_DPT.img", g_tp_prj_id);
				panel_data->manufacture_info.fw_path = panel_data->fw_name;
				memcpy(panel_data->manufacture_info.version, "0xDD075D", 8);
				panel_data->firmware_headfile.firmware_data = FW_18531_TD4330_NF_DPT;
				panel_data->firmware_headfile.firmware_size = sizeof(FW_18531_TD4330_NF_DPT);

			} else if (g_tp_dev_vendor == TP_TIANMA) {
                snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH, "tp/%d/FW_TD4330_NF_TIANMA.img", g_tp_prj_id);
				panel_data->manufacture_info.fw_path = panel_data->fw_name;
				memcpy(panel_data->manufacture_info.version, "0xDD075A", 8);
				panel_data->firmware_headfile.firmware_data = FW_18531_TD4330_NF_TM;
				panel_data->firmware_headfile.firmware_size = sizeof(FW_18531_TD4330_NF_TM);
			}

		} else {
            snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH, "tp/%d/FW_NT36672A_NF_TIANMA.bin", g_tp_prj_id);
			panel_data->manufacture_info.fw_path = panel_data->fw_name;
			memcpy(panel_data->manufacture_info.version, "0xDD0752", 8);
			panel_data->firmware_headfile.firmware_data = FW_18531_NT36672A_NF_TM;
			panel_data->firmware_headfile.firmware_size = sizeof(FW_18531_NT36672A_NF_TM);
		}

		break;

	case 20131:
	case 20133:
	case 20255:
	case 20257:
		snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
			 "tp/%d/FW_%s_%s.%s",
			 g_tp_prj_id, panel_data->chip_name, vendor, !strcmp(vendor,
					 "SAMSUNG") ? "bin" : "img");

		if (panel_data->test_limit_name) {
			snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
				 "tp/%d/LIMIT_%s_%s.img",
				 g_tp_prj_id, panel_data->chip_name, vendor);
		}

		break;

	default:
		snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
			 "tp/%d/FW_%s_%s.img",
			 g_tp_prj_id, panel_data->chip_name, vendor);

		if (panel_data->test_limit_name) {
			snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
				 "tp/%d/LIMIT_%s_%s.img",
				 g_tp_prj_id, panel_data->chip_name, vendor);
		}

		if (panel_data->extra) {
			snprintf(panel_data->extra, MAX_LIMIT_DATA_LENGTH,
				 "tp/%d/BOOT_FW_%s_%s.ihex",
				 prj_id, panel_data->chip_name, vendor);
		}

		panel_data->manufacture_info.fw_path = panel_data->fw_name;
		break;
	}

	pr_info("[TP]vendor:%s fw:%s limit:%s\n",
		vendor,
		panel_data->fw_name,
		panel_data->test_limit_name == NULL ? "NO Limit" : panel_data->test_limit_name);

	return 0;
}

