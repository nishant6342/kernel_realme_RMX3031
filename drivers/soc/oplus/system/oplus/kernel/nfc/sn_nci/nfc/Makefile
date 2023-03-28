# SPDX-License-Identifier: GPL-2.0-only
KBUILD_OPTIONS += CONFIG_NXP_NFC_I2C=m

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

M ?= $(shell pwd)
modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) $(@)