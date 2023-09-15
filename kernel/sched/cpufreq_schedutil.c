/*
 * CPUFreq governor based on scheduler-provided CPU utilization data.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <trace/events/sched.h>

#include "sched.h"

#include <linux/sched/cpufreq.h>
#include <trace/events/power.h>
#include "cpufreq_schedutil.h"
#include "../../drivers/misc/mediatek/base/power/include/mtk_upower.h"

#if defined(OPLUS_FEATURE_TASK_CPUSTATS) && defined(CONFIG_OPLUS_SCHED)
#include <linux/task_sched_info.h>
#endif /* defined(OPLUS_FEATURE_TASK_CPUSTATS) && defined(CONFIG_OPLUS_SCHED) */
#if defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_SCHED_WALT)
extern int sysctl_slide_boost_enabled;
extern int sysctl_sched_assist_enabled;
extern u64 ux_task_load[];
#endif /* OPLUS_FEATURE_SCHED_ASSIST */

void (*cpufreq_notifier_fp)(int cluster_id, unsigned long freq);
EXPORT_SYMBOL(cpufreq_notifier_fp);

#if defined(OPLUS_FEATURE_SCHEDUTIL_USE_TL) && defined(CONFIG_SCHEDUTIL_USE_TL)
/* Target load.  Lower values result in higher CPU speeds. */
#define DEFAULT_TARGET_LOAD 80
static unsigned int default_target_loads[] = {DEFAULT_TARGET_LOAD};
#endif

struct sugov_tunables {
	struct gov_attr_set	attr_set;
	unsigned int		up_rate_limit_us;
	unsigned int		down_rate_limit_us;
	#if defined(OPLUS_FEATURE_SCHEDUTIL_USE_TL) && defined(CONFIG_SCHEDUTIL_USE_TL)
	spinlock_t		target_loads_lock;
	unsigned int		*target_loads;
	unsigned int 		*util_loads;
	int			ntarget_loads;
#endif
};

struct sugov_policy {
	struct cpufreq_policy	*policy;

	struct sugov_tunables	*tunables;
	struct list_head	tunables_hook;

	raw_spinlock_t		update_lock;	/* For shared policies */
	u64			last_freq_update_time;
	s64			min_rate_limit_ns;
	s64			up_rate_delay_ns;
	s64			down_rate_delay_ns;
	unsigned int		next_freq;
	unsigned int		cached_raw_freq;
	unsigned int		prev_cached_raw_freq;

#if defined(OPLUS_FEATURE_SCHEDUTIL_USE_TL) && defined(CONFIG_SCHEDUTIL_USE_TL)
	unsigned int len;
#endif
	/* The next fields are only needed if fast switch cannot be used: */
	struct			irq_work irq_work;
	struct			kthread_work work;
	struct			mutex work_lock;
	struct			kthread_worker worker;
	struct task_struct	*thread;
	bool			work_in_progress;

	bool			limits_changed;
	bool			need_freq_update;
#if defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_SCHED_WALT)
	unsigned int flags;
#endif
};

struct sugov_cpu {
	struct update_util_data	update_util;
	struct sugov_policy	*sg_policy;
	unsigned int		cpu;

	bool			iowait_boost_pending;
	unsigned int		iowait_boost;
	u64			last_update;

	unsigned long		bw_dl;
	unsigned long		min;
	unsigned long		max;

};

static DEFINE_PER_CPU(struct sugov_cpu, sugov_cpu);

/************************ Governor internals ***********************/

static bool sugov_should_update_freq(struct sugov_policy *sg_policy, u64 time)
{
	s64 delta_ns;

	/*
	 * Since cpufreq_update_util() is called with rq->lock held for
	 * the @target_cpu, our per-CPU data is fully serialized.
	 *
	 * However, drivers cannot in general deal with cross-CPU
	 * requests, so while get_next_freq() will work, our
	 * sugov_update_commit() call may not for the fast switching platforms.
	 *
	 * Hence stop here for remote requests if they aren't supported
	 * by the hardware, as calculating the frequency is pointless if
	 * we cannot in fact act on it.
	 *
	 * This is needed on the slow switching platforms too to prevent CPUs
	 * going offline from leaving stale IRQ work items behind.
	 */
	if (!cpufreq_this_cpu_can_update(sg_policy->policy))
		return false;

	if (unlikely(sg_policy->limits_changed)) {
		sg_policy->limits_changed = false;
		sg_policy->need_freq_update = true;
		return true;
	}

	/* No need to recalculate next freq for min_rate_limit_us
	 * at least. However we might still decide to further rate
	 * limit once frequency change direction is decided, according
	 * to the separate rate limits.
	 */
#if defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_SCHED_WALT)
	if (sg_policy->flags & SCHED_CPUFREQ_BOOST)
		return true;
#endif /* OPLUS_FEATURE_SCHED_ASSIST */
	delta_ns = time - sg_policy->last_freq_update_time;
	return delta_ns >= sg_policy->min_rate_limit_ns;
}

static bool sugov_up_down_rate_limit(struct sugov_policy *sg_policy, u64 time,
				     unsigned int next_freq)
{
	s64 delta_ns;

	delta_ns = time - sg_policy->last_freq_update_time;
#if defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_SCHED_WALT)
	if (sg_policy->flags & SCHED_CPUFREQ_BOOST)
		return false;
#endif /* OPLUS_FEATURE_SCHED_ASSIST */
	if (next_freq > sg_policy->next_freq &&
	    delta_ns < sg_policy->up_rate_delay_ns)
			return true;

	if (next_freq < sg_policy->next_freq &&
	    delta_ns < sg_policy->down_rate_delay_ns)
			return true;

	return false;
}

static bool sugov_update_next_freq(struct sugov_policy *sg_policy, u64 time,
				   unsigned int next_freq)
{
	if (sg_policy->next_freq == next_freq)
		return false;

	if (sugov_up_down_rate_limit(sg_policy, time, next_freq)) {
		/* Restore cached freq as next_freq is not changed */
		sg_policy->cached_raw_freq = sg_policy->prev_cached_raw_freq;
		return false;
	}

	sg_policy->next_freq = next_freq;
	sg_policy->last_freq_update_time = time;

	return true;
}

