/**
 * @file    power_limit.c
 * @author  Tejas udupa
 * @date    12 May 2022
 * @version 0.1
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
#define LIMITER_OFF 3
#define LIMITER_H 2
#define LIMITER_M 1
#define LIMITER_L 0

static struct kobject *power_limiter_kobj;
static struct ppm_limit_data *freq_to_set;
static struct pm_qos_request pm_qos_req;
static struct pm_qos_request pm_qos_ddr_req;
static int cluster_num;
static int power_profile = 1; //Power saver profile 0-Low 1-Mid 2-High 3-Off
static unsigned int freq_to_hold[3][3] = {  {1725000, 1525000, 1350000},
                                            {2200000, 1451000, 1162000},
                                            {2600000, 1482000, 1108000}};

static int freq_hold(void)
{
    int i,retval;

    if(power_profile >= LIMITER_OFF)
        return 0;

    for (i = 0; i < cluster_num; i++) {
        freq_to_set[i].min = -1;
        freq_to_set[i].max = freq_to_hold[i][power_profile];
    }
    pr_debug("%s: cluster:%d freq:%d\n",__func__,cluster_num,freq_to_set);
    retval = mt_ppm_userlimit_cpu_freq(cluster_num, freq_to_set);

    return retval;
}

static int freq_release(void)
{
    int i,retval;

    for (i = 0; i < cluster_num; i++) {
        freq_to_set[i].min = -1;
        freq_to_set[i].max = -1;
    }
    pr_debug("%s: cluster:%d freq:%d\n",__func__,cluster_num,freq_to_set);
    retval = mt_ppm_userlimit_cpu_freq(cluster_num, freq_to_set);

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
            if(power_profile == LIMITER_H){
                core_release();
                vcorefs_release();
            }
            break;
        /* LCM OFF */
        case FB_BLANK_POWERDOWN:
            freq_hold();
            if(power_profile == LIMITER_H){
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

static struct kobj_attribute pwr_mode =
    __ATTR(power_profile, 0644, pwr_prof_show, pwr_prof_store);

/*********************************** sysfs end ***********************************/

static int __init freq_init(void)
{
    int rc = 0;
    cluster_num = arch_get_nr_clusters();

    freq_to_set = kcalloc(cluster_num,
                sizeof(struct ppm_limit_data), GFP_KERNEL);

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
    pm_qos_add_request(&pm_qos_req, PM_QOS_CPU_DMA_LATENCY,
		PM_QOS_DEFAULT_VALUE);

    pm_qos_add_request(&pm_qos_ddr_req, PM_QOS_DDR_OPP,
		PM_QOS_DDR_OPP_DEFAULT_VALUE);
out:
    return rc;
}

static void __exit freq_end(void)
{
    fb_unregister_client(&fb_notiy);
    kfree(freq_to_set);
}

module_init(freq_init);
module_exit(freq_end);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("trax85");
MODULE_DESCRIPTION("CPU Power Limiter Driver for MTK");
