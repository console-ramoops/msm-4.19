// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Oplus. All rights reserved.
 *
 * Implements the OPlus Theia and Stability telemetry interfaces
 * (/proc/theiaPwkReport, /proc/pwkMonitorParam, /proc/sau_pwknum,
 * /proc/shutdown_detect, /proc/opbootfrom, /proc/phoenix, /proc/blackSwitch,
 * /proc/brightSwitch, /proc/force_reboot, and /proc/oplusVersion sub-nodes) required by
 * vendor.oplus.hardware.stability.oplus_project-V1-service.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/reboot.h>

static struct proc_dir_entry *oplus_version_dir;

/* Theia Power Key telemetry state */
static unsigned int pwk_event_val;
static unsigned int pwk_press_cnt_val;
static unsigned int pwk_release_cnt_val;
static unsigned int pwk_long_press_cnt_val;
static unsigned int pwk_hang_cnt_val;
static unsigned int pwk_threshold_ms_val = 3000;
static int pwk_monitor_enable_val = 1;
static unsigned int sau_pwknum_val;
static int shutdown_detect_val;
static unsigned long phoenix_heartbeat;
static DEFINE_SPINLOCK(theia_lock);

/* /proc/theiaPwkReport */
static int theia_pwk_report_show(struct seq_file *m, void *v)
{
	unsigned long flags;

	spin_lock_irqsave(&theia_lock, flags);
	seq_printf(m,
		   "pwk_event: %u\n"
		   "pwk_press_cnt: %u\n"
		   "pwk_release_cnt: %u\n"
		   "pwk_long_press_cnt: %u\n"
		   "pwk_hang_cnt: %u\n"
		   "status: normal\n",
		   pwk_event_val,
		   pwk_press_cnt_val,
		   pwk_release_cnt_val,
		   pwk_long_press_cnt_val,
		   pwk_hang_cnt_val);
	spin_unlock_irqrestore(&theia_lock, flags);
	return 0;
}

static int theia_pwk_report_open(struct inode *inode, struct file *file)
{
	return single_open(file, theia_pwk_report_show, NULL);
}

static ssize_t theia_pwk_report_write(struct file *filp, const char __user *ubuf,
				      size_t cnt, loff_t *ppos)
{
	char kbuf[32];
	unsigned long flags;

	if (cnt >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, ubuf, cnt))
		return -EFAULT;
	kbuf[cnt] = '\0';

	spin_lock_irqsave(&theia_lock, flags);
	if (sysfs_streq(kbuf, "0") || sysfs_streq(kbuf, "reset"))
		pwk_event_val = 0;
	spin_unlock_irqrestore(&theia_lock, flags);

	return cnt;
}

static const struct file_operations theia_pwk_report_fops = {
	.open    = theia_pwk_report_open,
	.read    = seq_read,
	.write   = theia_pwk_report_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/pwkMonitorParam */
static int pwk_monitor_param_show(struct seq_file *m, void *v)
{
	unsigned long flags;

	spin_lock_irqsave(&theia_lock, flags);
	seq_printf(m,
		   "pwk_threshold_ms: %u\n"
		   "pwk_monitor_enable: %d\n",
		   pwk_threshold_ms_val,
		   pwk_monitor_enable_val);
	spin_unlock_irqrestore(&theia_lock, flags);
	return 0;
}

static int pwk_monitor_param_open(struct inode *inode, struct file *file)
{
	return single_open(file, pwk_monitor_param_show, NULL);
}

static ssize_t pwk_monitor_param_write(struct file *filp, const char __user *ubuf,
				       size_t cnt, loff_t *ppos)
{
	char kbuf[32];
	unsigned int val;
	unsigned long flags;

	if (cnt >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, ubuf, cnt))
		return -EFAULT;
	kbuf[cnt] = '\0';

	if (kstrtouint(strstrip(kbuf), 0, &val))
		return -EINVAL;

	spin_lock_irqsave(&theia_lock, flags);
	pwk_threshold_ms_val = val;
	spin_unlock_irqrestore(&theia_lock, flags);

	return cnt;
}

