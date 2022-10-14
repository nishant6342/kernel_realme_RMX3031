/**
 * @file    power_limit.c
 * @author  Tejas udupa
 * @date    12 May 2022
 *
 * @version 1.3 - Add Game Boost functionalities
 *              This adds game boost threshold, profile and auto throttle
 * @version 1.2 - Fix frequency values being held even after suspend by saving
 *              values in user_freq array struct and checking for freq_held.
 *              apply user_freq values on release.
 *
 * @version 1.1 - add policy_apply_limits() function which can be used
 *              to clamp max, min for current policy
 *
 * @brief  Frequency control driver, clamps max frequency
 * to profile selected when display is suspended, and
 * releases the clamp when out of suspend. it uses
 * mtk proprietry ppm api to set max/min Frequency.
 * It requires usb_boost to be turned off else our driver would
 * be fighting for control with usb_boost during usb connect
 * while suspended.(Also i dont see the use of usb_boost on
 * mt6893 anyways so better turn it off).
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/pm_qos.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include "helio-dvfsrc-opp.h"
#include "mtk_ppm_api.h"
#include "mtk_perfmgr_internal.h"
#include "power_limit.h"

static bool init_done = 0;
/*Creating work by Static Method */
DECLARE_WORK(workqueue,boost_wq);

void policy_apply_limits(void)
{
    struct cpufreq_policy *policy;
    int i;

    if(!init_done || freq_held || !gov_ctl)
        return;

    for (i = 0; i < cluster_num; i++) {
        policy = cpufreq_cpu_get(cpu_num[i]);
        if(!policy)
            continue;
        user_freq[i].min = policy->min;
        user_freq[i].max = policy->max;
        pr_debug("%s: cluster:%d max:%d min:%d bool:%d\n",__func__, i,
            policy->max, policy->min, freq_held);
    }
    mt_ppm_userlimit_cpu_freq(cluster_num, user_freq);
}

static int freq_hold(void)
{
    int i,retval;

    for (i = 0; i < cluster_num; i++) {
        freq_to_set[i].min = user_freq[i].min;
        freq_to_set[i].max = freq_to_hold[i][power_profile];
        pr_debug("%s: cluster:%d max:%d min:%d saved max:%d\n",__func__, i,
            freq_to_set[i].max, freq_to_set[i].min, user_freq[i].max);
    }

    retval = mt_ppm_userlimit_cpu_freq(cluster_num, freq_to_set);
    /* prevent any and all policy limits updates when freq is held or
     * device is in sleep */
    freq_held = true;

    return retval;
}

static int freq_release(void)
{
    int i,retval;

    for (i = 0; i < cluster_num; i++) {
        freq_to_set[i].min = user_freq[i].min;
        freq_to_set[i].max = user_freq[i].max;
        pr_debug("%s: cluster:%d max:%d min:%d\n",__func__, i,
            user_freq[i].max, user_freq[i].min);
    }

    retval = mt_ppm_userlimit_cpu_freq(cluster_num, freq_to_set);
    freq_held = false;

    return retval;
}

static int core_hold(void)
{
	pm_qos_update_request(&pm_qos_req, 0);
	return 0;
}

static int core_release(void)
{
	pm_qos_update_request(&pm_qos_req, PM_QOS_DEFAULT_VALUE);
	return 0;
}

static int vcorefs_hold(void)
{
	pm_qos_update_request(&pm_qos_ddr_req, DDR_OPP_7);
	return 0;
}

static int vcorefs_release(void)
{
	pm_qos_update_request(&pm_qos_ddr_req, DDR_OPP_UNREQ);
	return 0;
}

/*
 * Game Boost functions
 */
static int set_freq_arr(unsigned int prof_arr[])
{
    int i,retval;

    for (i = 0; i < cluster_num; i++) {
        freq_to_set[i].min = -1;
        freq_to_set[i].max = prof_arr[i];
        pr_debug("%s: cluster:%d max:%d min:%d saved max:%d\n",__func__, i,
            freq_to_set[i].max, freq_to_set[i].min, user_freq[i].max);
    }

    retval = mt_ppm_userlimit_cpu_freq(cluster_num, freq_to_set);
    /* prevent any and all policy limits updates when freq is held or
     * device is in sleep */
    freq_held = true;

    return retval;
}

static int core_hold_boost(void)
{
    pm_qos_update_request(&pm_qos_req, 50);
    return 0;
}

static int vcorefs_hold_boost(void)
{
    pm_qos_update_request(&pm_qos_ddr_req, DDR_OPP_0);
    return 0;
}

enum hrtimer_restart uboost_callback(struct hrtimer *timer)
{
    core_release();
    vcorefs_release();
    set_freq_arr(freq_profiles[perf_mode]);
    nap_time_ms = 500;
    return HRTIMER_RESTART;
}