#ifdef CONFIG_MTK_TINYSYS_SSPM_SUPPORT
#else
static void sugov_fast_switch(struct sugov_policy *sg_policy, u64 time,
			      unsigned int next_freq)
{
	struct cpufreq_policy *policy = sg_policy->policy;
	int cpu;

	if (!sugov_update_next_freq(sg_policy, time, next_freq))
		return;

	next_freq = cpufreq_driver_fast_switch(policy, next_freq);
	if (!next_freq)
		return;

	policy->cur = next_freq;
#if defined(OPLUS_FEATURE_TASK_CPUSTATS) && defined(CONFIG_OPLUS_SCHED)
	update_freq_info(policy);
#endif /* defined(OPLUS_FEATURE_TASK_CPUSTATS) && defined(CONFIG_OPLUS_SCHED) */

	if (trace_cpu_frequency_enabled()) {
		for_each_cpu(cpu, policy->cpus)
			trace_cpu_frequency(next_freq, cpu);
	}
}

static void sugov_deferred_update(struct sugov_policy *sg_policy, u64 time,
				  unsigned int next_freq)
{
	if (!sugov_update_next_freq(sg_policy, time, next_freq))
		return;

	if (!sg_policy->work_in_progress) {
		sg_policy->work_in_progress = true;
		irq_work_queue(&sg_policy->irq_work);
	}
}
#endif

#if defined(OPLUS_FEATURE_SCHEDUTIL_USE_TL) && defined(CONFIG_SCHEDUTIL_USE_TL)
#ifdef CONFIG_NONLINEAR_FREQ_CTL
static inline unsigned int get_opp_capacity(struct cpufreq_policy *policy,
						int row)
{
	struct upower_tbl *upower_tbl;

	upower_tbl = upower_get_core_tbl(policy->cpu);

	return upower_tbl->row[row].cap;
}
#else
static inline unsigned int get_opp_capacity(struct cpufreq_policy *policy,
						int row)
{
	unsigned int cap, orig_cap;
	unsigned long freq, max_freq;

	max_freq = policy->cpuinfo.max_freq;
	orig_cap = capacity_orig_of(policy->cpu);

	freq = policy->freq_table[row].frequency;
	cap = orig_cap * freq / max_freq;

	return cap;
}
#endif

static unsigned int util_to_targetload(
	struct sugov_tunables *tunables, unsigned int util)
{
	int i;
	unsigned int ret;
	unsigned long flags;

	spin_lock_irqsave(&tunables->target_loads_lock, flags);

	for (i = 0; i < tunables->ntarget_loads - 1 &&
		     util >= tunables->util_loads[i+1]; i += 2)
		;

	ret = tunables->util_loads[i];
	spin_unlock_irqrestore(&tunables->target_loads_lock, flags);
	return ret;
}

unsigned int find_util_l(struct sugov_policy *sg_policy, unsigned int util)
{
	unsigned int idx, capacity;

	for (idx = 0; idx < sg_policy->len; idx++) {
	/*TODO: find the first bigger one in table, to match below orginal codes
	 *in cpufreq_schedutil_plus.c
	 *tbl = upower_get_core_tbl(cpu);
	 *for (idx = 0; idx < tbl->row_num ; idx++) {
	 *	cap = tbl->row[idx].cap;
	 *	if (!cap)
	 *		break;
	 *
	 *	target_idx = idx;
	 *
	 *	if (cap > util)
	 *		break;
	 *}
	*/
//		if (sg_policy->freq2util[idx].cap >= util)
		capacity = get_opp_capacity(sg_policy->policy, idx);
		if (capacity >= util)
				return capacity;
	}
	return get_opp_capacity(sg_policy->policy, sg_policy->len - 1);
}

unsigned int find_util_h(struct sugov_policy *sg_policy, unsigned int util)
{
	unsigned int idx, capacity;
	int target_idx = -1;

	for (idx = 0; idx < sg_policy->len; idx++) {
		capacity = get_opp_capacity(sg_policy->policy, idx);
		if (capacity ==  util) {
			return util;
		}
		if (capacity < util) {
				target_idx = idx;
				continue;
		}
        if (target_idx == -1)
			return capacity;
		return get_opp_capacity(sg_policy->policy, target_idx);
	}
	return get_opp_capacity(sg_policy->policy, target_idx);
}

unsigned int find_closest_util(struct sugov_policy *sg_policy, unsigned int util
		, unsigned int policy)
{
	switch (policy) {
	case CPUFREQ_RELATION_L:
		return find_util_l(sg_policy, util);
	case CPUFREQ_RELATION_H:
		return find_util_h(sg_policy, util);
	default:
		return util;
	}
}

unsigned int choose_util(struct sugov_policy *sg_policy,
		unsigned int util)
{
	unsigned int prevutil, utilmin, utilmax;
	unsigned int tl;
	unsigned long orig_util = util;

	if (!sg_policy) {
		pr_err("sg_policy is null\n");
		return -EINVAL;
	}

	utilmin = 0;
	utilmax = UINT_MAX;

	do {
		prevutil = util;
		tl = util_to_targetload(sg_policy->tunables, util);

		/*
		 * Find the lowest frequency where the computed load is less
		 * than or equal to the target load.
		 */

		util = find_closest_util(sg_policy, (orig_util * 100 / tl), CPUFREQ_RELATION_L);
		trace_choose_util(util, prevutil, utilmax, utilmin, tl);

		if (util > prevutil) {
			/* The previous frequency is too low. */
			utilmin = prevutil;

			if (util >= utilmax) {
				/*
				 * Find the highest frequency that is less
				 * than freqmax.
				 */
				util = find_closest_util(sg_policy, utilmax - 1,CPUFREQ_RELATION_H);

				if (util == utilmin) {
					/*
					 * The first frequency below freqmax
					 * has already been found to be too
					 * low.  freqmax is the lowest speed
					 * we found that is fast enough.
					 */
					util = utilmax;
					break;
				}
			}
		} else if (util < prevutil) {
			/* The previous frequency is high enough. */
			utilmax = prevutil;

			if (util <= utilmin) {
				/*
				 * Find the lowest frequency that is higher
				 * than freqmin.
				 */
				util = find_closest_util(sg_policy, utilmin + 1, CPUFREQ_RELATION_L);

				/*
				 * If freqmax is the first frequency above
				 * freqmin then we have already found that
				 * this speed is fast enough.
				 */
				if (util == utilmax)
					break;
			}
		}

		/* If same frequency chosen as previous then done. */
	} while (util != prevutil);

	return util;
}
#endif

