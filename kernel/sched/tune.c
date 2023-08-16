#include <linux/cgroup.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>

#include <trace/events/sched.h>

#ifdef CONFIG_OPLUS_FG_BOOST
#include "extension/tuning.h"
#define SCHED_FG_BOOST 2
#endif /* CONFIG_OPLUS_FG_BOOST */
#include "sched.h"

bool schedtune_initialized = false;
extern struct reciprocal_value schedtune_spc_rdiv;

/* We hold schedtune boost in effect for at least this long */
#define SCHEDTUNE_BOOST_HOLD_NS 50000000ULL

/*
 * EAS scheduler tunables for task groups.
 *
 * When CGroup support is enabled, we have to synchronize two different
 * paths:
 *  - slow path: where CGroups are created/updated/removed
 *  - fast path: where tasks in a CGroups are accounted
 *
 * The slow path tracks (a limited number of) CGroups and maps each on a
 * "boost_group" index. The fastpath accounts tasks currently RUNNABLE on each
 * "boost_group".
 *
 * Once a new CGroup is created, a boost group idx is assigned and the
 * corresponding "boost_group" marked as valid on each CPU.
 * Once a CGroup is release, the corresponding "boost_group" is marked as
 * invalid on each CPU. The CPU boost value (boost_max) is aggregated by
 * considering only valid boost_groups with a non null tasks counter.
 *
 * .:: Locking strategy
 *
 * The fast path uses a spin lock for each CPU boost_group which protects the
 * tasks counter.
 *
 * The "valid" and "boost" values of each CPU boost_group is instead
 * protected by the RCU lock provided by the CGroups callbacks. Thus, only the
 * slow path can access and modify the boost_group attribtues of each CPU.
 * The fast path will catch up the most updated values at the next scheduling
 * event (i.e. enqueue/dequeue).
 *
 *                                                        |
 *                                             SLOW PATH  |   FAST PATH
 *                              CGroup add/update/remove  |   Scheduler enqueue/dequeue events
 *                                                        |
 *                                                        |
 *                                                        |     DEFINE_PER_CPU(struct boost_groups)
 *                                                        |     +--------------+----+---+----+----+
 *                                                        |     |  idle        |    |   |    |    |
 *                                                        |     |  boost_max   |    |   |    |    |
 *                                                        |  +---->lock        |    |   |    |    |
 *  struct schedtune                  allocated_groups    |  |  |  group[    ] |    |   |    |    |
 *  +------------------------------+         +-------+    |  |  +--+---------+-+----+---+----+----+
 *  | idx                          |         |       |    |  |     |  valid  |
 *  | boots / prefer_idle          |         |       |    |  |     |  boost  |
 *  | perf_{boost/constraints}_idx | <---------+(*)  |    |  |     |  tasks  | <------------+
 *  | css                          |         +-------+    |  |     +---------+              |
 *  +-+----------------------------+         |       |    |  |     |         |              |
 *    ^                                      |       |    |  |     |         |              |
 *    |                                      +-------+    |  |     +---------+              |
 *    |                                      |       |    |  |     |         |              |
 *    |                                      |       |    |  |     |         |              |
 *    |                                      +-------+    |  |     +---------+              |
 *    | zmalloc                              |       |    |  |     |         |              |
 *    |                                      |       |    |  |     |         |              |
 *    |                                      +-------+    |  |     +---------+              |
 *    +                              BOOSTGROUPS_COUNT    |  |     BOOSTGROUPS_COUNT        |
 *  schedtune_boostgroup_init()                           |  +                              |
 *                                                        |  schedtune_{en,de}queue_task()  |
 *                                                        |                                 +
 *                                                        |          schedtune_tasks_update()
 *                                                        |
 */

/* SchdTune tunables for a group of tasks */
struct schedtune {
	/* SchedTune CGroup subsystem */
	struct cgroup_subsys_state css;

	/* Boost group allocated ID */
	int idx;

	/* Boost value for tasks on that SchedTune CGroup */
	int boost;

	/* Hint to bias scheduling of tasks on that SchedTune CGroup
	 * towards idle CPUs */
	int prefer_idle;

#ifdef CONFIG_SCHEDUTIL_USE_TL
	unsigned int window_policy;
	bool discount_wait_time;
#endif

#ifdef CONFIG_UCLAMP_TASK_GROUP
	/* The two decimal precision [%] value requested from user-space */
	unsigned int		uclamp_pct[UCLAMP_CNT];
	/* Clamp values requested for a task group */
	struct uclamp_se	uclamp_req[UCLAMP_CNT];
	/* Effective clamp values used for a task group */
	struct uclamp_se	uclamp[UCLAMP_CNT];
#endif
};

