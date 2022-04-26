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
#ifndef __IMGSENSOR_HWCFG_20131_CTRL_H__
#define __IMGSENSOR_HWCFG_20131_CTRL_H__
#include "imgsensor_hwcfg_custom.h"


struct IMGSENSOR_SENSOR_LIST *oplus_gimgsensor_sensor_list = NULL;
struct IMGSENSOR_HW_CFG *oplus_imgsensor_custom_config = NULL;
struct IMGSENSOR_HW_POWER_SEQ *oplus_sensor_power_sequence = NULL;
struct CAMERA_DEVICE_INFO gImgEepromInfo;

struct IMGSENSOR_SENSOR_LIST
    gimgsensor_sensor_list_20131[MAX_NUM_OF_SUPPORT_SENSOR] = {
#if defined(OV64B_MIPI_RAW)
{OV64B_SENSOR_ID, SENSOR_DRVNAME_OV64B_MIPI_RAW, OV64B_MIPI_RAW_SensorInit},
#endif
#if defined(IMX616_MIPI_RAW)
{IMX616_SENSOR_ID, SENSOR_DRVNAME_IMX616_MIPI_RAW, IMX616_MIPI_RAW_SensorInit},
#endif
#if defined(IMX319_MIPI_RAW)
{IMX319_SENSOR_ID, SENSOR_DRVNAME_IMX319_MIPI_RAW, IMX319_MIPI_RAW_SensorInit},
#endif
#if defined(OV02B10_MIPI_RAW)
{OV02B10_SENSOR_ID, SENSOR_DRVNAME_OV02B10_MIPI_RAW, OV02B10_MIPI_RAW_SensorInit},
#endif
#if defined(GC02M1B_MIPI_MONO)
{GC02M1B_SENSOR_ID, SENSOR_DRVNAME_GC02M1B_MIPI_MONO, GC02M1B_MIPI_MONO_SensorInit},
#endif

    /*  ADD sensor driver before this line */
    {0, {0}, NULL}, /* end of list */
};

struct IMGSENSOR_HW_CFG imgsensor_custom_config_20131[] = {
    {
        IMGSENSOR_SENSOR_IDX_MAIN,
        IMGSENSOR_I2C_DEV_0,
        {
            {IMGSENSOR_HW_PIN_MCLK,  IMGSENSOR_HW_ID_MCLK},
            {IMGSENSOR_HW_PIN_AVDD,  IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DOVDD, IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DVDD,  IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_PDN,   IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_RST,   IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_NONE,  IMGSENSOR_HW_ID_NONE},
        },
    },
    {
        IMGSENSOR_SENSOR_IDX_SUB,
        IMGSENSOR_I2C_DEV_1,
        {
            {IMGSENSOR_HW_PIN_MCLK,  IMGSENSOR_HW_ID_MCLK},
            {IMGSENSOR_HW_PIN_AVDD,  IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DOVDD, IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DVDD,  IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_PDN,   IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_RST,   IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_NONE, IMGSENSOR_HW_ID_NONE},
        },
    },
    {
        IMGSENSOR_SENSOR_IDX_MAIN2,
        IMGSENSOR_I2C_DEV_2,
        {
            {IMGSENSOR_HW_PIN_MCLK,  IMGSENSOR_HW_ID_MCLK},
            {IMGSENSOR_HW_PIN_AVDD,  IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DOVDD, IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DVDD,  IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_RST,   IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_NONE,  IMGSENSOR_HW_ID_NONE},
        },
    },
    {
        IMGSENSOR_SENSOR_IDX_SUB2,
        IMGSENSOR_I2C_DEV_2,
        {
            {IMGSENSOR_HW_PIN_MCLK,  IMGSENSOR_HW_ID_MCLK},
            {IMGSENSOR_HW_PIN_AVDD,  IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DOVDD, IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_RST,   IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_NONE,  IMGSENSOR_HW_ID_NONE},
        },
    },
    {
        IMGSENSOR_SENSOR_IDX_MAIN3,
        IMGSENSOR_I2C_DEV_1,
        {
            {IMGSENSOR_HW_PIN_MCLK,  IMGSENSOR_HW_ID_MCLK},
            {IMGSENSOR_HW_PIN_AVDD,  IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DOVDD, IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_RST,   IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_NONE,  IMGSENSOR_HW_ID_NONE},
        },
    },