#ifdef CONFIG_NONLINEAR_FREQ_CTL

#include "cpufreq_schedutil_plus.c"
#else
/**
 * get_next_freq - Compute a new frequency for a given cpufreq policy.
 * @sg_policy: schedutil policy object to compute the new frequency for.
 * @util: Current CPU utilization.
 * @max: CPU capacity.
 *
 * If the utilization is frequency-invariant, choose the new frequency to be
 * proportional to it, that is
 *
 * next_freq = C * max_freq * util / max
 *
 * Otherwise, approximate the would-be frequency-invariant utilization by
 * util_raw * (curr_freq / max_freq) which leads to
 *
 * next_freq = C * curr_freq * util_raw / max
 *
 * Take C = 1.25 for the frequency tipping point at (util / max) = 0.8.
 *
 * The lowest driver-supported frequency which is equal or greater than the raw
 * next_freq (as calculated above) is returned, subject to policy min/max and
 * cpufreq driver limitations.
 */
static unsigned int get_next_freq(struct sugov_policy *sg_policy,
				  unsigned long util, unsigned long max)
{
	struct cpufreq_policy *policy = sg_policy->policy;
	unsigned int freq = arch_scale_freq_invariant() ?
				policy->cpuinfo.max_freq : policy->cur;

#ifdef CONFIG_MTK_SCHED_EXTENSION
	freq = map_util_freq_with_margin(util, freq, max);
#else
	freq = map_util_freq(util, freq, max);
#endif

#ifdef CONFIG_MTK_TINYSYS_SSPM_SUPPORT
	freq = clamp_val(freq, policy->min, policy->max);
#endif

	if (freq == sg_policy->cached_raw_freq && !sg_policy->need_freq_update)
		return sg_policy->next_freq;

	sg_policy->need_freq_update = false;
	sg_policy->prev_cached_raw_freq = sg_policy->cached_raw_freq;
	sg_policy->cached_raw_freq = freq;

#ifdef CONFIG_MTK_TINYSYS_SSPM_SUPPORT
	return freq;
#else
	return cpufreq_driver_resolve_freq(policy, freq);
#endif
}
#endif

/*
 * This function computes an effective utilization for the given CPU, to be
 * used for frequency selection given the linear relation: f = u * f_max.
 *
 * The scheduler tracks the following metrics:
 *
 *   cpu_util_{cfs,rt,dl,irq}()
 *   cpu_bw_dl()
 *
 * Where the cfs,rt and dl util numbers are tracked with the same metric and
 * synchronized windows and are thus directly comparable.
 *
 * The @util parameter passed to this function is assumed to be the aggregation
 * of RT and CFS util numbers. The cases of DL and IRQ are managed here.
 *
 * The cfs,rt,dl utilization are the running times measured with rq->clock_task
 * which excludes things like IRQ and steal-time. These latter are then accrued
 * in the irq utilization.
 *
 * The DL bandwidth number otoh is not a measured metric but a value computed
 * based on the task model parameters and gives the minimal utilization
 * required to meet deadlines.
 */
unsigned long schedutil_cpu_util(int cpu, unsigned long util_cfs,
				 unsigned long max, enum schedutil_type type,
				 struct task_struct *p)
{
	unsigned long dl_util, util, irq;
	struct rq *rq = cpu_rq(cpu);

	if (sched_feat(SUGOV_RT_MAX_FREQ) && !IS_BUILTIN(CONFIG_UCLAMP_TASK) &&
	    type == FREQUENCY_UTIL && rt_rq_is_runnable(&rq->rt)) {
		return max;
	}

	/*
	 * Early check to see if IRQ/steal time saturates the CPU, can be
	 * because of inaccuracies in how we track these -- see
	 * update_irq_load_avg().
	 */
	irq = cpu_util_irq(rq);
	if (unlikely(irq >= max))
		return max;

	/*
	 * Because the time spend on RT/DL tasks is visible as 'lost' time to
	 * CFS tasks and we use the same metric to track the effective
	 * utilization (PELT windows are synchronized) we can directly add them
	 * to obtain the CPU's actual utilization.
	 *
	 * CFS and RT utilization can be boosted or capped, depending on
	 * utilization clamp constraints requested by currently RUNNABLE
	 * tasks.
	 * When there are no CFS RUNNABLE tasks, clamps are released and
	 * frequency will be gracefully reduced with the utilization decay.
	 */
	util = util_cfs + cpu_util_rt(rq);
	if (type == FREQUENCY_UTIL)
		util = uclamp_rq_util_with(rq, util, p);

	dl_util = cpu_util_dl(rq);

	/*
	 * For frequency selection we do not make cpu_util_dl() a permanent part
	 * of this sum because we want to use cpu_bw_dl() later on, but we need
	 * to check if the CFS+RT+DL sum is saturated (ie. no idle time) such
	 * that we select f_max when there is no idle time.
	 *
	 * NOTE: numerical errors or stop class might cause us to not quite hit
	 * saturation when we should -- something for later.
	 */
	if (util + dl_util >= max)
		return max;

	/*
	 * OTOH, for energy computation we need the estimated running time, so
	 * include util_dl and ignore dl_bw.
	 */
	if (type == ENERGY_UTIL)
		util += dl_util;

	/*
	 * There is still idle time; further improve the number by using the
	 * irq metric. Because IRQ/steal time is hidden from the task clock we
	 * need to scale the task numbers:
	 *
	 *              1 - irq
	 *   U' = irq + ------- * U
	 *                max
	 */
	util = scale_irq_capacity(util, irq, max);
	util += irq;

	/*
	 * Bandwidth required by DEADLINE must always be granted while, for
	 * FAIR and RT, we use blocked utilization of IDLE CPUs as a mechanism
	 * to gracefully reduce the frequency when no tasks show up for longer
	 * periods of time.
	 *
	 * Ideally we would like to set bw_dl as min/guaranteed freq and util +
	 * bw_dl as requested freq. However, cpufreq is not yet ready for such
	 * an interface. So, we only do the latter for now.
	 */
	if (type == FREQUENCY_UTIL)
		util += cpu_bw_dl(rq);

	return min(max, util);
}