static inline struct schedtune *css_st(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct schedtune, css) : NULL;
}

static inline struct schedtune *task_schedtune(struct task_struct *tsk)
{
	return css_st(task_css(tsk, schedtune_cgrp_id));
}

static inline struct schedtune *parent_st(struct schedtune *st)
{
	return css_st(st->css.parent);
}

/*
 * SchedTune root control group
 * The root control group is used to defined a system-wide boosting tuning,
 * which is applied to all tasks in the system.
 * Task specific boost tuning could be specified by creating and
 * configuring a child control group under the root one.
 * By default, system-wide boosting is disabled, i.e. no boosting is applied
 * to tasks which are not into a child control group.
 */
struct schedtune
root_schedtune = {
	.boost	= 0,
#ifdef CONFIG_SCHEDUTIL_USE_TL
	.window_policy = 2,
	.discount_wait_time = false,
#endif
	.prefer_idle = 0,
};

/*
 * Maximum number of boost groups to support
 * When per-task boosting is used we still allow only limited number of
 * boost groups for two main reasons:
 * 1. on a real system we usually have only few classes of workloads which
 *    make sense to boost with different values (e.g. background vs foreground
 *    tasks, interactive vs low-priority tasks)
 * 2. a limited number allows for a simpler and more memory/time efficient
 *    implementation especially for the computation of the per-CPU boost
 *    value
 */
#define BOOSTGROUPS_COUNT 10

/* Array of configured boostgroups */
static struct schedtune *allocated_group[BOOSTGROUPS_COUNT] = {
	&root_schedtune,
	NULL,
};

static inline bool is_group_idx_valid(int idx)
{
	return idx >= 0 && idx < BOOSTGROUPS_COUNT;
}

/* SchedTune boost groups
 * Keep track of all the boost groups which impact on CPU, for example when a
 * CPU has two RUNNABLE tasks belonging to two different boost groups and thus
 * likely with different boost values.
 * Since on each system we expect only a limited number of boost groups, here
 * we use a simple array to keep track of the metrics required to compute the
 * maximum per-CPU boosting value.
 */
struct boost_groups {
	/* Maximum boost value for all RUNNABLE tasks on a CPU */
	int boost_max;
	u64 boost_ts;
	struct {
		/* True when this boost group maps an actual cgroup */
		bool valid;
		/* The boost for tasks on that boost group */
		int boost;
		/* Count of RUNNABLE tasks on that boost group */
		unsigned tasks;
		/* Timestamp of boost activation */
		u64 ts;
	} group[BOOSTGROUPS_COUNT];
	/* CPU's boost group locking */
	raw_spinlock_t lock;
};

/* Boost groups affecting each CPU in the system */
DEFINE_PER_CPU(struct boost_groups, cpu_boost_groups);

static inline bool schedtune_boost_timeout(u64 now, u64 ts)
{
	return ((now - ts) > SCHEDTUNE_BOOST_HOLD_NS);
}

static inline bool
schedtune_boost_group_active(int idx, struct boost_groups* bg, u64 now)
{
	if (bg->group[idx].tasks)
		return true;

	return !schedtune_boost_timeout(now, bg->group[idx].ts);
}

static void
schedtune_cpu_update(int cpu, u64 now)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	int boost_max;
	u64 boost_ts;
	int idx;

	/* The root boost group is always active */
	boost_max = bg->group[0].boost;
	boost_ts = now;
	for (idx = 1; idx < BOOSTGROUPS_COUNT; ++idx) {

		/* Ignore non boostgroups not mapping a cgroup */
		if (!bg->group[idx].valid)
			continue;

		/*
		 * A boost group affects a CPU only if it has
		 * RUNNABLE tasks on that CPU or it has hold
		 * in effect from a previous task.
		 */
		if (!schedtune_boost_group_active(idx, bg, now))
			continue;

		/* This boost group is active */
		if (boost_max > bg->group[idx].boost)
			continue;

		boost_max = bg->group[idx].boost;
		boost_ts =  bg->group[idx].ts;
	}

	/* Ensures boost_max is non-negative when all cgroup boost values
	 * are neagtive. Avoids under-accounting of cpu capacity which may cause
	 * task stacking and frequency spikes.*/
	boost_max = max(boost_max, 0);
	bg->boost_max = boost_max;
	bg->boost_ts = boost_ts;
}