    {IMGSENSOR_SENSOR_IDX_NONE}
};

struct IMGSENSOR_HW_POWER_SEQ sensor_power_sequence_20131[] = {
#if defined(OV64B_MIPI_RAW)
    {
        SENSOR_DRVNAME_OV64B_MIPI_RAW,
        {
            {RST, Vol_Low, 1},
            {SensorMCLK, Vol_High, 1},
            {AVDD, Vol_2800, 2},
            {DOVDD, Vol_1800, 1},
            {DVDD, Vol_1100, 5},
            {RST, Vol_High, 5},
        },
    },
#endif
#if defined(IMX616_MIPI_RAW)
    {
        SENSOR_DRVNAME_IMX616_MIPI_RAW,
        {
            {RST, Vol_Low, 1},
            {AVDD, Vol_2900, 0},
            {DVDD, Vol_1100, 0},
            {DOVDD, Vol_1800, 1},
            {SensorMCLK, Vol_High, 1},
            {RST, Vol_High, 2},
        },
    },
#endif
#if defined(IMX319_MIPI_RAW)
    {
        SENSOR_DRVNAME_IMX319_MIPI_RAW,
        {
            {RST, Vol_Low, 1},
            {AVDD, Vol_2800, 0},
            {DVDD, Vol_1100, 0},
            {DOVDD, Vol_1800, 1},
            {SensorMCLK, Vol_High, 1},
            {RST, Vol_High, 2},
        },
    },
#endif
#if defined(OV02B10_MIPI_RAW)
    {
        SENSOR_DRVNAME_OV02B10_MIPI_RAW,
        {
            {RST, Vol_Low, 1},
            {DOVDD, Vol_1800, 1},
            {AVDD, Vol_2800, 0, Vol_Low, 8},
            {SensorMCLK, Vol_High, 5},
            {RST, Vol_High, 10}
        },
    },
#endif
#if defined(GC02M1B_MIPI_MONO)
    {
        SENSOR_DRVNAME_GC02M1B_MIPI_MONO,
        {
            {RST, Vol_Low, 1},
            //{DVDD, Vol_1200, 0},
            {DOVDD, Vol_1800, 1},
            {AVDD, Vol_2800, 0, Vol_Low, 8},
            {SensorMCLK, Vol_High, 1},
            {RST, Vol_High, 2}
        },
    },
#endif
    /* add new sensor before this line */
    {NULL,},
};

#ifdef SENSOR_PLATFORM_5G_H
struct CAMERA_DEVICE_INFO gImgEepromInfo= {
    .i4SensorNum = 5,
    .pCamModuleInfo = {
        {OV64B_SENSOR_ID,   0xA0, {0x00, 0x06}, 0xB0, 1, {0x92,0xFF,0xFF,0x94}, "Cam_r0", "ov64b2q"},
        {IMX616_SENSOR_ID,  0xA8, {0x00, 0x06}, 0xB0, 0, {0xFF,0xFF,0xFF,0xFF}, "Cam_f",  "imx616"},
        {IMX319_SENSOR_ID,  0xA2, {0x00, 0x06}, 0xB0, 0, {0xFF,0xFF,0xFF,0xFF}, "Cam_r1", "imx319"},
        {OV02B10_SENSOR_ID, 0xA4, {0x00, 0x06}, 0xB0, 0, {0xFF,0xFF,0xFF,0xFF}, "Cam_r2", "ov02b10"},
        {GC02M1B_SENSOR_ID, 0xFF, {0xFF, 0xFF}, 0xFF, 0, {0xFF,0xFF,0xFF,0xFF}, "Cam_r3", "gc02m1b"},
    },
    .i4MWDataIdx = IMGSENSOR_SENSOR_IDX_MAIN2,
    .i4MTDataIdx = 0xFF,
    .i4FrontDataIdx = 0xFF,
    .i4NormDataLen = 40,
    .i4MWDataLen = 3102,
    .i4MWStereoAddr = {OV64B_STEREO_START_ADDR, IMX319_STEREO_START_ADDR},
    .i4MTStereoAddr = {0xFFFF, 0xFFFF},
    .i4FrontStereoAddr = {0xFFFF, 0xFFFF},
};
#endif