static unsigned long sugov_get_util(struct sugov_cpu *sg_cpu)
{
	struct rq *rq = cpu_rq(sg_cpu->cpu);
#ifdef CONFIG_SCHED_TUNE
	unsigned long util = stune_util(sg_cpu->cpu, cpu_util_rt(rq));
#else
	unsigned long util = cpu_util_freq(sg_cpu->cpu);
#endif
	unsigned long util_cfs = util - cpu_util_rt(rq);
	unsigned long max = arch_scale_cpu_capacity(NULL, sg_cpu->cpu);

	sg_cpu->max = max;
	sg_cpu->bw_dl = cpu_bw_dl(rq);

	return schedutil_cpu_util(sg_cpu->cpu, util_cfs, max,
				  FREQUENCY_UTIL, NULL);
}

/**
 * sugov_iowait_reset() - Reset the IO boost status of a CPU.
 * @sg_cpu: the sugov data for the CPU to boost
 * @time: the update time from the caller
 * @set_iowait_boost: true if an IO boost has been requested
 *
 * The IO wait boost of a task is disabled after a tick since the last update
 * of a CPU. If a new IO wait boost is requested after more then a tick, then
 * we enable the boost starting from the minimum frequency, which improves
 * energy efficiency by ignoring sporadic wakeups from IO.
 */
static bool sugov_iowait_reset(struct sugov_cpu *sg_cpu, u64 time,
			       bool set_iowait_boost)
{
	s64 delta_ns = time - sg_cpu->last_update;

	/* Reset boost only if a tick has elapsed since last request */
	if (delta_ns <= TICK_NSEC)
		return false;

	sg_cpu->iowait_boost = set_iowait_boost ? sg_cpu->min : 0;
	sg_cpu->iowait_boost_pending = set_iowait_boost;

	return true;
}

/**
 * sugov_iowait_boost() - Updates the IO boost status of a CPU.
 * @sg_cpu: the sugov data for the CPU to boost
 * @time: the update time from the caller
 * @flags: SCHED_CPUFREQ_IOWAIT if the task is waking up after an IO wait
 *
 * Each time a task wakes up after an IO operation, the CPU utilization can be
 * boosted to a certain utilization which doubles at each "frequent and
 * successive" wakeup from IO, ranging from the utilization of the minimum
 * OPP to the utilization of the maximum OPP.
 * To keep doubling, an IO boost has to be requested at least once per tick,
 * otherwise we restart from the utilization of the minimum OPP.
 */
static void sugov_iowait_boost(struct sugov_cpu *sg_cpu, u64 time,
			       unsigned int flags)
{
	bool set_iowait_boost = flags & SCHED_CPUFREQ_IOWAIT;

	/* Reset boost if the CPU appears to have been idle enough */
	if (sg_cpu->iowait_boost &&
	    sugov_iowait_reset(sg_cpu, time, set_iowait_boost))
		return;

	/* Boost only tasks waking up after IO */
	if (!set_iowait_boost)
		return;

	/* Ensure boost doubles only one time at each request */
	if (sg_cpu->iowait_boost_pending)
		return;
	sg_cpu->iowait_boost_pending = true;

	/* Double the boost at each request */
	if (sg_cpu->iowait_boost) {
		sg_cpu->iowait_boost =
			min_t(unsigned int, sg_cpu->iowait_boost << 1, SCHED_CAPACITY_SCALE);

		return;
	}

	/* First wakeup after IO: start with minimum boost */
	sg_cpu->iowait_boost = sg_cpu->min;
}

/**
 * sugov_iowait_apply() - Apply the IO boost to a CPU.
 * @sg_cpu: the sugov data for the cpu to boost
 * @time: the update time from the caller
 * @util: the utilization to (eventually) boost
 * @max: the maximum value the utilization can be boosted to
 *
 * A CPU running a task which woken up after an IO operation can have its
 * utilization boosted to speed up the completion of those IO operations.
 * The IO boost value is increased each time a task wakes up from IO, in
 * sugov_iowait_apply(), and it's instead decreased by this function,
 * each time an increase has not been requested (!iowait_boost_pending).
 *
 * A CPU which also appears to have been idle for at least one tick has also
 * its IO boost utilization reset.
 *
 * This mechanism is designed to boost high frequently IO waiting tasks, while
 * being more conservative on tasks which does sporadic IO operations.
 */
static unsigned long sugov_iowait_apply(struct sugov_cpu *sg_cpu, u64 time,
					unsigned long util, unsigned long max)
{
	unsigned long boost;

	/* No boost currently required */
	if (!sg_cpu->iowait_boost)
		return util;

	/* Reset boost if the CPU appears to have been idle enough */
	if (sugov_iowait_reset(sg_cpu, time, false))
		return util;

	if (!sg_cpu->iowait_boost_pending) {
		/*
		 * No boost pending; reduce the boost value.
		 */
		sg_cpu->iowait_boost >>= 1;
		if (sg_cpu->iowait_boost < sg_cpu->min) {
			sg_cpu->iowait_boost = 0;
			return util;
		}
	}

	sg_cpu->iowait_boost_pending = false;

	/*
	 * @util is already in capacity scale; convert iowait_boost
	 * into the same scale so we can compare.
	 */
	boost = (sg_cpu->iowait_boost * max) >> SCHED_CAPACITY_SHIFT;
	return max(boost, util);
}

/*
 * Make sugov_should_update_freq() ignore the rate limit when DL
 * has increased the utilization.
 */
static inline void ignore_dl_rate_limit(struct sugov_cpu *sg_cpu, struct sugov_policy *sg_policy)
{
	if (cpu_bw_dl(cpu_rq(sg_cpu->cpu)) > sg_cpu->bw_dl)
		sg_policy->limits_changed = true;
}

static inline void __cpufreq_notifier_fp(int cid, unsigned int next_f)
{
	if (cpufreq_notifier_fp)
		cpufreq_notifier_fp(cid, next_f);
}