static int
schedtune_boostgroup_update(int idx, int boost)
{
	struct boost_groups *bg;
	int cur_boost_max;
	int old_boost;
	int cpu;
	u64 now;

	/* Update per CPU boost groups */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);

		/* CGroups are never associated to non active cgroups */
		BUG_ON(!bg->group[idx].valid);

		/*
		 * Keep track of current boost values to compute the per CPU
		 * maximum only when it has been affected by the new value of
		 * the updated boost group
		 */
		cur_boost_max = bg->boost_max;
		old_boost = bg->group[idx].boost;

		/* Update the boost value of this boost group */
		bg->group[idx].boost = boost;

		/* Check if this update increase current max */
		now = sched_clock_cpu(cpu);
		if (boost > cur_boost_max &&
			schedtune_boost_group_active(idx, bg, now)) {
			bg->boost_max = boost;
			bg->boost_ts = bg->group[idx].ts;

			trace_sched_tune_boostgroup_update(cpu, 1, bg->boost_max);
			continue;
		}

		/* Check if this update has decreased current max */
		if (cur_boost_max == old_boost && old_boost > boost) {
			schedtune_cpu_update(cpu, now);
			trace_sched_tune_boostgroup_update(cpu, -1, bg->boost_max);
			continue;
		}

		trace_sched_tune_boostgroup_update(cpu, 0, bg->boost_max);
	}

	return 0;
}

#define ENQUEUE_TASK  1
#define DEQUEUE_TASK -1

static inline bool
schedtune_update_timestamp(struct task_struct *p)
{
	if (sched_feat(SCHEDTUNE_BOOST_HOLD_ALL))
		return true;

	return task_has_rt_policy(p);
}

static inline void
schedtune_tasks_update(struct task_struct *p, int cpu, int idx, int task_count)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	int tasks = bg->group[idx].tasks + task_count;

	/* Update boosted tasks count while avoiding to make it negative */
	bg->group[idx].tasks = max(0, tasks);

	/* Update timeout on enqueue */
	if (task_count > 0) {
		u64 now = sched_clock_cpu(cpu);

		if (schedtune_update_timestamp(p))
			bg->group[idx].ts = now;

		/* Boost group activation or deactivation on that RQ */
		if (bg->group[idx].tasks == 1)
			schedtune_cpu_update(cpu, now);
	}

	trace_sched_tune_tasks_update(p, cpu, tasks, idx,
			bg->group[idx].boost, bg->boost_max,
			bg->group[idx].ts);
}

/*
 * NOTE: This function must be called while holding the lock on the CPU RQ
 */
void schedtune_enqueue_task(struct task_struct *p, int cpu)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	unsigned long irq_flags;
	struct schedtune *st;
	int idx;

	if (unlikely(!schedtune_initialized))
		return;

	/*
	 * Boost group accouting is protected by a per-cpu lock and requires
	 * interrupt to be disabled to avoid race conditions for example on
	 * do_exit()::cgroup_exit() and task migration.
	 */
	raw_spin_lock_irqsave(&bg->lock, irq_flags);
	rcu_read_lock();

	st = task_schedtune(p);
	idx = st->idx;

	schedtune_tasks_update(p, cpu, idx, ENQUEUE_TASK);

	rcu_read_unlock();
	raw_spin_unlock_irqrestore(&bg->lock, irq_flags);
}