struct IMGSENSOR_SENSOR_LIST
    gimgsensor_sensor_list_20615[MAX_NUM_OF_SUPPORT_SENSOR] = {
#if defined(IMX682_MIPI_RAW_20615)
{IMX682_SENSOR_ID_20615, SENSOR_DRVNAME_IMX682_MIPI_RAW_20615, IMX682_MIPI_RAW_20615_SensorInit},
#endif
#if defined(IMX471_MIPI_RAW_20615)
{IMX471_SENSOR_ID_20615, SENSOR_DRVNAME_IMX471_MIPI_RAW_20615, IMX471_MIPI_RAW_20615_SensorInit},
#endif
#if defined(HI846_MIPI_RAW_20615)
{HI846_SENSOR_ID_20615, SENSOR_DRVNAME_HI846_MIPI_RAW_20615, HI846_MIPI_RAW_20615_SensorInit},
#endif
#if defined(OV02B10_MIPI_RAW_20615)
{OV02B10_SENSOR_ID_20615, SENSOR_DRVNAME_OV02B10_MIPI_RAW_20615, OV02B10_MIPI_RAW_20615_SensorInit},
#endif


    /*  ADD sensor driver before this line */
    {0, {0}, NULL}, /* end of list */
};

struct IMGSENSOR_SENSOR_LIST
    gimgsensor_sensor_list_20619[MAX_NUM_OF_SUPPORT_SENSOR] = {
#if defined(OV64B_MIPI_RAW_20619)
{OV64B_SENSOR_ID_20619, SENSOR_DRVNAME_OV64B_MIPI_RAW_20619, OV64B_MIPI_RAW_20619_SensorInit},
#endif
#if defined(IMX471_MIPI_RAW_20619)
{IMX471_SENSOR_ID_20619, SENSOR_DRVNAME_IMX471_MIPI_RAW_20619, IMX471_MIPI_RAW_20619_SensorInit},
#endif
#if defined(HI846_MIPI_RAW_20619)
{HI846_SENSOR_ID_20619, SENSOR_DRVNAME_HI846_MIPI_RAW_20619, HI846_MIPI_RAW_20619_SensorInit},
#endif
#if defined(OV02B10_MIPI_RAW_20619)
{OV02B10_SENSOR_ID_20619, SENSOR_DRVNAME_OV02B10_MIPI_RAW_20619, OV02B10_MIPI_RAW_20619_SensorInit},
#endif

    /*  ADD sensor driver before this line */
    {0, {0}, NULL}, /* end of list */
};


struct IMGSENSOR_HW_CFG imgsensor_custom_config_20615[] = {
    {
        IMGSENSOR_SENSOR_IDX_MAIN,
        IMGSENSOR_I2C_DEV_0,
        {
            {IMGSENSOR_HW_PIN_MCLK,  IMGSENSOR_HW_ID_MCLK},
            {IMGSENSOR_HW_PIN_AVDD,  IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_AVDD_1,IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DOVDD, IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DVDD,  IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_RST,   IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_NONE,  IMGSENSOR_HW_ID_NONE},
        },
    },
    {
        IMGSENSOR_SENSOR_IDX_SUB,
        IMGSENSOR_I2C_DEV_1,
        {
            {IMGSENSOR_HW_PIN_MCLK,  IMGSENSOR_HW_ID_MCLK},
            {IMGSENSOR_HW_PIN_AVDD,  IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_DOVDD, IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DVDD,  IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_RST,   IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_NONE, IMGSENSOR_HW_ID_NONE},
        },
    },
    {
        IMGSENSOR_SENSOR_IDX_MAIN2,
        IMGSENSOR_I2C_DEV_2,
        {
            {IMGSENSOR_HW_PIN_MCLK,  IMGSENSOR_HW_ID_MCLK},
            {IMGSENSOR_HW_PIN_AVDD,  IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DOVDD, IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DVDD,  IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_RST,   IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_NONE,  IMGSENSOR_HW_ID_NONE},
        },
    },
    {
        IMGSENSOR_SENSOR_IDX_SUB2,
        IMGSENSOR_I2C_DEV_3,
        {
            {IMGSENSOR_HW_PIN_MCLK,  IMGSENSOR_HW_ID_MCLK},
            {IMGSENSOR_HW_PIN_AVDD,  IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_DOVDD, IMGSENSOR_HW_ID_REGULATOR},
            {IMGSENSOR_HW_PIN_RST,   IMGSENSOR_HW_ID_GPIO},
            {IMGSENSOR_HW_PIN_NONE,  IMGSENSOR_HW_ID_NONE},
        },
    },

