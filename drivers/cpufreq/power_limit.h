#ifndef _POWER_LIMIT_H_
#define _POWER_LIMIT_H_

#define LIMITER_OFF 3
#define LIMITER_H 2
#define LIMITER_M 1
#define LIMITER_L 0
#define POWER_MODE 0 
#define BALANCE_MODE 1
#define PERF_MODE 2

static struct kobject *power_limiter_kobj;
static struct ppm_limit_data *freq_to_set;
static struct ppm_limit_data *user_freq;
static struct pm_qos_request pm_qos_req;
static struct pm_qos_request pm_qos_ddr_req;
//flags
static bool enable;
static bool gov_ctl;
static bool run_state = 0;
static bool freq_held = false;

static int cluster_num;
static int perf_mode = 1;
static int prev_cpu_wall = 0, prev_cpu_idle = 0;
static int boost_threshold = 90, boost_duration = 2000;
static unsigned int nap_time_ms = 500;
static int cpu_num[3] = {0, 4, 7};
static int power_profile = 1; //Power saver profile 0-Low 1-Mid 2-High 3-Off
static unsigned int freq_to_hold[3][3] = {  {1725000, 1350000, 1075000}, //Little
                                            {1537000, 1335000, 902000}, //Big
                                            {1998000, 1258000, 659000}}; //Prime
static unsigned int freq_profiles[3][3] = { {1525000, 1451000, 659000},
                                            {2000000, 2200000, 1632000},
                                            {2000000, 2507000, 2463000}};
                                            
extern void policy_apply_limits(void);
static struct task_struct *lc_thread, *timer_thread;
void boost_wq(struct work_struct *work);
static struct hrtimer boosthold_timer;

#endif