int schedtune_can_attach(struct cgroup_taskset *tset)
{
	struct task_struct *task;
	struct cgroup_subsys_state *css;
	struct boost_groups *bg;
	struct rq_flags rq_flags;
	unsigned int cpu;
	struct rq *rq;
	int src_bg; /* Source boost group index */
	int dst_bg; /* Destination boost group index */
	int tasks;
	u64 now;

	if (unlikely(!schedtune_initialized))
		return 0;


	cgroup_taskset_for_each(task, css, tset) {

		/*
		 * Lock the CPU's RQ the task is enqueued to avoid race
		 * conditions with migration code while the task is being
		 * accounted
		 */
		rq = task_rq_lock(task, &rq_flags);

		if (!task->on_rq) {
			task_rq_unlock(rq, task, &rq_flags);
			continue;
		}

		/*
		 * Boost group accouting is protected by a per-cpu lock and requires
		 * interrupt to be disabled to avoid race conditions on...
		 */
		cpu = cpu_of(rq);
		bg = &per_cpu(cpu_boost_groups, cpu);
		raw_spin_lock(&bg->lock);

		dst_bg = css_st(css)->idx;
		src_bg = task_schedtune(task)->idx;

		/*
		 * Current task is not changing boostgroup, which can
		 * happen when the new hierarchy is in use.
		 */
		if (unlikely(dst_bg == src_bg)) {
			raw_spin_unlock(&bg->lock);
			task_rq_unlock(rq, task, &rq_flags);
			continue;
		}

		/*
		 * This is the case of a RUNNABLE task which is switching its
		 * current boost group.
		 */

		/* Move task from src to dst boost group */
		tasks = bg->group[src_bg].tasks - 1;
		bg->group[src_bg].tasks = max(0, tasks);
		bg->group[dst_bg].tasks += 1;

		/* Update boost hold start for this group */
		now = sched_clock_cpu(cpu);
		bg->group[dst_bg].ts = now;

		/* Force boost group re-evaluation at next boost check */
		bg->boost_ts = now - SCHEDTUNE_BOOST_HOLD_NS;

		raw_spin_unlock(&bg->lock);
		task_rq_unlock(rq, task, &rq_flags);
	}

	return 0;
}

void schedtune_cancel_attach(struct cgroup_taskset *tset)
{
	/* This can happen only if SchedTune controller is mounted with
	 * other hierarchies ane one of them fails. Since usually SchedTune is
	 * mouted on its own hierarcy, for the time being we do not implement
	 * a proper rollback mechanism */
	WARN(1, "SchedTune cancel attach not implemented");
}

/*
 * NOTE: This function must be called while holding the lock on the CPU RQ
 */
void schedtune_dequeue_task(struct task_struct *p, int cpu)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	unsigned long irq_flags;
	struct schedtune *st;
	int idx;

	if (unlikely(!schedtune_initialized))
		return;

	/*
	 * Boost group accouting is protected by a per-cpu lock and requires
	 * interrupt to be disabled to avoid race conditions on...
	 */
	raw_spin_lock_irqsave(&bg->lock, irq_flags);
	rcu_read_lock();

	st = task_schedtune(p);
	idx = st->idx;

	schedtune_tasks_update(p, cpu, idx, DEQUEUE_TASK);

	rcu_read_unlock();
	raw_spin_unlock_irqrestore(&bg->lock, irq_flags);
}

int schedtune_cpu_boost(int cpu)
{
	struct boost_groups *bg;
	u64 now;

	bg = &per_cpu(cpu_boost_groups, cpu);
	now = sched_clock_cpu(cpu);

	/* Check to see if we have a hold in effect */
	if (schedtune_boost_timeout(now, bg->boost_ts))
		schedtune_cpu_update(cpu, now);

	return bg->boost_max;
}

static inline bool schedtune_adj_ta(struct schedtune *st, struct task_struct *p)
{
	char name_buf[NAME_MAX + 1];
	int adj = p->signal->oom_score_adj;

	cgroup_name(st->css.cgroup, name_buf, sizeof(name_buf));
	if (!strncmp(name_buf, "top-app", strlen("top-app"))) {
		if ((adj == 0) && !(p->flags & PF_KTHREAD)) {
			pr_debug("top app is %s\n", p->comm);
			return true;
		}
	}

	return false;
}

int schedtune_task_boost(struct task_struct *p)
{
	struct schedtune *st;
	int task_boost;

	if (unlikely(!schedtune_initialized))
		return 0;

	/* Get task boost value */
	rcu_read_lock();
	st = task_schedtune(p);
	task_boost = st->boost || schedtune_adj_ta(st, p);
	rcu_read_unlock();

	return task_boost;
}

#ifdef CONFIG_OPLUS_FG_BOOST
extern int sysctl_animation_type;
extern int sched_boost_type;
void oplus_task_sched_boost(struct task_struct *p, int *task_prefer)
{
	int boost = sched_boost_type == SCHED_FG_BOOST? 1 :0;
	struct schedtune *st = NULL;
	if(!boost) {
		return;
	}
	rcu_read_lock();
	st = task_schedtune(p);
	if (((st-> idx == 3) || (st-> idx == 1))){
		if (READ_ONCE(p->se.avg.util_avg) > sysctl_boost_task_threshold){
			*task_prefer = SCHED_PREFER_MEDIUM;
		}
	} else {
		*task_prefer = SCHED_PREFER_LITTLE;
	}
	rcu_read_unlock();
}
#endif /* CONFIG_OPLUS_FG_BOOST */