void boost_wq(struct work_struct *work)
{
    unsigned int boost_freq[] = {2000000, 2600000, 3000000};
    //Boost all frequencies
    set_freq_arr(boost_freq);
    core_hold_boost();
    vcorefs_hold_boost();
    //Hold boost for fixed duration
    hrtimer_start(&boosthold_timer, ms_to_ktime(boost_duration),
        HRTIMER_MODE_REL);
}

/*
 * update_load():- gives current load on the respective cpu core.
 */
static u64 update_load(int cpu)
{
    u64 cur_wall_time;
    u64 cur_idle_time;
    int io_busy = 0;
    unsigned int wall_time, idle_time, load = 0;

    cur_idle_time = get_cpu_idle_time(cpu, &cur_wall_time, io_busy);
    wall_time = (unsigned int)
            (cur_wall_time - prev_cpu_wall);
    idle_time = (unsigned int)
            (cur_idle_time - prev_cpu_idle);

    prev_cpu_wall = cur_wall_time;
    prev_cpu_idle = cur_idle_time;
    load = 100 * (wall_time - idle_time) / wall_time;

    return load;
}

static int load_chk_thread(void *data)
{
    int load, prime_core = 6;

    while(!kthread_should_stop())
    {
        set_current_state(TASK_RUNNING);
        if(!run_state){
            set_current_state(TASK_INTERRUPTIBLE);
            schedule();
        }
        load = update_load(prime_core);
        if(load >= boost_threshold){
            schedule_work(&workqueue);
            nap_time_ms = boost_duration * 2;
        }

        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(msecs_to_jiffies(nap_time_ms));
    }
    return 0;
}

static void set_mode(int profile_mode)
{
    switch(profile_mode)
    {
        case POWER_MODE:
        case BALANCE_MODE:
                set_freq_arr(freq_profiles[profile_mode]);
                gov_ctl = 0;    //disable governor control
                run_state = 0;  //disable load check
                break;
        case PERF_MODE:
                set_freq_arr(freq_profiles[profile_mode]);
                gov_ctl = 0;
                run_state = 1;
                wake_up_process(lc_thread);
                break;
        case 3:
                run_state = 0;
                gov_ctl = 1;
        default:break;
    }
}

static int fb_action(struct notifier_block *self,
    unsigned long event, void *data)
{
    struct fb_event *evdata = data;
    int blank;

    /* skip if it's not a blank event */
    if (event != FB_EVENT_BLANK)
        return 0;

    blank = *(int *)evdata->data;
    switch (blank) {
        /* LCM ON */
        case FB_BLANK_UNBLANK:
            freq_release();
    		core_release();
    		vcorefs_release();
            break;
        /* LCM OFF */
        case FB_BLANK_POWERDOWN:
            if(enable){
                freq_hold();
                core_hold();
                vcorefs_hold();
            }
            break;
        default:
            break;
    }

    return 0;
}

static struct notifier_block fb_notiy = {
    .notifier_call = fb_action,
};

//********************************* sysfs interface ********************************//
static ssize_t pwr_prof_show(struct kobject *kobj, struct kobj_attribute *attr,
                  char *buf)
{
    return sprintf(buf,"%d\n", power_profile);
}
static ssize_t pwr_prof_store(struct kobject *kobj, struct kobj_attribute *attr,
                  const char *buf, size_t count)
{
    int temp;

    sscanf(buf,"%d", &temp);
    if(temp < 0)
        return count;
    power_profile = temp;

    return count;
}

static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr,
                  char *buf)
{
    return sprintf(buf,"%d\n", enable);
}
static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr,
                  const char *buf, size_t count)
{
    sscanf(buf,"%d", &enable);
    return count;
}

static ssize_t gov_ctl_show(struct kobject *kobj, struct kobj_attribute *attr,
                  char *buf)
{
    return sprintf(buf,"governor cpufreq control:%d\n", gov_ctl);
}
static ssize_t gov_ctl_store(struct kobject *kobj, struct kobj_attribute *attr,
                  const char *buf, size_t count)
{
    sscanf(buf,"%d", &perf_mode);
    return count;
}
static ssize_t mode_show(struct kobject *kobj, struct kobj_attribute *attr,
                  char *buf)
{
    char* arr[] = {"Power Mode",
                    "Balance Mode",
                    "Performance Mode", "Manual Mode"};
    return sprintf(buf,"%s\n", arr[perf_mode]);
}
static ssize_t mode_store(struct kobject *kobj, struct kobj_attribute *attr,
                  const char *buf, size_t count)
{
    sscanf(buf,"%d", &perf_mode);
    set_mode(perf_mode);
    return count;
}
static ssize_t bst_time_show(struct kobject *kobj, struct kobj_attribute *attr,
                  char *buf)
{
    return sprintf(buf,"%d\n", boost_duration);
}
static ssize_t bst_time_store(struct kobject *kobj, struct kobj_attribute *attr,
                  const char *buf, size_t count)
{
    sscanf(buf,"%d", &boost_duration);
    return count;
}
static ssize_t bst_thresh_show(struct kobject *kobj, struct kobj_attribute *attr,
                  char *buf)
{
    return sprintf(buf,"%d\n", boost_threshold);
}
static ssize_t bst_thresh_store(struct kobject *kobj, struct kobj_attribute *attr,
                  const char *buf, size_t count)
{
    sscanf(buf,"%d", &boost_threshold);
    return count;
}