    {IMGSENSOR_SENSOR_IDX_NONE}
};

struct IMGSENSOR_HW_POWER_SEQ sensor_power_sequence_20615[] = {
#if defined(IMX682_MIPI_RAW_20615)
    {
        SENSOR_DRVNAME_IMX682_MIPI_RAW_20615,
        {
            {RST, Vol_Low, 1},
            {AVDD_1, Vol_1800, 0},
            {AVDD, Vol_2900, 0},
            {DVDD, Vol_1100, 0},
            {DOVDD, Vol_1800, 2},
            //{AFVDD, Vol_1800, 1},
            {SensorMCLK, Vol_High, 1},
            {RST, Vol_High, 3}
        },
    },
#endif
#if defined(IMX471_MIPI_RAW_20615)
    {
        SENSOR_DRVNAME_IMX471_MIPI_RAW_20615,
        {
            {RST, Vol_Low, 1},
            {AVDD, Vol_2800, 0},
            {DVDD, Vol_1100, 0},
            {DOVDD, Vol_1800, 1},
            {SensorMCLK, Vol_High, 1},
            {RST, Vol_High, 2}
        },
    },
#endif
#if defined(HI846_MIPI_RAW_20615)
      {
          SENSOR_DRVNAME_HI846_MIPI_RAW_20615,
          {
              {RST, Vol_Low, 1},
              {AVDD, Vol_2800, 0},
              {DVDD, Vol_1200, 0},
              {DOVDD, Vol_1800, 1},
              {SensorMCLK, Vol_High, 1},
              {RST, Vol_High, 2}
          },
     },
#endif
#if defined(OV02B10_MIPI_RAW_20615)
    {
        SENSOR_DRVNAME_OV02B10_MIPI_RAW_20615,
        {
            {RST, Vol_Low, 1},
            {DOVDD, Vol_1800, 1},
            {AVDD, Vol_2800, 0, Vol_Low, 8},
            {SensorMCLK, Vol_High, 5},
            {RST, Vol_High, 10}
        },
    },
#endif
    /* add new sensor before this line */
    {NULL,},
};

struct IMGSENSOR_HW_POWER_SEQ sensor_power_sequence_20619[] = {
#if defined(OV64B_MIPI_RAW_20619)
    {
        SENSOR_DRVNAME_OV64B_MIPI_RAW_20619,
        {
            {RST, Vol_Low, 1},
            {SensorMCLK, Vol_High, 1},
            {AVDD, Vol_2800, 2},
            {DOVDD, Vol_1800, 1},
            {DVDD, Vol_1100, 5},
            {RST, Vol_High, 5},
        },
    },
#endif
#if defined(IMX471_MIPI_RAW_20619)
    {
        SENSOR_DRVNAME_IMX471_MIPI_RAW_20619,
        {
            {RST, Vol_Low, 1},
            {AVDD, Vol_2800, 0},
            {DVDD, Vol_1100, 0},
            {DOVDD, Vol_1800, 1},
            {SensorMCLK, Vol_High, 1},
            {RST, Vol_High, 2}
        },
    },
#endif
#if defined(HI846_MIPI_RAW_20619)
    {
        SENSOR_DRVNAME_HI846_MIPI_RAW_20619,
        {
            {RST, Vol_Low, 1},
            {AVDD, Vol_2800, 0},
            {DVDD, Vol_1200, 0},
            {DOVDD, Vol_1800, 1},
            {SensorMCLK, Vol_High, 1},
            {RST, Vol_High, 2}
        },
    },
#endif
#if defined(OV02B10_MIPI_RAW_20619)
    {
        SENSOR_DRVNAME_OV02B10_MIPI_RAW_20619,
        {
            {RST, Vol_Low, 1},
            {DOVDD, Vol_1800, 1},
            {AVDD, Vol_2800, 0, Vol_Low, 8},
            {SensorMCLK, Vol_High, 5},
            {RST, Vol_High, 10}
        },
    },
#endif