int schedtune_prefer_idle(struct task_struct *p)
{
	struct schedtune *st;
	int prefer_idle;

	if (unlikely(!schedtune_initialized))
		return 0;

	/* Get prefer_idle value */
	rcu_read_lock();
	st = task_schedtune(p);
	prefer_idle = st->prefer_idle;
	rcu_read_unlock();

	return prefer_idle;
}

#ifdef CONFIG_UCLAMP_TASK_GROUP
void init_root_st_uclamp(int clamp_id)
{
	struct uclamp_se uc_max = {};

	uc_max.value = uclamp_none(UCLAMP_MAX);
	uc_max.bucket_id = uclamp_bucket_id(uc_max.value);
	uc_max.user_defined = false;

	root_schedtune.uclamp_req[clamp_id] = uc_max;
	root_schedtune.uclamp[clamp_id] = uc_max;
}

struct uclamp_se
uclamp_st_restrict(struct task_struct *p, enum uclamp_id clamp_id)
{
	struct uclamp_se uc_req = p->uclamp_req[clamp_id];
	struct uclamp_se uc_max;

	rcu_read_lock();
	/*
	 * Tasks in autogroups or root task group will be
	 * restricted by system defaults.
	 */
	if (task_schedtune(p) == &root_schedtune)
		goto unlock;

	uc_max = task_schedtune(p)->uclamp[clamp_id];

	if (UCLAMP_MIN == clamp_id && 0 == uc_max.value)
		goto unlock;
	if (!uc_req.user_defined || (uc_req.value != uc_max.value &&
						uc_max.value != uclamp_none(clamp_id))) {
		rcu_read_unlock();
		return uc_max;
	}
unlock:
	rcu_read_unlock();
	return uc_req;
}

static inline void alloc_uclamp_sched_group(struct schedtune *st,
					    struct schedtune *parent)
{
	enum uclamp_id clamp_id;

	for_each_clamp_id(clamp_id) {
		uclamp_se_set(&st->uclamp_req[clamp_id],
			      uclamp_none(clamp_id), false);
		st->uclamp[clamp_id] = parent->uclamp[clamp_id];
	}
}
#endif

static u64
prefer_idle_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct schedtune *st = css_st(css);

	return st->prefer_idle;
}

static int
prefer_idle_write(struct cgroup_subsys_state *css, struct cftype *cft,
	    u64 prefer_idle)
{
	struct schedtune *st = css_st(css);
	st->prefer_idle = !!prefer_idle;

	return 0;
}

static s64
boost_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct schedtune *st = css_st(css);

	return st->boost;
}

static int
boost_write(struct cgroup_subsys_state *css, struct cftype *cft,
	    s64 boost)
{
	struct schedtune *st = css_st(css);

	if (boost < 0 || boost > 100)
		return -EINVAL;

	st->boost = boost;

	/* Update CPU boost */
	schedtune_boostgroup_update(st->idx, st->boost);

	return 0;
}

#ifdef CONFIG_SCHEDUTIL_USE_TL
unsigned int schedtune_window_policy(struct task_struct *p)
{
	struct schedtune *st;
	unsigned int window_policy;

	if (unlikely(!schedtune_initialized))
		return 0;

	rcu_read_lock();
	st = task_schedtune(p);
	window_policy = st->window_policy;
	rcu_read_unlock();

	return window_policy;
}

unsigned int uclamp_discount_wait_time(struct task_struct *p)
{
	struct schedtune *st;
	unsigned int ret;

	if (unlikely(!schedtune_initialized))
		return 0;

	rcu_read_lock();
	st = task_schedtune(p);
	ret = st->discount_wait_time;
	rcu_read_unlock();

	return ret;
}

static u64
window_policy_read(struct cgroup_subsys_state *css,
		struct cftype *cft)
{
	struct schedtune *st = css_st(css);
	return st->window_policy;
}