static const struct file_operations pwk_monitor_param_fops = {
	.open    = pwk_monitor_param_open,
	.read    = seq_read,
	.write   = pwk_monitor_param_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/sau_pwknum */
static int sau_pwknum_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%u\n", sau_pwknum_val);
	return 0;
}

static int sau_pwknum_open(struct inode *inode, struct file *file)
{
	return single_open(file, sau_pwknum_show, NULL);
}

static ssize_t sau_pwknum_write(struct file *filp, const char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
	char kbuf[16];
	unsigned int val;

	if (cnt >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, ubuf, cnt))
		return -EFAULT;
	kbuf[cnt] = '\0';

	if (kstrtouint(strstrip(kbuf), 0, &val))
		return -EINVAL;

	sau_pwknum_val = val;
	return cnt;
}

static const struct file_operations sau_pwknum_fops = {
	.open    = sau_pwknum_open,
	.read    = seq_read,
	.write   = sau_pwknum_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/shutdown_detect */
static int shutdown_detect_show(struct seq_file *m, void *v)
{
	seq_printf(m, "shutdown_detect: %d\n", shutdown_detect_val);
	return 0;
}

static int shutdown_detect_open(struct inode *inode, struct file *file)
{
	return single_open(file, shutdown_detect_show, NULL);
}

static ssize_t shutdown_detect_write(struct file *filp, const char __user *ubuf,
				     size_t cnt, loff_t *ppos)
{
	char kbuf[16];
	int val;

	if (cnt >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, ubuf, cnt))
		return -EFAULT;
	kbuf[cnt] = '\0';

	if (kstrtoint(strstrip(kbuf), 0, &val))
		return -EINVAL;

	shutdown_detect_val = val;
	return cnt;
}

