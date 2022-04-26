/************************************************************************************
** File: -
** VENDOR_EDIT
** Copyright (C), 2020-2025, OPPO Mobile Comm Corp., Ltd
**
** Description:
**     add audio extend driver
** Version: 1.0
** --------------------------- Revision History: --------------------------------
**               <author>                                <date>          <desc>
** Jianfeng.Qiu@MULTIMEDIA.AUDIODRIVER                 2020/07/13     Add this file
**
************************************************************************************/

#ifndef __AUDIO_EXTEND_DRV_H__
#define __AUDIO_EXTEND_DRV_H__
extern void extend_codec_i2s_be_dailinks(struct snd_soc_dai_link *dailink, size_t size);
extern bool extend_codec_i2s_compare(struct snd_soc_dai_link *dailink, int dailink_num);
#endif
