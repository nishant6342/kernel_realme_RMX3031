# SPDX-License-Identifier: GPL-2.0-only
KBUILD_OPTIONS += CONFIG_NXP_ESE_P73=m

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

M ?= $(shell pwd)
modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) KBUILD_EXTRA_SYMBOLS="$(abspath $(O)/../vendor/oplus/kernel/nfc/sn_nci/nfc/Module.symvers)" $(@)