static const struct file_operations shutdown_detect_fops = {
	.open    = shutdown_detect_open,
	.read    = seq_read,
	.write   = shutdown_detect_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/opbootfrom */
static int opbootfrom_show(struct seq_file *m, void *v)
{
	seq_puts(m, "boot_from: normal\n");
	return 0;
}

static int opbootfrom_open(struct inode *inode, struct file *file)
{
	return single_open(file, opbootfrom_show, NULL);
}

static const struct file_operations opbootfrom_fops = {
	.open    = opbootfrom_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/phoenix */
static int phoenix_show(struct seq_file *m, void *v)
{
	seq_printf(m, "phoenix_timestamp: %lu\n", phoenix_heartbeat);
	return 0;
}

static int phoenix_open(struct inode *inode, struct file *file)
{
	return single_open(file, phoenix_show, NULL);
}

static ssize_t phoenix_write(struct file *filp, const char __user *ubuf,
			     size_t cnt, loff_t *ppos)
{
	phoenix_heartbeat = jiffies;
	return cnt;
}

static const struct file_operations phoenix_fops = {
	.open    = phoenix_open,
	.read    = seq_read,
	.write   = phoenix_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Generic integer proc helper for switch nodes */
#define DEFINE_THEIA_INT_NODE(name, default_val)			\
static int name##_val = default_val;					\
static int name##_show(struct seq_file *m, void *v)			\
{									\
	seq_printf(m, "%d\n", name##_val);				\
	return 0;							\
}									\
static int name##_open(struct inode *inode, struct file *file)		\
{									\
	return single_open(file, name##_show, NULL);			\
}									\
static ssize_t name##_write(struct file *filp, const char __user *ubuf,	\
			    size_t cnt, loff_t *ppos)			\
{									\
	char kbuf[16];							\
	int val;							\
	if (cnt >= sizeof(kbuf))					\
		return -EINVAL;						\
	if (copy_from_user(kbuf, ubuf, cnt))				\
		return -EFAULT;						\
	kbuf[cnt] = '\0';						\
	if (kstrtoint(strstrip(kbuf), 0, &val))				\
		return -EINVAL;						\
	name##_val = val;						\
	return cnt;							\
}									\
static const struct file_operations name##_proc_fops = {		\
	.open    = name##_open,						\
	.read    = seq_read,						\
	.write   = name##_write,					\
	.llseek  = seq_lseek,						\
	.release = single_release,					\
}

DEFINE_THEIA_INT_NODE(blackSwitch, 0);
DEFINE_THEIA_INT_NODE(brightSwitch, 0);

/* /proc/force_reboot */
static int force_reboot_show(struct seq_file *m, void *v)
{
	seq_puts(m, "0\n");
	return 0;
}

static int force_reboot_open(struct inode *inode, struct file *file)
{
	return single_open(file, force_reboot_show, NULL);
}

static ssize_t force_reboot_write(struct file *filp, const char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	char kbuf[16];

	if (cnt >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, ubuf, cnt))
		return -EFAULT;
	kbuf[cnt] = '\0';

	if (sysfs_streq(kbuf, "1") || sysfs_streq(kbuf, "reboot")) {
		pr_warn("oplus_theia: Force reboot requested by userspace\n");
		emergency_restart();
	}

	return cnt;
}

static const struct file_operations force_reboot_fops = {
	.open    = force_reboot_open,
	.read    = seq_read,
	.write   = force_reboot_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Generic string proc helper for /proc/oplusVersion nodes */
#define DEFINE_OPLUS_STR_NODE(name, default_val)			\
static char name##_buf[64] = default_val;				\
static int name##_show(struct seq_file *m, void *v)			\
{									\
	unsigned long flags;						\
	spin_lock_irqsave(&theia_lock, flags);				\
	seq_printf(m, "%s\n", name##_buf);				\
	spin_unlock_irqrestore(&theia_lock, flags);			\
	return 0;							\
}									\
static int name##_open(struct inode *inode, struct file *file)		\
{									\
	return single_open(file, name##_show, NULL);			\
}									\
static ssize_t name##_write(struct file *filp, const char __user *ubuf,	\
			    size_t cnt, loff_t *ppos)			\
{									\
	char kbuf[sizeof(name##_buf)];					\
	size_t len = min(cnt, sizeof(kbuf) - 1);			\
	unsigned long flags;						\
	if (copy_from_user(kbuf, ubuf, len))				\
		return -EFAULT;						\
	kbuf[len] = '\0';						\
	spin_lock_irqsave(&theia_lock, flags);				\
	strscpy(name##_buf, strstrip(kbuf), sizeof(name##_buf));	\
	spin_unlock_irqrestore(&theia_lock, flags);			\
	return cnt;							\
}									\
static const struct file_operations name##_proc_fops = {		\
	.open    = name##_open,						\
	.read    = seq_read,						\
	.write   = name##_write,					\
	.llseek  = seq_lseek,						\
	.release = single_release,					\
}

DEFINE_OPLUS_STR_NODE(prjName, "23317");
DEFINE_OPLUS_STR_NODE(prjVersion, "11");
DEFINE_OPLUS_STR_NODE(pcbVersion, "PRE_EVB");
DEFINE_OPLUS_STR_NODE(serialID, "0x0");
DEFINE_OPLUS_STR_NODE(operatorName, "all");
DEFINE_OPLUS_STR_NODE(RFType, "0");
DEFINE_OPLUS_STR_NODE(modemType, "0");
DEFINE_OPLUS_STR_NODE(engVersion, "release");
DEFINE_OPLUS_STR_NODE(ocp, "0");

static void init_prj_name(void)
{
	char *pos;

	if (saved_command_line) {
		pos = strstr(saved_command_line, "PN=");
		if (!pos)
			pos = strstr(saved_command_line, "oplusboot.prjname=");
		if (!pos)
			pos = strstr(saved_command_line, "androidboot.prjname=");

		if (pos) {
			char *end;

			pos = strchr(pos, '=');
			if (pos) {
				pos++;
				end = strpbrk(pos, " \t\n\r");
				if (end) {
					size_t len = min_t(size_t, end - pos,
							   sizeof(prjName_buf) - 1);

					memcpy(prjName_buf, pos, len);
					prjName_buf[len] = '\0';
				} else {
					strscpy(prjName_buf, pos, sizeof(prjName_buf));
				}
			}
		}
	}
}

static int __init oplus_theia_init(void)
{
	phoenix_heartbeat = jiffies;
	init_prj_name();

	proc_create("theiaPwkReport", 0644, NULL, &theia_pwk_report_fops);
	proc_create("pwkMonitorParam", 0644, NULL, &pwk_monitor_param_fops);
	proc_create("sau_pwknum", 0644, NULL, &sau_pwknum_fops);
	proc_create("shutdown_detect", 0644, NULL, &shutdown_detect_fops);
	proc_create("opbootfrom", 0444, NULL, &opbootfrom_fops);
	proc_create("phoenix", 0644, NULL, &phoenix_fops);
	proc_create("blackSwitch", 0644, NULL, &blackSwitch_proc_fops);
	proc_create("brightSwitch", 0644, NULL, &brightSwitch_proc_fops);
	proc_create("force_reboot", 0644, NULL, &force_reboot_fops);

	oplus_version_dir = proc_mkdir("oplusVersion", NULL);
	if (oplus_version_dir) {
		proc_create("prjName", 0666, oplus_version_dir, &prjName_proc_fops);
		proc_create("prjVersion", 0666, oplus_version_dir, &prjVersion_proc_fops);
		proc_create("pcbVersion", 0666, oplus_version_dir, &pcbVersion_proc_fops);
		proc_create("serialID", 0666, oplus_version_dir, &serialID_proc_fops);
		proc_create("operatorName", 0666, oplus_version_dir, &operatorName_proc_fops);
		proc_create("RFType", 0666, oplus_version_dir, &RFType_proc_fops);
		proc_create("modemType", 0666, oplus_version_dir, &modemType_proc_fops);
		proc_create("engVersion", 0666, oplus_version_dir, &engVersion_proc_fops);
		proc_create("ocp", 0666, oplus_version_dir, &ocp_proc_fops);

		/* Symlink /proc/oppoVersion for ColorOS compatibility */
		proc_symlink("oppoVersion", NULL, "oplusVersion");
	}

	pr_info("oplus_theia: Theia and oplusVersion interfaces initialized\n");
	return 0;
}

static void __exit oplus_theia_exit(void)
{
	if (oplus_version_dir) {
		remove_proc_entry("oppoVersion", NULL);
		remove_proc_entry("ocp", oplus_version_dir);
		remove_proc_entry("engVersion", oplus_version_dir);
		remove_proc_entry("modemType", oplus_version_dir);
		remove_proc_entry("RFType", oplus_version_dir);
		remove_proc_entry("operatorName", oplus_version_dir);
		remove_proc_entry("serialID", oplus_version_dir);
		remove_proc_entry("pcbVersion", oplus_version_dir);
		remove_proc_entry("prjVersion", oplus_version_dir);
		remove_proc_entry("prjName", oplus_version_dir);
		remove_proc_entry("oplusVersion", NULL);
	}

	remove_proc_entry("force_reboot", NULL);
	remove_proc_entry("brightSwitch", NULL);
	remove_proc_entry("blackSwitch", NULL);
	remove_proc_entry("phoenix", NULL);
	remove_proc_entry("opbootfrom", NULL);
	remove_proc_entry("shutdown_detect", NULL);
	remove_proc_entry("sau_pwknum", NULL);
	remove_proc_entry("pwkMonitorParam", NULL);
	remove_proc_entry("theiaPwkReport", NULL);
}

module_init(oplus_theia_init);
module_exit(oplus_theia_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oplus Team");
MODULE_DESCRIPTION("OPlus Theia Stability and Hardware Version Telemetry Driver");
MODULE_VERSION("1.0.0");
MODULE_ALIAS("oplus_theia");