static void sugov_update_single(struct update_util_data *hook, u64 time,
				unsigned int flags)
{
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
/*#ifdef CONFIG_MTK_TINYSYS_SSPM_SUPPORT */
	struct cpufreq_policy *policy = sg_policy->policy;
/*#endif */
	int cid = arch_cpu_cluster_id(policy->cpu);
	unsigned long util, max;
	unsigned int next_f;

	raw_spin_lock(&sg_policy->update_lock);

	sugov_iowait_boost(sg_cpu, time, flags);
	sg_cpu->last_update = time;

	ignore_dl_rate_limit(sg_cpu, sg_policy);

	if (!sugov_should_update_freq(sg_policy, time)) {
		raw_spin_unlock(&sg_policy->update_lock);
		return;
	}


	util = sugov_get_util(sg_cpu);
	max = sg_cpu->max;
	util = sugov_iowait_apply(sg_cpu, time, util, max);
#ifdef CONFIG_UCLAMP_TASK
	trace_schedutil_uclamp_util(policy->cpu, util);
#endif
	next_f = get_next_freq(sg_policy, util, max);

#ifdef CONFIG_MTK_TINYSYS_SSPM_SUPPORT
	if (sugov_update_next_freq(sg_policy, time, next_f)) {
		mt_cpufreq_set_by_wfi_load_cluster(cid, next_f);
		policy->cur = next_f;
#if defined(OPLUS_FEATURE_TASK_CPUSTATS) && defined(CONFIG_OPLUS_SCHED)
	update_freq_info(policy);
#endif /* defined(OPLUS_FEATURE_TASK_CPUSTATS) && defined(CONFIG_OPLUS_SCHED) */
		trace_sched_util(cid, next_f, time);
	}
#else

	/*
	 * This code runs under rq->lock for the target CPU, so it won't run
	 * concurrently on two different CPUs for the same target and it is not
	 * necessary to acquire the lock in the fast switch case.
	 */
	if (sg_policy->policy->fast_switch_enabled) {
		sugov_fast_switch(sg_policy, time, next_f);
	} else {
		sugov_deferred_update(sg_policy, time, next_f);
	}
#endif

	__cpufreq_notifier_fp(cid, next_f);
	raw_spin_unlock(&sg_policy->update_lock);

}

static unsigned int sugov_next_freq_shared(struct sugov_cpu *sg_cpu, u64 time)
{
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	struct cpufreq_policy *policy = sg_policy->policy;
	unsigned long util = 0, max = 1;
	unsigned int j;
	unsigned int next_f;
#ifdef CONFIG_MTK_TINYSYS_SSPM_SUPPORT
	int cid;
#endif

	for_each_cpu(j, policy->cpus) {
		struct sugov_cpu *j_sg_cpu = &per_cpu(sugov_cpu, j);
		unsigned long j_util, j_max;

		j_util = sugov_get_util(j_sg_cpu);
		j_max = j_sg_cpu->max;
		j_util = sugov_iowait_apply(j_sg_cpu, time, j_util, j_max);

#ifdef CONFIG_UCLAMP_TASK
		trace_schedutil_uclamp_util(j, j_util);
#endif

		if (j_util * max > j_max * util) {
			util = j_util;
			max = j_max;
		}
	}

	next_f = get_next_freq(sg_policy, util, max);

#ifdef CONFIG_MTK_TINYSYS_SSPM_SUPPORT
	cid = arch_cpu_cluster_id(policy->cpu);
	next_f = mt_cpufreq_find_close_freq(cid, next_f);
#endif

	return next_f;
}

static void
sugov_update_shared(struct update_util_data *hook, u64 time, unsigned int flags)
{
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	unsigned int next_f;
	int cid;

	raw_spin_lock(&sg_policy->update_lock);

#if defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_SCHED_WALT)
	sg_policy->flags = flags;
#endif /* OPLUS_FEATURE_SCHED_ASSIST */

	sugov_iowait_boost(sg_cpu, time, flags);
	sg_cpu->last_update = time;

	ignore_dl_rate_limit(sg_cpu, sg_policy);

	cid = arch_cpu_cluster_id(sg_policy->policy->cpu);

	if (sugov_should_update_freq(sg_policy, time)) {
		next_f = sugov_next_freq_shared(sg_cpu, time);

#ifdef CONFIG_MTK_TINYSYS_SSPM_SUPPORT
		if (sugov_update_next_freq(sg_policy, time, next_f)) {
			next_f = mt_cpufreq_find_close_freq(cid, next_f);
			mt_cpufreq_set_by_wfi_load_cluster(cid, next_f);
			__cpufreq_notifier_fp(cid, next_f);
			trace_sched_util(cid, next_f, time);
		}
#else
		if (sg_policy->policy->fast_switch_enabled)
			sugov_fast_switch(sg_policy, time, next_f);
		else
			sugov_deferred_update(sg_policy, time, next_f);
		__cpufreq_notifier_fp(cid, next_f);
#endif
	}

	raw_spin_unlock(&sg_policy->update_lock);
}

static void sugov_work(struct kthread_work *work)
{
	struct sugov_policy *sg_policy = container_of(work, struct sugov_policy, work);
	unsigned int freq;
	unsigned long flags;

	/*
	 * Hold sg_policy->update_lock shortly to handle the case where:
	 * incase sg_policy->next_freq is read here, and then updated by
	 * sugov_deferred_update() just before work_in_progress is set to false
	 * here, we may miss queueing the new update.
	 *
	 * Note: If a work was queued after the update_lock is released,
	 * sugov_work() will just be called again by kthread_work code; and the
	 * request will be proceed before the sugov thread sleeps.
	 */
	raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
	freq = sg_policy->next_freq;
	sg_policy->work_in_progress = false;
	raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);

	mutex_lock(&sg_policy->work_lock);
	__cpufreq_driver_target(sg_policy->policy, freq, CPUFREQ_RELATION_L);
	mutex_unlock(&sg_policy->work_lock);
}

static void sugov_irq_work(struct irq_work *irq_work)
{
	struct sugov_policy *sg_policy;

	sg_policy = container_of(irq_work, struct sugov_policy, irq_work);

	kthread_queue_work(&sg_policy->worker, &sg_policy->work);
}

/************************** sysfs interface ************************/

static struct sugov_tunables *global_tunables;
static DEFINE_MUTEX(global_tunables_lock);

static inline struct sugov_tunables *to_sugov_tunables(struct gov_attr_set *attr_set)
{
	return container_of(attr_set, struct sugov_tunables, attr_set);
}

static DEFINE_MUTEX(min_rate_lock);

static void update_min_rate_limit_ns(struct sugov_policy *sg_policy)
{
	mutex_lock(&min_rate_lock);
	sg_policy->min_rate_limit_ns = min(sg_policy->up_rate_delay_ns,
					   sg_policy->down_rate_delay_ns);
	mutex_unlock(&min_rate_lock);
}

static ssize_t up_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->up_rate_limit_us);
}

static ssize_t down_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->down_rate_limit_us);
}