static int
window_policy_write(struct cgroup_subsys_state *css, struct cftype *cft,
		u64 window_policy)
{
	struct schedtune *st = css_st(css);

	if (window_policy >= 4)
		return -EINVAL;

	st->window_policy = window_policy;

	return 0;
}

#define PE_FUNC(NAME) \
static u64 NAME##_read(struct cgroup_subsys_state *css, \
		struct cftype *cft) \
{ \
	struct schedtune *st = css_st(css); \
	return st->NAME; \
} \
 \
static int \
NAME##_write(struct cgroup_subsys_state *css, struct cftype *cft, \
		u64 NAME) \
{ \
	struct schedtune *st = css_st(css); \
 \
	st->NAME = !!NAME; \
 \
	return 0; \
}

PE_FUNC(discount_wait_time)
#endif /* CONFIG_SCHEDUTIL_USE_TL */

#ifdef CONFIG_UCLAMP_TASK_GROUP
static void cpu_util_update_eff(struct cgroup_subsys_state *css)
{
	struct cgroup_subsys_state *top_css = css;
	struct uclamp_se *uc_se = NULL;
	unsigned int eff[UCLAMP_CNT];
	enum uclamp_id clamp_id;
	unsigned int clamps;

	css_for_each_descendant_pre(css, top_css) {

		for_each_clamp_id(clamp_id) {
			/* Assume effective clamps matches requested clamps */
			eff[clamp_id] = css_st(css)->uclamp_req[clamp_id].value;
		}
		/* Ensure protection is always capped by limit */
		eff[UCLAMP_MIN] = min(eff[UCLAMP_MIN], eff[UCLAMP_MAX]);

		/* Propagate most restrictive effective clamps */
		clamps = 0x0;
		uc_se = css_st(css)->uclamp;
		for_each_clamp_id(clamp_id) {
			if (eff[clamp_id] == uc_se[clamp_id].value)
				continue;
			uc_se[clamp_id].value = eff[clamp_id];
			uc_se[clamp_id].bucket_id =
				uclamp_bucket_id(eff[clamp_id]);
			clamps |= (0x1 << clamp_id);
		}
		if (!clamps) {
			css = css_rightmost_descendant(css);
			continue;
		}

		/* Immediately update descendants RUNNABLE tasks */
		uclamp_update_active_tasks(css, clamps);
	}
}

void uclamp_update_root_st(void)
{
	struct schedtune *st = &root_schedtune;

	uclamp_se_set(&st->uclamp_req[UCLAMP_MIN],
		      sysctl_sched_uclamp_util_min, false);
	uclamp_se_set(&st->uclamp_req[UCLAMP_MAX],
		      sysctl_sched_uclamp_util_max, false);

	rcu_read_lock();
	cpu_util_update_eff(&root_schedtune.css);
	rcu_read_unlock();
}
/*
 * Integer 10^N with a given N exponent by casting to integer the literal "1eN"
 * C expression. Since there is no way to convert a macro argument (N) into a
 * character constant, use two levels of macros.
 */
#define _POW10(exp) ((unsigned int)1e##exp)
#define POW10(exp) _POW10(exp)

struct uclamp_request {
#define UCLAMP_PERCENT_SHIFT	2
#define UCLAMP_PERCENT_SCALE	(100 * POW10(UCLAMP_PERCENT_SHIFT))
	s64 percent;
	u64 util;
	int ret;
};

static inline struct uclamp_request
capacity_from_percent(char *buf)
{
	struct uclamp_request req = {
		.percent = UCLAMP_PERCENT_SCALE,
		.util = SCHED_CAPACITY_SCALE,
		.ret = 0,
	};

	buf = strim(buf);
	if (strcmp(buf, "max")) {
		req.ret = cgroup_parse_float(buf, UCLAMP_PERCENT_SHIFT,
					     &req.percent);
		if (req.ret)
			return req;
		if (req.percent > UCLAMP_PERCENT_SCALE) {
			req.ret = -ERANGE;
			return req;
		}

		req.util = req.percent << SCHED_CAPACITY_SHIFT;
		req.util =
			DIV_ROUND_CLOSEST_ULL(req.util, UCLAMP_PERCENT_SCALE);
	}

	return req;
}

static ssize_t cpu_uclamp_write(struct kernfs_open_file *of, char *buf,
				size_t nbytes, loff_t off,
				enum uclamp_id clamp_id)
{
	struct uclamp_request req;
	struct schedtune *st;

	req = capacity_from_percent(buf);
	if (req.ret)
		return req.ret;

