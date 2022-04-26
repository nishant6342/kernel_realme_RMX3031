/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include "imgsensor_hwcfg_custom.h"
struct IMGSENSOR_SENSOR_LIST *Oplusimgsensor_Sensorlist(void)
{
    struct IMGSENSOR_SENSOR_LIST *pOplusImglist = gimgsensor_sensor_list;
    pr_debug("Oplusimgsensor_Sensorlist enter:\n");
    pOplusImglist = oplus_gimgsensor_sensor_list;
    pr_info("oplus_gimgsensor_sensor_list Selected\n");
    return pOplusImglist;
}

struct IMGSENSOR_HW_CFG *Oplusimgsensor_Custom_Config(void)
{
    struct IMGSENSOR_HW_CFG *pOplusImgHWCfg = imgsensor_custom_config;
    pOplusImgHWCfg = oplus_imgsensor_custom_config;
    pr_info("oplus_imgsensor_custom_config Selected\n");
    return pOplusImgHWCfg;
}

enum IMGSENSOR_RETURN Oplusimgsensor_i2c_init(
        struct IMGSENSOR_SENSOR_INST *psensor_inst)
{
    enum IMGSENSOR_RETURN ret = IMGSENSOR_RETURN_SUCCESS;
    struct IMGSENSOR_HW_CFG *pOplusImgHWCfg = imgsensor_custom_config;

    if (psensor_inst == NULL) {
        pr_info("Oplusimgsensor_i2c_init psensor_inst is NULL\n");
        return IMGSENSOR_RETURN_ERROR;
    }

    pOplusImgHWCfg = Oplusimgsensor_Custom_Config();
    ret = imgsensor_i2c_init(&psensor_inst->i2c_cfg,
                             pOplusImgHWCfg[psensor_inst->sensor_idx].i2c_dev);
    pr_debug("[%s] sensor_idx:%d name:%s ret: %d\n",
        __func__,
        psensor_inst->sensor_idx,
        psensor_inst->psensor_list->name,
        ret);

    return ret;
}

struct IMGSENSOR_HW_POWER_SEQ *Oplusimgsensor_matchhwcfg_power(
        enum  IMGSENSOR_POWER_ACTION_INDEX  pwr_actidx)
{
    struct IMGSENSOR_HW_POWER_SEQ *ppwr_seq = NULL;
    pr_debug("[%s] pwr_actidx:%d\n", __func__, pwr_actidx);
    if ((pwr_actidx != IMGSENSOR_POWER_MATCHMIPI_HWCFG_INDEX)
        && (pwr_actidx != IMGSENSOR_POWER_MATCHSENSOR_HWCFG_INDEX)) {
        return NULL;
    }

    if (pwr_actidx == IMGSENSOR_POWER_MATCHSENSOR_HWCFG_INDEX) {
        ppwr_seq = oplus_sensor_power_sequence;
        pr_info("[%s] match oplus_sensor_power_sequence\n", __func__);
#ifdef SENSOR_PLATFORM_5G_B
    } else if (pwr_actidx == IMGSENSOR_POWER_MATCHMIPI_HWCFG_INDEX){
            ppwr_seq = oplus_platform_power_sequence;
            pr_info("[%s] enter for 20075 IMGSENSOR_POWER_MATCHMIPI_HWCFG_INDEX \n", __func__);
            return NULL;
#endif

#ifdef SENSOR_PLATFORM_4G_20682
    } else if (pwr_actidx == IMGSENSOR_POWER_MATCHMIPI_HWCFG_INDEX){
            if (is_project(20682) || is_project(19661)) {
                ppwr_seq = oplus_platform_power_sequence;
                pr_info("[%s] match 20682 19661 IMGSENSOR_POWER_MATCHMIPI_HWCFG_INDEX \n", __func__);
            }
#endif

#ifdef SENSOR_PLATFORM_5G_A
    } else if (pwr_actidx == IMGSENSOR_POWER_MATCHMIPI_HWCFG_INDEX){
            ppwr_seq = oplus_platform_power_sequence;
            pr_info("[%s] match 19131 19420 IMGSENSOR_POWER_MATCHMIPI_HWCFG_INDEX \n", __func__);
#endif

#ifdef SENSOR_PLATFORM_MT6771
    } else if (pwr_actidx == IMGSENSOR_POWER_MATCHMIPI_HWCFG_INDEX){
            if (is_project(19531) || is_project(19151) || is_project(19350)) {
                ppwr_seq = oplus_platform_power_sequence;
                pr_info("[%s] MT6771 19531 19151 19350IMGSENSOR_POWER_MATCHMIPI_HWCFG_INDEX \n", __func__);
            }
#endif

#ifdef SENSOR_PLATFORM_5G_H
    } else if (pwr_actidx == IMGSENSOR_POWER_MATCHMIPI_HWCFG_INDEX){
            if(is_project(19165)){
            pr_info("[%s] enter for 19165 IMGSENSOR_POWER_MATCHMIPI_HWCFG_INDEX \n", __func__);
            return NULL;
            }
#endif
    } else {
        pr_info("[%s] NOT Support MIPISWITCH\n", __func__);
    }

