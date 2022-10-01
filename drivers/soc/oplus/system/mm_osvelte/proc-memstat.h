// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */
#ifndef _PROC_MEMSTAT_H
#define _PROC_MEMSTAT_H
#include <linux/pagewalk.h>

long proc_memstat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#endif /* _PROC_MEMSTAT_H */
