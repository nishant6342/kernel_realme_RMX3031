/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2021 Oplus. All rights reserved.
 */
#ifndef _SYS_MEMSTAT_H
#define _SYS_MEMSTAT_H

#include <linux/vmstat.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/vmalloc.h>

static inline unsigned long sys_totalram(void)
{
	return totalram_pages();
}

static inline unsigned long sys_freeram(void)
{
	return global_zone_page_state(NR_FREE_PAGES);
}

static inline unsigned long sys_inactive_file(void)
{
	return global_node_page_state(NR_ACTIVE_FILE);
}

static inline unsigned long sys_active_file(void)
{
	return global_node_page_state(NR_INACTIVE_FILE);
}

static inline unsigned long sys_file(void)
{
	return global_node_page_state(NR_FILE_PAGES);
}

static inline unsigned long sys_slab_reclaimable(void)
{
	return global_node_page_state_pages(NR_SLAB_RECLAIMABLE_B);
}

static inline unsigned long sys_slab_unreclaimable(void)
{
	return global_node_page_state_pages(NR_SLAB_UNRECLAIMABLE_B);
}

static inline unsigned long sys_vmalloc(void)
{
	return vmalloc_nr_pages();
}

static inline unsigned long sys_inactive_anon(void)
{
	return global_node_page_state(NR_INACTIVE_ANON);
}

static inline unsigned long sys_active_anon(void)
{
	return global_node_page_state(NR_ACTIVE_ANON);
}

static inline unsigned long sys_anon(void)
{
	return global_node_page_state(NR_ANON_MAPPED);
}

static inline unsigned long sys_page_tables(void)
{
	return global_zone_page_state(NR_PAGETABLE);
}

static inline unsigned long sys_kernel_stack(void)
{
	return global_node_page_state(NR_KERNEL_STACK_KB) >> (PAGE_SHIFT - 10);
}

static inline unsigned long sys_sharedram(void)
{
	return global_node_page_state(NR_SHMEM);
}

static inline unsigned long sys_dmabuf(void)
{
	return 0;
}

static inline unsigned long sys_dmabuf_pool(void)
{
	return 0;
}

static inline unsigned long sys_gpu(void)
{
	return 0;
}
#endif /* _SYS_MEMSTAT_H */