static ssize_t up_rate_limit_us_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	struct sugov_policy *sg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->up_rate_limit_us = rate_limit_us;

	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->up_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_ns(sg_policy);
	}

	return count;
}

static ssize_t down_rate_limit_us_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	struct sugov_policy *sg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->down_rate_limit_us = rate_limit_us;

	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->down_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_ns(sg_policy);
	}

	return count;
}

static struct governor_attr up_rate_limit_us = __ATTR_RW(up_rate_limit_us);
static struct governor_attr down_rate_limit_us = __ATTR_RW(down_rate_limit_us);

#if defined(OPLUS_FEATURE_SCHEDUTIL_USE_TL) && defined(CONFIG_SCHEDUTIL_USE_TL)
static ssize_t target_loads_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	int i;
	ssize_t ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&tunables->target_loads_lock, flags);
	for (i = 0; i < tunables->ntarget_loads; i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret - 1, "%u%s", tunables->target_loads[i],
			i & 0x1 ? ":" : " ");

	snprintf(buf + ret - 1, PAGE_SIZE - ret - 1, "\n");
	spin_unlock_irqrestore(&tunables->target_loads_lock, flags);
	return ret;
}

static unsigned int *get_tokenized_data(const char *buf, int *num_tokens)
{
	const char *cp;
	int i;
	int ntokens = 1;
	unsigned int *tokenized_data;
	int err = -EINVAL;

	cp = buf;
	while ((cp = strpbrk(cp + 1, " :")))
		ntokens++;

	if (!(ntokens & 0x1))
		goto err;

	tokenized_data = kmalloc(ntokens * sizeof(unsigned int), GFP_KERNEL);
	if (!tokenized_data) {
		err = -ENOMEM;
		goto err;
	}

	cp = buf;
	i = 0;
	while (i < ntokens) {
		if (sscanf(cp, "%u", &tokenized_data[i++]) != 1)
			goto err_kfree;

		cp = strpbrk(cp, " :");
		if (!cp)
			break;
		cp++;
	}

	if (i != ntokens)
		goto err_kfree;

	*num_tokens = ntokens;

	return tokenized_data;
err_kfree:
	kfree(tokenized_data);
err:
	return ERR_PTR(err);
}

static unsigned int freq2util(struct sugov_policy *sg_policy, unsigned int freq)
{
	int idx;
	unsigned int capacity, opp_freq;
#ifdef CONFIG_MTK_TINYSYS_SSPM_SUPPORT
	int cpu = sg_policy->policy->cpu;
	int cid = arch_cpu_cluster_id(cpu);

	cid = arch_cpu_cluster_id(cpu);
	freq = mt_cpufreq_find_close_freq(cid, freq);
#endif
	for (idx = 0; idx < sg_policy->len; idx++) {
		capacity = get_opp_capacity(sg_policy->policy, idx);
		opp_freq = mt_cpufreq_get_cpu_freq(sg_policy->policy->cpu, idx);
		if (freq <= opp_freq)
			return capacity;
	}
	return get_opp_capacity(sg_policy->policy, sg_policy->len - 1);
}

static ssize_t target_loads_store(struct gov_attr_set *attr_set, const char *buf,
					size_t count)
{
	int ntokens, i;
	unsigned int *new_target_loads = NULL;
	unsigned long flags;
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	struct sugov_policy *sg_policy;
	unsigned int *new_util_loads = NULL;


	//get the first policy if this tunnables have mutil policies
	sg_policy = list_first_entry(&attr_set->policy_list, struct sugov_policy, tunables_hook);
	if (!sg_policy) {
		pr_err("sg_policy is null\n");
		return count;
	}

	new_target_loads = get_tokenized_data(buf, &ntokens);
	for(i = 0; i < ntokens; i++) {
		printk("token %d is %d\n", i, new_target_loads[i]);
	}
	if (IS_ERR(new_target_loads))
		return PTR_ERR(new_target_loads);

	new_util_loads = kzalloc(ntokens * sizeof(unsigned int), GFP_KERNEL);
	if (!new_util_loads)
		return -ENOMEM;

	memcpy(new_util_loads, new_target_loads, sizeof(unsigned int) * ntokens);
	for (i = 0; i < ntokens - 1; i += 2) {
			new_util_loads[i+1] = freq2util(sg_policy, new_target_loads[i+1]);
			printk("freq = %d, util = %d\n", new_target_loads[i+1], new_util_loads[i+1]);
	}

	spin_lock_irqsave(&tunables->target_loads_lock, flags);
	if (tunables->target_loads != default_target_loads)
		kfree(tunables->target_loads);
	if (tunables->util_loads != default_target_loads)
		kfree(tunables->util_loads);

	tunables->target_loads = new_target_loads;
	tunables->ntarget_loads = ntokens;
	tunables->util_loads = new_util_loads;
	spin_unlock_irqrestore(&tunables->target_loads_lock, flags);

	return count;
}

ssize_t set_sugov_tl(unsigned int cpu, char *buf)
{
	struct cpufreq_policy *policy;
	struct sugov_policy *sg_policy;
	struct sugov_tunables *tunables;
	struct gov_attr_set *attr_set;
	size_t count;

	if (!buf)
		return -EFAULT;

	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return -ENODEV;

	sg_policy = policy->governor_data;
	if (!sg_policy)
		return -EINVAL;

	tunables = sg_policy->tunables;
	if (!tunables)
		return -ENOMEM;

	attr_set = &tunables->attr_set;
	count = strlen(buf);

	return target_loads_store(attr_set, buf, count);
}
EXPORT_SYMBOL_GPL(set_sugov_tl);
#endif

#if defined(OPLUS_FEATURE_SCHEDUTIL_USE_TL) && defined(CONFIG_SCHEDUTIL_USE_TL)
static struct governor_attr target_loads =
	__ATTR(target_loads, 0664, target_loads_show, target_loads_store);
#endif

static struct attribute *sugov_attributes[] = {
	&up_rate_limit_us.attr,
	&down_rate_limit_us.attr,
#if defined(OPLUS_FEATURE_SCHEDUTIL_USE_TL) && defined(CONFIG_SCHEDUTIL_USE_TL)
	&target_loads.attr,
#endif
	NULL
};

static void sugov_tunables_free(struct kobject *kobj)
{
	struct gov_attr_set *attr_set = container_of(kobj, struct gov_attr_set, kobj);

	kfree(to_sugov_tunables(attr_set));
}