    /* add new sensor before this line */
    {NULL,},
};


struct CAMERA_DEVICE_INFO gImgEepromInfo_20615= {
    .i4SensorNum = 4,
    .pCamModuleInfo = {
        {IMX682_SENSOR_ID_20615, 0xA0, {0x00, 0x06}, 0xB0, 1, {0x92,0xFF,0xFF,0x94}, "Cam_r0", "imx682"},
        {IMX471_SENSOR_ID_20615, 0xA8, {0x00, 0x06}, 0xB0, 0, {0xFF,0xFF,0xFF,0xFF}, "Cam_f",  "imx471"},
        {HI846_SENSOR_ID_20615,  0xA2, {0x00, 0x06}, 0xB0, 0, {0xFF,0xFF,0xFF,0xFF}, "Cam_r1", "hi846"},
        {OV02B10_SENSOR_ID_20615,0xA4, {0x00, 0x06}, 0xB0, 0, {0xFF,0xFF,0xFF,0xFF}, "Cam_r2", "ov02b10"},
    },
    .i4MWDataIdx = IMGSENSOR_SENSOR_IDX_MAIN2,
    .i4MTDataIdx = 0xFF,
    .i4FrontDataIdx = 0xFF,
    .i4NormDataLen = 40,
    .i4MWDataLen = 3102,
    .i4MWStereoAddr = {IMX682_STEREO_START_ADDR_20615, HI846_STEREO_START_ADDR_20615},
    .i4MTStereoAddr = {0xFFFF, 0xFFFF},
    .i4FrontStereoAddr = {0xFFFF, 0xFFFF},
};

struct CAMERA_DEVICE_INFO gImgEepromInfo_20619= {
    .i4SensorNum = 4,
    .pCamModuleInfo = {
        {OV64B_SENSOR_ID_20619, 0xA0, {0x00, 0x06}, 0xB0, 1, {0x92,0xFF,0xFF,0x94}, "Cam_r0", "ov64b"},
        {IMX471_SENSOR_ID_20619, 0xA8, {0x00, 0x06}, 0xB0, 0, {0xFF,0xFF,0xFF,0xFF}, "Cam_f",  "imx471"},
        {HI846_SENSOR_ID_20619,  0xA2, {0x00, 0x06}, 0xB0, 0, {0xFF,0xFF,0xFF,0xFF}, "Cam_r1", "hi846"},
        {OV02B10_SENSOR_ID_20619,0xA4, {0x00, 0x06}, 0xB0, 0, {0xFF,0xFF,0xFF,0xFF}, "Cam_r2", "ov02b10"},
    },
    .i4MWDataIdx = IMGSENSOR_SENSOR_IDX_MAIN2,
    .i4MTDataIdx = 0xFF,
    .i4FrontDataIdx = 0xFF,
    .i4NormDataLen = 40,
    .i4MWDataLen = 3102,
    .i4MWStereoAddr = {OV64B_STEREO_START_ADDR, HI846_STEREO_START_ADDR_20615},
    .i4MTStereoAddr = {0xFFFF, 0xFFFF},
    .i4FrontStereoAddr = {0xFFFF, 0xFFFF},
};


void oplus_imgsensor_hwcfg()
{
    if (is_project(20615) || is_project(20662) || is_project(21609)) {
        oplus_gimgsensor_sensor_list = gimgsensor_sensor_list_20615;
        oplus_imgsensor_custom_config = imgsensor_custom_config_20615;
        oplus_sensor_power_sequence = sensor_power_sequence_20615;
        gImgEepromInfo = gImgEepromInfo_20615;
    } else if(is_project(20619)) {
        oplus_gimgsensor_sensor_list = gimgsensor_sensor_list_20619;
        oplus_imgsensor_custom_config = imgsensor_custom_config_20615;
        oplus_sensor_power_sequence = sensor_power_sequence_20619;
        gImgEepromInfo = gImgEepromInfo_20619;
    }
   #ifdef SENSOR_PLATFORM_5G_H
    else {
        oplus_gimgsensor_sensor_list = gimgsensor_sensor_list_20131;
        oplus_imgsensor_custom_config = imgsensor_custom_config_20131;
        oplus_sensor_power_sequence = sensor_power_sequence_20131;
    }
#endif
}


#endif