    return ppwr_seq;
}

enum IMGSENSOR_RETURN Oplusimgsensor_ldoenable_power(
        struct IMGSENSOR_HW             *phw,
        enum   IMGSENSOR_SENSOR_IDX      sensor_idx,
        enum   IMGSENSOR_HW_POWER_STATUS pwr_status)
{
    struct IMGSENSOR_HW_DEVICE *pdev = phw->pdev[IMGSENSOR_HW_ID_GPIO];
    pr_debug("[%s] sensor_idx:%d pdev->set is ERROR\n", __func__, sensor_idx );
    if ((pwr_status == IMGSENSOR_HW_POWER_STATUS_ON)
        && (is_project(20131) || is_project(20133)
              || is_project(20255) || is_project(20257) || is_project(20615)
              || is_project(20662) || is_project(20619) || is_project(21609))) {
        if (pdev->set != NULL) {
            pr_debug("set GPIO29 to enable fan53870");
            pdev->set(pdev->pinstance, sensor_idx, IMGSENSOR_HW_PIN_FAN53870_ENABLE, Vol_High);
        } else {
            pr_debug("[%s] sensor_idx:%d pdev->set is ERROR\n", __func__, sensor_idx );
        }
    }
    return IMGSENSOR_RETURN_SUCCESS;
}

enum IMGSENSOR_RETURN Oplusimgsensor_power_fan53870_20615(
        enum   IMGSENSOR_SENSOR_IDX      sensor_idx,
        enum   IMGSENSOR_HW_PIN          pin,
        enum   IMGSENSOR_HW_POWER_STATUS pwr_status)
{
    int PCB_Version;
    int fan53870_avdd_20615[4][2] = {{7,2900},{0,0},{5,2800},{5,2800}};
    int fan53870_avdd1_20615[2] = {4,1800};
    int fan53870_dvdd_20615[3][2] = {{0,0},{1,1050},{2,1200}};
    int avddIdx = sensor_idx > IMGSENSOR_SENSOR_IDX_SUB2 ?
                    IMGSENSOR_SENSOR_IDX_SUB2 : sensor_idx;
    pr_debug("[%s] is_fan53870_pmic:%d pwr_status:%d avddIdx:%d", __func__, is_fan53870_pmic(), pwr_status, avddIdx);
    pr_debug("%s GetOppoPcbVer:%d",__func__,get_PCB_Version());
    PCB_Version = get_PCB_Version();
    if(((PCB_Version == 4)||(PCB_Version == 2)) && !is_project(20619)){
        fan53870_avdd1_20615[0] = 5;
        fan53870_avdd_20615[2][0] = 4;
        fan53870_avdd_20615[3][0] = 4;
    }
    if (is_project(20619)) {
        fan53870_avdd_20615[0][1] = 2800;
    }
    if (pwr_status == IMGSENSOR_HW_POWER_STATUS_ON) {
        if ((pin == IMGSENSOR_HW_PIN_AVDD) &&  (avddIdx != IMGSENSOR_SENSOR_IDX_SUB)) {
            fan53870_cam_ldo_set_voltage(fan53870_avdd_20615[avddIdx][0], fan53870_avdd_20615[avddIdx][1]);
        } else if (pin == IMGSENSOR_HW_PIN_AVDD_1){
            fan53870_cam_ldo_set_voltage(fan53870_avdd1_20615[0],fan53870_avdd1_20615[1]);
        } else if (pin == IMGSENSOR_HW_PIN_DVDD && ((avddIdx == IMGSENSOR_SENSOR_IDX_MAIN2) || (avddIdx == IMGSENSOR_SENSOR_IDX_SUB)) ){
            fan53870_cam_ldo_set_voltage(fan53870_dvdd_20615[avddIdx][0],fan53870_dvdd_20615[avddIdx][1]);
        } else {
            return IMGSENSOR_RETURN_ERROR;
        }
    } else {
        if ((pin == IMGSENSOR_HW_PIN_AVDD) &&  (avddIdx != IMGSENSOR_SENSOR_IDX_SUB)) {
            fan53870_cam_ldo_disable(fan53870_avdd_20615[avddIdx][0]);
        }else if(pin == IMGSENSOR_HW_PIN_AVDD_1){
            fan53870_cam_ldo_disable(fan53870_avdd1_20615[0]);
        }else if (pin == IMGSENSOR_HW_PIN_DVDD && ((avddIdx == IMGSENSOR_SENSOR_IDX_MAIN2) || (avddIdx == IMGSENSOR_SENSOR_IDX_SUB)) ){
            fan53870_cam_ldo_disable(fan53870_dvdd_20615[avddIdx][0]);
        } else {
            return IMGSENSOR_RETURN_ERROR;
        }
    }
    return IMGSENSOR_RETURN_SUCCESS;

}


