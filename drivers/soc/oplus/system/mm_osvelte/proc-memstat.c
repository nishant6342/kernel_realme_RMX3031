/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2021 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) "svelte: " fmt
#include "proc-memstat.h"

#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>
#include <linux/cred.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/pagemap.h>
#include <linux/dma-buf.h>
#include <linux/fdtable.h>
#include <linux/thread_info.h>
#include <linux/pagewalk.h>
#include "memstat.h"

static is_ashmem_file(struct file *file)
{
	return false;
}

static int match_file(const void *p, struct file *file, unsigned fd)
{
	struct dma_buf *dmabuf;
	struct ashmem_area *ashmem_data;
	struct proc_ms *ms = (struct proc_ms *)p;

	if (is_dma_buf_file(file)) {
		dmabuf = file->private_data;

		if (dmabuf) {
			ms->dmabuf += dmabuf->size;
		}

		return 0;
	}

#ifdef CONFIG_ASHMEM

	if (is_ashmem_file(file)) {
		ashmem_data = file->private_data;

		if (ashmem_data) {
			ms->ashmem += ashmem_data->size;
		}

		return 0;
	}

#endif /* CONFIG_ASHMEM */

	ms->nr_fds += 1;
	return 0;
}

static void __proc_memstat_fd(struct task_struct *tsk, struct proc_ms *ms)
{
	iterate_fd(tsk->files, 0, match_file, ms);
}


static int __proc_memstat(struct task_struct *tsk, struct proc_ms *ms,
			  u32 flags)
{
	struct mm_struct *mm = NULL;

	mm = get_task_mm(tsk);

	if (!mm) {
		return -EINVAL;
	}

	if (flags & PROC_MS_UID) {
		ms->uid = from_kuid(&init_user_ns, task_uid(tsk));
	}

	if (flags & PROC_MS_PID) {
		ms->pid = tsk->pid;
	}

	if (flags & PROC_MS_OOM_SCORE_ADJ) {
		ms->oom_score_adj = tsk->signal->oom_score_adj;
	}

	if (flags & PROC_MS_32BIT)
		ms->is_32bit = test_ti_thread_flag(task_thread_info(tsk),
						   TIF_32BIT);

	if (flags & PROC_MS_COMM) {
		strncpy(ms->comm, tsk->comm, sizeof(ms->comm));
	}

	if (flags & PROC_MS_VSS) {
		ms->vss = mm->total_vm;
	}

	if (flags & PROC_MS_ANON) {
		ms->anon = get_mm_counter(mm, MM_ANONPAGES);
	}

	if (flags & PROC_MS_FILE) {
		ms->file = get_mm_counter(mm, MM_FILEPAGES);
	}

	if (flags & PROC_MS_SHMEM) {
		ms->shmem = get_mm_counter(mm, MM_SHMEMPAGES);
	}

	if (flags & PROC_MS_SWAP) {
		ms->swap = get_mm_counter(mm, MM_SWAPENTS);
	}

	mmput(mm);

	if (flags & PROC_MS_ITERATE_FD) {
		__proc_memstat_fd(tsk, ms);

		/* dma_buf size use byte */
		ms->dmabuf = ms->dmabuf >> PAGE_SHIFT;
		ms->ashmem = ms->ashmem >> PAGE_SHIFT;
	}

	/* add vendor custom  gpu or sth.*/

	return 0;
}

static int proc_pid_memstat(unsigned long arg)
{
	long ret = -EINVAL;
	struct proc_pid_ms ppm;
	struct task_struct *p;
	pid_t pid;
	void __user *argp = (void __user *) arg;

	if (copy_from_user(&ppm, argp, sizeof(ppm))) {
		return -EFAULT;
	}

	pid = ppm.pid;
	/* zeroed data */
	memset(&ppm.ms, 0, sizeof(ppm.ms));

	rcu_read_lock();
	p = find_task_by_vpid(pid);

	if (!p) {
		rcu_read_unlock();
		return -EINVAL;
	}

	get_task_struct(p);

	if ((ppm.flags & PROC_MS_PPID) && pid_alive(p)) {
		ppm.ms.ppid = task_pid_nr(rcu_dereference(p->real_parent));
	}

	rcu_read_unlock();

	ret = __proc_memstat(p, &ppm.ms, ppm.flags);
	put_task_struct(p);

	if (ret) {
		return ret;
	}

	if (copy_to_user(argp, &ppm, sizeof(ppm))) {
		return -EFAULT;
	}

	return 0;
}

static int proc_size_memstat(unsigned int cmd, unsigned long arg)
{
	struct proc_size_ms psm;
	struct task_struct *p = NULL;
	int cnt = 0;
	struct proc_ms *arr_ms;
	int ret;

	void __user *argp = (void __user *) arg;

	if (copy_from_user(&psm, argp, sizeof(psm))) {
		return -EFAULT;
	}

	if (psm.size > PROC_MS_MAX_SIZE) {
		return -EINVAL;
	}


	arr_ms = vzalloc(psm.size * sizeof(struct proc_ms));

	if (!arr_ms) {
		return -ENOMEM;
	}

	rcu_read_lock();
	for_each_process(p) {
		struct proc_ms *ms = arr_ms + cnt;

		if (cnt >= psm.size) {
			break;
		}

		if (p->flags & PF_KTHREAD) {
			continue;
		}

		if (p->pid != p->tgid) {
			continue;
		}

		if (cmd == CMD_PROC_MS_SIZE_UID) {
			/* don't need fetch uid again */
			psm.flags &= ~PROC_MS_UID;
			ms->uid = from_kuid(&init_user_ns, task_uid(p));

			if (ms->uid != psm.uid) {
				continue;
			}
		}

		if ((psm.flags & PROC_MS_PPID) && pid_alive(p)) {
			ms->ppid = task_pid_nr(rcu_dereference(p->real_parent));
		}

		ret = __proc_memstat(p, ms, psm.flags);

		if (likely(!ret)) {
			cnt++;

		} else {
			pr_err("get proc %s mmstat failed. ret = %d\n", p->comm, ret);
		}
	}
	rcu_read_unlock();

	if (!cnt) {
		return 0;
	}

	psm.size = cnt;

	if (copy_to_user(argp, &psm, sizeof(psm))) {
		return -EFAULT;
	}

	if (copy_to_user(argp + sizeof(psm), arr_ms, cnt * sizeof(struct proc_ms))) {
		return -EFAULT;
	}

	return 0;
}

long proc_memstat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -EINVAL;

	if (cmd < CMD_PROC_MS_MIN || cmd > CMD_PROC_MS_MAX) {
		pr_err("cmd invalid.\n");
		return CMD_PROC_MS_INVALID;
	}

	switch (cmd) {
	case CMD_PROC_MS_PID:
		ret = proc_pid_memstat(arg);
		break;

	case CMD_PROC_MS_SIZE:
		ret = proc_size_memstat(cmd, arg);
		break;

	case CMD_PROC_MS_SIZE_UID:
		ret = proc_size_memstat(cmd, arg);
		break;
	}

	return ret;
}
MODULE_LICENSE("GPL");