static struct kobj_type sugov_tunables_ktype = {
	.default_attrs = sugov_attributes,
	.sysfs_ops = &governor_sysfs_ops,
	.release = &sugov_tunables_free,
};

/********************** cpufreq governor interface *********************/

struct cpufreq_governor schedutil_gov;

int schedutil_set_down_rate_limit_us(int cpu, unsigned int rate_limit_us)
{
	struct cpufreq_policy *policy;
	struct sugov_policy *sg_policy;
	struct sugov_tunables *tunables;
	struct gov_attr_set *attr_set;

	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return -EINVAL;

	if (policy->governor != &schedutil_gov)
		return -ENOENT;

	mutex_lock(&global_tunables_lock);
	sg_policy = policy->governor_data;
	if (!sg_policy) {
		mutex_unlock(&global_tunables_lock);
		cpufreq_cpu_put(policy);
		return -EINVAL;
	}

	tunables = sg_policy->tunables;
	tunables->down_rate_limit_us = rate_limit_us;
	attr_set = &tunables->attr_set;

	mutex_lock(&attr_set->update_lock);
	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->down_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_ns(sg_policy);
	}
	mutex_unlock(&attr_set->update_lock);
	mutex_unlock(&global_tunables_lock);

	if (policy)
		cpufreq_cpu_put(policy);
	return 0;
}
EXPORT_SYMBOL(schedutil_set_down_rate_limit_us);

int schedutil_set_up_rate_limit_us(int cpu, unsigned int rate_limit_us)
{
	struct cpufreq_policy *policy;
	struct sugov_policy *sg_policy;
	struct sugov_tunables *tunables;
	struct gov_attr_set *attr_set;

	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return -EINVAL;

	if (policy->governor != &schedutil_gov)
		return -ENOENT;

	mutex_lock(&global_tunables_lock);
	sg_policy = policy->governor_data;
	if (!sg_policy) {
		mutex_unlock(&global_tunables_lock);
		cpufreq_cpu_put(policy);
		return -EINVAL;
	}

	tunables = sg_policy->tunables;
	tunables->up_rate_limit_us = rate_limit_us;
	attr_set = &tunables->attr_set;

	mutex_lock(&attr_set->update_lock);
	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->up_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_ns(sg_policy);
	}
	mutex_unlock(&attr_set->update_lock);
	mutex_unlock(&global_tunables_lock);

	if (policy)
		cpufreq_cpu_put(policy);
	return 0;
}
EXPORT_SYMBOL(schedutil_set_up_rate_limit_us);

static struct sugov_policy *sugov_policy_alloc(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy;

	sg_policy = kzalloc(sizeof(*sg_policy), GFP_KERNEL);
	if (!sg_policy)
		return NULL;

	sg_policy->policy = policy;
	raw_spin_lock_init(&sg_policy->update_lock);
	return sg_policy;
}

static void sugov_policy_free(struct sugov_policy *sg_policy)
{
	kfree(sg_policy);
}

static int sugov_kthread_create(struct sugov_policy *sg_policy)
{
	struct task_struct *thread;
	struct sched_attr attr = {
		.size		= sizeof(struct sched_attr),
		.sched_policy	= SCHED_DEADLINE,
		.sched_flags	= SCHED_FLAG_SUGOV,
		.sched_nice	= 0,
		.sched_priority	= 0,
		/*
		 * Fake (unused) bandwidth; workaround to "fix"
		 * priority inheritance.
		 */
		.sched_runtime	=  1000000,
		.sched_deadline = 10000000,
		.sched_period	= 10000000,
	};
	struct cpufreq_policy *policy = sg_policy->policy;
	int ret;

	/* kthread only required for slow path */
	if (policy->fast_switch_enabled)
		return 0;

	kthread_init_work(&sg_policy->work, sugov_work);
	kthread_init_worker(&sg_policy->worker);
	thread = kthread_create(kthread_worker_fn, &sg_policy->worker,
				"sugov:%d",
				cpumask_first(policy->related_cpus));
	if (IS_ERR(thread)) {
		pr_err("failed to create sugov thread: %ld\n", PTR_ERR(thread));
		return PTR_ERR(thread);
	}

	ret = sched_setattr_nocheck(thread, &attr);
	if (ret) {
		kthread_stop(thread);
		pr_warn("%s: failed to set SCHED_DEADLINE\n", __func__);
		return ret;
	}

	sg_policy->thread = thread;
	kthread_bind_mask(thread, policy->related_cpus);
	init_irq_work(&sg_policy->irq_work, sugov_irq_work);
	mutex_init(&sg_policy->work_lock);

	wake_up_process(thread);

	return 0;
}

static void sugov_kthread_stop(struct sugov_policy *sg_policy)
{
	/* kthread only required for slow path */
	if (sg_policy->policy->fast_switch_enabled)
		return;

	kthread_flush_worker(&sg_policy->worker);
	kthread_stop(sg_policy->thread);
	mutex_destroy(&sg_policy->work_lock);
}

static struct sugov_tunables *sugov_tunables_alloc(struct sugov_policy *sg_policy)
{
	struct sugov_tunables *tunables;

	tunables = kzalloc(sizeof(*tunables), GFP_KERNEL);
	if (tunables) {
		gov_attr_set_init(&tunables->attr_set, &sg_policy->tunables_hook);
		if (!have_governor_per_policy())
			global_tunables = tunables;
	}
	return tunables;
}

static void sugov_clear_global_tunables(void)
{
	if (!have_governor_per_policy())
		global_tunables = NULL;
}

static int sugov_init(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy;
	struct sugov_tunables *tunables;
	int ret = 0;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	policy->dvfs_possible_from_any_cpu = true;

	cpufreq_enable_fast_switch(policy);

	sg_policy = sugov_policy_alloc(policy);
	if (!sg_policy) {
		ret = -ENOMEM;
		goto disable_fast_switch;
	}

#if defined(OPLUS_FEATURE_SCHEDUTIL_USE_TL) && defined(CONFIG_SCHEDUTIL_USE_TL)
	sg_policy->len = UPOWER_OPP_NUM;
#endif

	ret = sugov_kthread_create(sg_policy);
	if (ret)
		goto free_sg_policy;

	mutex_lock(&global_tunables_lock);

	if (global_tunables) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto stop_kthread;
		}
		policy->governor_data = sg_policy;
		sg_policy->tunables = global_tunables;

		gov_attr_set_get(&global_tunables->attr_set, &sg_policy->tunables_hook);
		goto out;
	}

	tunables = sugov_tunables_alloc(sg_policy);
	if (!tunables) {
		ret = -ENOMEM;
		goto stop_kthread;
	}

	tunables->up_rate_limit_us = cpufreq_policy_transition_delay_us(policy);
	tunables->down_rate_limit_us = cpufreq_policy_transition_delay_us(policy);