static struct kobj_attribute pwr_mode =
    __ATTR(power_profile, 0644, pwr_prof_show, pwr_prof_store);
static struct kobj_attribute enable_limiter =
    __ATTR(enable, 0644, enable_show, enable_store);
static struct kobj_attribute enable_gov_ctl =
    __ATTR(governor_ctl, 0644, gov_ctl_show, gov_ctl_store);
static struct kobj_attribute profile_mode =
    __ATTR(profile_mode, 0644, mode_show, mode_store);
static struct kobj_attribute bst_time =
    __ATTR(boost_duration, 0644, bst_time_show, bst_time_store);
static struct kobj_attribute bst_thresh =
    __ATTR(boost_threshold, 0644, bst_thresh_show, bst_thresh_store);

/*********************************** sysfs end ***********************************/

static int __init freq_init(void)
{
    struct cpufreq_policy *policy;
    int rc = 0, i;

    cluster_num = arch_get_nr_clusters();

    freq_to_set = kcalloc(cluster_num,
                sizeof(struct ppm_limit_data), GFP_KERNEL);
    user_freq = kcalloc(cluster_num,
                sizeof(struct ppm_limit_data), GFP_KERNEL);

    for(i = 0; i < cluster_num; i++){
        policy = cpufreq_cpu_get(cpu_num[i]);
        if(!policy){
            user_freq[i].min = -1;
            user_freq[i].max = -1;
            continue;
        }
        user_freq[i].min = policy->min;
        user_freq[i].max = policy->max;
    }

    if (!freq_to_set) {
        pr_err("kcalloc freq_to_set fail\n");
        rc = -ENOMEM;
        goto out;
    }

    if (fb_register_client(&fb_notiy)) {
        pr_err("%s: fb register failed\n");
        rc = -EINVAL;
        goto out;
    }

    power_limiter_kobj = kobject_create_and_add("power_limiter", kernel_kobj) ;
    if (!power_limiter_kobj) {
        pr_err(KERN_WARNING "%s: power_limiter create_and_add failed\n", __func__);
        goto out;
    }
    rc = sysfs_create_file(power_limiter_kobj, &pwr_mode.attr);
    if (rc) {
        pr_err(KERN_WARNING "%s: sysfs_create_file failed for power_limiter\n", __func__);
        rc = -1;
        goto out;
    }
    rc = sysfs_create_file(power_limiter_kobj, &enable_limiter.attr);
    if (rc) {
        pr_err(KERN_WARNING "%s: sysfs_create_file failed for power_limiter\n", __func__);
        rc = -1;
        goto out;
    }
    rc = sysfs_create_file(power_limiter_kobj, &enable_gov_ctl.attr);
    if (rc) {
        pr_err(KERN_WARNING "%s: sysfs_create_file failed for power_limiter\n", __func__);
        rc = -1;
        goto out;
    }
    rc = sysfs_create_file(power_limiter_kobj, &profile_mode.attr);
    if (rc) {
        pr_err(KERN_WARNING "%s: sysfs_create_file failed for power_limiter\n", __func__);
        rc = -1;
        goto out;
    }
    rc = sysfs_create_file(power_limiter_kobj, &bst_time.attr);
    if (rc) {
        pr_err(KERN_WARNING "%s: sysfs_create_file failed for power_limiter\n", __func__);
        rc = -1;
        goto out;
    }
    rc = sysfs_create_file(power_limiter_kobj, &bst_thresh.attr);
    if (rc) {
        pr_err(KERN_WARNING "%s: sysfs_create_file failed for power_limiter\n", __func__);
        rc = -1;
        goto out;
    }

    lc_thread = kthread_create(load_chk_thread,NULL,"loadchk thread");
    if(!lc_thread)
        pr_debug(KERN_ERR "%s:failed to create kthread\n",__func__);

    hrtimer_init(&boosthold_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    boosthold_timer.function = &uboost_callback;

    pm_qos_add_request(&pm_qos_req, PM_QOS_CPU_DMA_LATENCY,
		PM_QOS_DEFAULT_VALUE);

    pm_qos_add_request(&pm_qos_ddr_req, PM_QOS_DDR_OPP,
		PM_QOS_DDR_OPP_DEFAULT_VALUE);

    //set flags
    enable = 1;
    init_done = 1;
    gov_ctl = 1;
out:
    return rc;
}

static void __exit freq_end(void)
{
    fb_unregister_client(&fb_notiy);
    kfree(freq_to_set);
}

late_initcall(freq_init);
module_exit(freq_end);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("trax85");
MODULE_DESCRIPTION("CPU Power Limiter Driver for MTK");
