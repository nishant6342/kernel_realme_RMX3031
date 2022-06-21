/**
 * @file    power_limit.c
 * @author  Tejas udupa
 * @date    12 May 2022
 * 
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
#include "helio-dvfsrc-opp.h"
#include "mtk_ppm_api.h"
#include "mtk_perfmgr_internal.h"
#include "power_limit.h"

static bool init_done = 0;

void policy_apply_limits(void)
{    
    struct cpufreq_policy *policy;
    int i;

    if(!init_done || freq_held)
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

static struct kobj_attribute pwr_mode =
    __ATTR(power_profile, 0644, pwr_prof_show, pwr_prof_store);
static struct kobj_attribute enable_limiter =
    __ATTR(enable, 0644, enable_show, enable_store);

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
    pm_qos_add_request(&pm_qos_req, PM_QOS_CPU_DMA_LATENCY,
		PM_QOS_DEFAULT_VALUE);

    pm_qos_add_request(&pm_qos_ddr_req, PM_QOS_DDR_OPP,
		PM_QOS_DDR_OPP_DEFAULT_VALUE);
    enable = 1;
    init_done = 1;
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
