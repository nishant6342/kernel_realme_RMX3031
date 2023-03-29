#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

obj-y += driver/
obj-y += aw87339/
obj-y += tfa98xx/
ifeq ($(CONFIG_SND_SOC_SIA81XX_V1_2_0),y)
obj-$(CONFIG_SIA_PA_ALGO) += sia81xx_1.2.0/
else
obj-$(CONFIG_SIA_PA_ALGO) += sia81xx/
obj-$(CONFIG_SIA_PA_ALGO_MT6833) += sia81xx/
endif
obj-$(CONFIG_SND_SOC_CODEC_AW882XX) += aw882xx_v1.10.0/
obj-$(CONFIG_SND_SOC_AW87XXX) += awinic/
obj-$(CONFIG_SND_SOC_OPLUS_PA_MANAGER) += oplus_speaker_manager/oplus_speaker_manager.o

obj-$(CONFIG_SND_SOC_AW87XXX_2_X_0) += awinic_2.x.0/

obj-$(CONFIG_SND_SOC_FSA) += fsa44xx/
obj-$(CONFIG_SND_SOC_TFA_HAPTIC) += tfa_haptic/