enum IMGSENSOR_RETURN Oplusimgsensor_ldo_powerset(
        enum   IMGSENSOR_SENSOR_IDX      sensor_idx,
        enum   IMGSENSOR_HW_PIN          pin,
        enum   IMGSENSOR_HW_POWER_STATUS pwr_status)
{
    int fan53870_avdd[3][2] = {{3, 2800}, {4, 2900}, {6, 2800}};
    int avddIdx = sensor_idx > IMGSENSOR_SENSOR_IDX_SUB ?
                    IMGSENSOR_SENSOR_IDX_MAIN2 : sensor_idx;

    pr_debug("[%s] is_fan53870_pmic:%d pwr_status:%d avddIdx:%d",
                    __func__, is_fan53870_pmic(), pwr_status, avddIdx);
#ifdef SENSOR_PLATFORM_5G_B
    if (is_project(20075) || is_project(20076))
        return IMGSENSOR_RETURN_ERROR;
#endif

    if(is_project(20615) || is_project(20662) || is_project(20619) || is_project(21609))
        return Oplusimgsensor_power_fan53870_20615(sensor_idx,pin,pwr_status);

    if (pwr_status == IMGSENSOR_HW_POWER_STATUS_ON) {
        if (pin == IMGSENSOR_HW_PIN_AVDD) {
            fan53870_cam_ldo_set_voltage(
                fan53870_avdd[avddIdx][0], fan53870_avdd[avddIdx][1]);
        } else if (pin == IMGSENSOR_HW_PIN_DVDD
                && sensor_idx == IMGSENSOR_SENSOR_IDX_MAIN2){
            fan53870_cam_ldo_set_voltage(1, 1050);
        } else {
            return IMGSENSOR_RETURN_ERROR;
        }
    } else {
        if (pin == IMGSENSOR_HW_PIN_AVDD) {
            fan53870_cam_ldo_disable(fan53870_avdd[avddIdx][0]);
        } else if (pin == IMGSENSOR_HW_PIN_DVDD
                && sensor_idx == IMGSENSOR_SENSOR_IDX_MAIN2){
            fan53870_cam_ldo_disable(1);
        } else {
            return IMGSENSOR_RETURN_ERROR;
        }
    }

    return IMGSENSOR_RETURN_SUCCESS;
}

void Oplusimgsensor_Registdeviceinfo(char *name, char *version, kal_uint8 module_id)
{
    char *manufacture;
    if (name == NULL || version == NULL)
    {
        pr_info("name or version is NULL");
        return;
    }
    switch (module_id)
    {
        case IMGSENSOR_MODULE_ID_SUNNY:  /* Sunny */
            manufacture = DEVICE_MANUFACUTRE_SUNNY;
            break;
        case IMGSENSOR_MODULE_ID_TRULY:  /* Truly */
            manufacture = DEVICE_MANUFACUTRE_TRULY;
            break;
        case IMGSENSOR_MODULE_ID_SEMCO:  /* Semco */
            manufacture = DEVICE_MANUFACUTRE_SEMCO;
            break;
        case IMGSENSOR_MODULE_ID_LITEON:  /* Lite-ON */
            manufacture = DEVICE_MANUFACUTRE_LITEON;
            break;
        case IMGSENSOR_MODULE_ID_QTECH:  /* Q-Tech */
            manufacture = DEVICE_MANUFACUTRE_QTECH;
            break;
        case IMGSENSOR_MODULE_ID_OFILM:  /* O-Film */
            manufacture = DEVICE_MANUFACUTRE_OFILM;
            break;
        case IMGSENSOR_MODULE_ID_SHINE:  /* Shine */
            manufacture = DEVICE_MANUFACUTRE_SHINE;
            break;
        default:
            manufacture = DEVICE_MANUFACUTRE_NA;
    }
    register_device_proc(name, version, manufacture);
}

void Oplusimgsensor_powerstate_notify(bool val)
{
    if (is_project(20131) || is_project(20133)
          || is_project(20255) || is_project(20257)
          || is_project(20615) || is_project(20662)
          || is_project(20619) || is_project(21609)) {
        pr_info("[%s] val:%d", __func__, val);
        oplus_chg_set_camera_status(val);
        oplus_chg_set_camera_on(val);
    }
}