	mutex_lock(&uclamp_mutex);
	rcu_read_lock();

	st = css_st(of_css(of));
	if (st->uclamp_req[clamp_id].value != req.util)
		uclamp_se_set(&st->uclamp_req[clamp_id], req.util, false);

	/*
	 * Because of not recoverable conversion rounding we keep track of the
	 * exact requested value
	 */
	st->uclamp_pct[clamp_id] = req.percent;

	/* Update effective clamps to track the most restrictive value */
	cpu_util_update_eff(of_css(of));

	rcu_read_unlock();
	mutex_unlock(&uclamp_mutex);

	return nbytes;
}

static ssize_t cpu_uclamp_min_write(struct kernfs_open_file *of,
				    char *buf, size_t nbytes,
				    loff_t off)
{
	return cpu_uclamp_write(of, buf, nbytes, off, UCLAMP_MIN);
}

static ssize_t cpu_uclamp_max_write(struct kernfs_open_file *of,
				    char *buf, size_t nbytes,
				    loff_t off)
{
	return cpu_uclamp_write(of, buf, nbytes, off, UCLAMP_MAX);
}

static inline void cpu_uclamp_print(struct seq_file *sf,
				    enum uclamp_id clamp_id)
{
	struct schedtune *st;
	u64 util_clamp;
	u64 percent;
	u32 rem;

	rcu_read_lock();
	st = css_st(seq_css(sf));
	util_clamp = st->uclamp_req[clamp_id].value;
	rcu_read_unlock();

	if (util_clamp == SCHED_CAPACITY_SCALE) {
		seq_puts(sf, "max\n");
		return;
	}

	percent = st->uclamp_pct[clamp_id];
	percent = div_u64_rem(percent, POW10(UCLAMP_PERCENT_SHIFT), &rem);
	seq_printf(sf, "%llu.%0*u\n", percent, UCLAMP_PERCENT_SHIFT, rem);
}

static int cpu_uclamp_min_show(struct seq_file *sf, void *v)
{
	cpu_uclamp_print(sf, UCLAMP_MIN);
	return 0;
}

static int cpu_uclamp_max_show(struct seq_file *sf, void *v)
{
	cpu_uclamp_print(sf, UCLAMP_MAX);
	return 0;
}
#endif

static struct cftype files[] = {
	{
		.name = "boost",
		.read_s64 = boost_read,
		.write_s64 = boost_write,
	},
	{
		.name = "prefer_idle",
		.read_u64 = prefer_idle_read,
		.write_u64 = prefer_idle_write,
	},
#ifdef CONFIG_SCHEDUTIL_USE_TL
	{
		.name = "window_policy",
		.read_u64 = window_policy_read,
		.write_u64 = window_policy_write,
	},
	{
		.name = "discount_wait_time",
		.read_u64 = discount_wait_time_read,
		.write_u64 = discount_wait_time_write,
	},
#endif /* CONFIG_SCHEDUTIL_USE_TL */
#ifdef CONFIG_UCLAMP_TASK_GROUP
	{
		.name = "uclamp.min",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = cpu_uclamp_min_show,
		.write = cpu_uclamp_min_write,
	},
	{
		.name = "uclamp.max",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = cpu_uclamp_max_show,
		.write = cpu_uclamp_max_write,
	},
#endif
	{ }	/* terminate */
};

static void
schedtune_boostgroup_init(struct schedtune *st, int idx)
{
	struct boost_groups *bg;
	int cpu;

	/* Initialize per CPUs boost group support */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		bg->group[idx].boost = 0;
		bg->group[idx].valid = true;
		bg->group[idx].ts = 0;
	}

	/* Keep track of allocated boost groups */
	allocated_group[idx] = st;
	st->idx = idx;
}


