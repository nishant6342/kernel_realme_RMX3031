#define LIMITER_OFF 3
#define LIMITER_H 2
#define LIMITER_M 1
#define LIMITER_L 0

static struct kobject *power_limiter_kobj;
static struct ppm_limit_data *freq_to_set;
static struct ppm_limit_data *user_freq;
static struct pm_qos_request pm_qos_req;
static struct pm_qos_request pm_qos_ddr_req;
static int cluster_num;
static bool enable;
static bool freq_held = false;
static int cpu_num[3] = {0, 4, 7};
static int power_profile = 1; //Power saver profile 0-Low 1-Mid 2-High 3-Off
static unsigned int freq_to_hold[3][3] = {  {1725000, 1350000, 1075000}, //Little
                                            {1537000, 1335000, 902000}, //Big
                                            {1998000, 1258000, 659000}}; //Prime
                                            
extern void policy_apply_limits(void);