#if defined(OPLUS_FEATURE_SCHEDUTIL_USE_TL) && defined(CONFIG_SCHEDUTIL_USE_TL)
	tunables->target_loads = default_target_loads;
	tunables->ntarget_loads = ARRAY_SIZE(default_target_loads);
	//same with target_loads by default
	tunables->util_loads = default_target_loads;
	spin_lock_init(&tunables->target_loads_lock);
#endif

	policy->governor_data = sg_policy;
	sg_policy->tunables = tunables;

	ret = kobject_init_and_add(&tunables->attr_set.kobj, &sugov_tunables_ktype,
				   get_governor_parent_kobj(policy), "%s",
				   schedutil_gov.name);
	if (ret)
		goto fail;


out:
	mutex_unlock(&global_tunables_lock);
	return 0;

fail:
	kobject_put(&tunables->attr_set.kobj);
	policy->governor_data = NULL;
	sugov_clear_global_tunables();

stop_kthread:
	sugov_kthread_stop(sg_policy);
	mutex_unlock(&global_tunables_lock);

free_sg_policy:
	sugov_policy_free(sg_policy);

disable_fast_switch:
	cpufreq_disable_fast_switch(policy);

	pr_err("initialization failed (error %d)\n", ret);
	return ret;
}

static void sugov_exit(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	struct sugov_tunables *tunables = sg_policy->tunables;
	unsigned int count;

	mutex_lock(&global_tunables_lock);

	count = gov_attr_set_put(&tunables->attr_set, &sg_policy->tunables_hook);
	policy->governor_data = NULL;
	if (!count)
		sugov_clear_global_tunables();

	mutex_unlock(&global_tunables_lock);

	sugov_kthread_stop(sg_policy);
	sugov_policy_free(sg_policy);
	cpufreq_disable_fast_switch(policy);
}

static int sugov_start(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	sg_policy->up_rate_delay_ns =
		sg_policy->tunables->up_rate_limit_us * NSEC_PER_USEC;
	sg_policy->down_rate_delay_ns =
		sg_policy->tunables->down_rate_limit_us * NSEC_PER_USEC;
	update_min_rate_limit_ns(sg_policy);
	sg_policy->last_freq_update_time	= 0;
	sg_policy->next_freq			= 0;
	sg_policy->work_in_progress		= false;
	sg_policy->limits_changed		= false;
	sg_policy->need_freq_update		= false;
	sg_policy->cached_raw_freq		= 0;
#if defined(OPLUS_FEATURE_SCHED_ASSIST) && defined(CONFIG_SCHED_WALT)
	sg_policy->flags	= 0;
#endif /* OPLUS_FEATURE_SCHED_ASSIST */
	sg_policy->prev_cached_raw_freq		= 0;

	for_each_cpu(cpu, policy->cpus) {
		struct sugov_cpu *sg_cpu = &per_cpu(sugov_cpu, cpu);

		memset(sg_cpu, 0, sizeof(*sg_cpu));
		sg_cpu->cpu			= cpu;
		sg_cpu->sg_policy		= sg_policy;
		sg_cpu->min			=
			(SCHED_CAPACITY_SCALE * policy->cpuinfo.min_freq) /
			policy->cpuinfo.max_freq;
	}

	for_each_cpu(cpu, policy->cpus) {
		struct sugov_cpu *sg_cpu = &per_cpu(sugov_cpu, cpu);

		cpufreq_add_update_util_hook(cpu, &sg_cpu->update_util,
					     policy_is_shared(policy) ?
							sugov_update_shared :
							sugov_update_single);
	}
	return 0;
}

static void sugov_stop(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	for_each_cpu(cpu, policy->cpus)
		cpufreq_remove_update_util_hook(cpu);

	synchronize_sched();

	if (!policy->fast_switch_enabled) {
		irq_work_sync(&sg_policy->irq_work);
		kthread_cancel_work_sync(&sg_policy->work);
	}
}

static void sugov_limits(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;

	if (!policy->fast_switch_enabled) {
		mutex_lock(&sg_policy->work_lock);
		cpufreq_policy_apply_limits(policy);
		mutex_unlock(&sg_policy->work_lock);
	}

	sg_policy->limits_changed = true;
}

struct cpufreq_governor schedutil_gov = {
	.name			= "schedutil",
	.owner			= THIS_MODULE,
	.dynamic_switching	= true,
	.init			= sugov_init,
	.exit			= sugov_exit,
	.start			= sugov_start,
	.stop			= sugov_stop,
	.limits			= sugov_limits,
};

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_SCHEDUTIL
struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &schedutil_gov;
}
#endif

static int __init sugov_register(void)
{
	return cpufreq_register_governor(&schedutil_gov);
}
fs_initcall(sugov_register);

#ifdef CONFIG_ENERGY_MODEL
extern bool sched_energy_update;
extern struct mutex sched_energy_mutex;

static void rebuild_sd_workfn(struct work_struct *work)
{
	mutex_lock(&sched_energy_mutex);
	sched_energy_update = true;
	rebuild_sched_domains();
	sched_energy_update = false;
	mutex_unlock(&sched_energy_mutex);
}
static DECLARE_WORK(rebuild_sd_work, rebuild_sd_workfn);

/*
 * EAS shouldn't be attempted without sugov, so rebuild the sched_domains
 * on governor changes to make sure the scheduler knows about it.
 */
void sched_cpufreq_governor_change(struct cpufreq_policy *policy,
				  struct cpufreq_governor *old_gov)
{
	if (old_gov == &schedutil_gov || policy->governor == &schedutil_gov) {
		/*
		 * When called from the cpufreq_register_driver() path, the
		 * cpu_hotplug_lock is already held, so use a work item to
		 * avoid nested locking in rebuild_sched_domains().
		 */
		schedule_work(&rebuild_sd_work);
	}

}
#endif