static struct cgroup_subsys_state *
schedtune_css_alloc(struct cgroup_subsys_state *parent_css)
{
#ifdef CONFIG_UCLAMP_TASK_GROUP
	struct schedtune *parent = parent_css ? css_st(parent_css) : NULL;
#endif
	struct schedtune *st;
	int idx;

	if (!parent_css)
		return &root_schedtune.css;

	/* Allow only single level hierachies */
	if (parent_css != &root_schedtune.css) {
		pr_err("Nested SchedTune boosting groups not allowed\n");
		return ERR_PTR(-ENOMEM);
	}

	/* Allow only a limited number of boosting groups */
	for (idx = 1; idx < BOOSTGROUPS_COUNT; ++idx)
		if (!allocated_group[idx])
			break;
	if (idx == BOOSTGROUPS_COUNT) {
		pr_err("Trying to create more than %d SchedTune boosting groups\n",
		       BOOSTGROUPS_COUNT);
		return ERR_PTR(-ENOSPC);
	}

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto out;

#ifdef CONFIG_UCLAMP_TASK_GROUP
	alloc_uclamp_sched_group(st, parent);
#endif

	/* Initialize per CPUs boost group support */
	schedtune_boostgroup_init(st, idx);

	return &st->css;

out:
	return ERR_PTR(-ENOMEM);
}

static void
schedtune_boostgroup_release(struct schedtune *st)
{
	struct boost_groups *bg;
	int cpu;

	/* Reset per CPUs boost group support */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		bg->group[st->idx].valid = false;
		bg->group[st->idx].boost = 0;
	}

	/* Keep track of allocated boost groups */
	allocated_group[st->idx] = NULL;
}

static void
schedtune_css_free(struct cgroup_subsys_state *css)
{
	struct schedtune *st = css_st(css);

	/* Release per CPUs boost group support */
	schedtune_boostgroup_release(st);
	kfree(st);
}

struct cgroup_subsys schedtune_cgrp_subsys = {
	.css_alloc	= schedtune_css_alloc,
	.css_free	= schedtune_css_free,
	.can_attach     = schedtune_can_attach,
	.cancel_attach  = schedtune_cancel_attach,
	.legacy_cftypes	= files,
	.early_init	= 1,
};

static inline void
schedtune_init_cgroups(void)
{
	struct boost_groups *bg;
	int cpu;

	/* Initialize the per CPU boost groups */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		memset(bg, 0, sizeof(struct boost_groups));
		bg->group[0].valid = true;
		raw_spin_lock_init(&bg->lock);
	}

	pr_info("schedtune: configured to support %d boost groups\n",
		BOOSTGROUPS_COUNT);

	schedtune_initialized = true;
}

#ifdef CONFIG_SCHED_TUNE
int prefer_idle_for_perf_idx(int idx, int prefer_idle)
{
	struct schedtune *ct = NULL;

	if (!is_group_idx_valid(idx))
		return -ERANGE;

	ct = allocated_group[idx];

	if (!ct)
		return -EINVAL;

	rcu_read_lock();
	ct->prefer_idle = prefer_idle;
	rcu_read_unlock();
	return 0;
}
EXPORT_SYMBOL(prefer_idle_for_perf_idx);
#endif

#if defined(CONFIG_UCLAMP_TASK_GROUP) && defined(CONFIG_SCHED_TUNE)
int uclamp_min_for_perf_idx(int idx, int min_value)
{
	struct schedtune *st;
	struct cgroup_subsys_state *css;

	s64 percent = min_value * UCLAMP_PERCENT_SCALE;

	if (min_value > SCHED_CAPACITY_SCALE)
		return -ERANGE;

	if (!is_group_idx_valid(idx))
		return -ERANGE;

	st = allocated_group[idx];
	if (!st)
		return -EINVAL;

	css = &st->css;

	mutex_lock(&uclamp_mutex);
	rcu_read_lock();

	if (st->uclamp_req[UCLAMP_MIN].value != min_value)
		uclamp_se_set(&st->uclamp_req[UCLAMP_MIN], min_value, false);

	st->uclamp_pct[UCLAMP_MIN] = percent >> SCHED_CAPACITY_SHIFT;
	cpu_util_update_eff(css);

	rcu_read_unlock();
	mutex_unlock(&uclamp_mutex);

	return 0;

}
EXPORT_SYMBOL(uclamp_min_for_perf_idx);

int uclamp_min_pct_for_perf_idx(int idx, int pct)
{
	unsigned int min_value;

	if (pct < 0 || pct > 100)
		return -ERANGE;

	min_value = scale_from_percent(pct);
	return uclamp_min_for_perf_idx(idx, min_value);
}
EXPORT_SYMBOL(uclamp_min_pct_for_perf_idx);
#endif

/*
 * Initialize the cgroup structures
 */
static int
schedtune_init(void)
{
	schedtune_spc_rdiv = reciprocal_value(100);
	schedtune_init_cgroups();
	return 0;
}
postcore_initcall(schedtune_init);
