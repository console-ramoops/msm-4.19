// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Oplus. All rights reserved.
 *
 * Implements the OPlus NANDswap / Virtual Swap procfs interfaces
 * (/proc/nandswap/ and /proc/nandswap_vnd/) required by ColorOS / OxygenOS
 * RAM Expansion framework, routing swap memory transparently into zRAM
 * to protect eMMC flash from wear and latency spikes.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <linux/mm.h>
#include "nandswap.h"

static struct proc_dir_entry *nandswap_dir;

static bool fn_enable_val = true;
static int swap_ctl_val = 1;
static unsigned int swap_limit_val = 3072;
static int dev_life_val = 100;
static int life_protect_val = 0;
static unsigned int curr_ratio_val = 30;
static char threshold_buf[64] = "0 0 0 0";

bool oplus_nandswap_is_enabled(void)
{
	return fn_enable_val;
}
EXPORT_SYMBOL_GPL(oplus_nandswap_is_enabled);

/* /proc/nandswap/fn_enable */
static int fn_enable_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", fn_enable_val ? "true" : "false");
	return 0;
}

static int fn_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, fn_enable_show, NULL);
}

static ssize_t fn_enable_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	char kbuf[32];
	char *str;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';
	str = strstrip(kbuf);

	if (sysfs_streq(str, "true") || sysfs_streq(str, "1") ||
	    sysfs_streq(str, "enable"))
		fn_enable_val = true;
	else if (sysfs_streq(str, "false") || sysfs_streq(str, "0") ||
		 sysfs_streq(str, "disable"))
		fn_enable_val = false;
	else
		return -EINVAL;

	return count;
}

static const struct file_operations fn_enable_fops = {
	.open    = fn_enable_open,
	.read    = seq_read,
	.write   = fn_enable_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/nandswap/swap_ctl */
static int swap_ctl_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", swap_ctl_val);
	return 0;
}

static int swap_ctl_open(struct inode *inode, struct file *file)
{
	return single_open(file, swap_ctl_show, NULL);
}

static ssize_t swap_ctl_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	char kbuf[32];
	int val;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (kstrtoint(strstrip(kbuf), 0, &val))
		return -EINVAL;

	swap_ctl_val = val;
	return count;
}

static const struct file_operations swap_ctl_fops = {
	.open    = swap_ctl_open,
	.read    = seq_read,
	.write   = swap_ctl_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/nandswap/swap_limit */
static int swap_limit_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%u\n", swap_limit_val);
	return 0;
}

static int swap_limit_open(struct inode *inode, struct file *file)
{
	return single_open(file, swap_limit_show, NULL);
}

static ssize_t swap_limit_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char kbuf[32];
	unsigned int val;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (kstrtouint(strstrip(kbuf), 0, &val))
		return -EINVAL;

	swap_limit_val = val;
	return count;
}

static const struct file_operations swap_limit_fops = {
	.open    = swap_limit_open,
	.read    = seq_read,
	.write   = swap_limit_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/nandswap/dev_life */
static int dev_life_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", dev_life_val);
	return 0;
}

static int dev_life_open(struct inode *inode, struct file *file)
{
	return single_open(file, dev_life_show, NULL);
}

static ssize_t dev_life_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	char kbuf[32];
	int val;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (kstrtoint(strstrip(kbuf), 0, &val))
		return -EINVAL;

	dev_life_val = clamp(val, 0, 100);
	return count;
}

static const struct file_operations dev_life_fops = {
	.open    = dev_life_open,
	.read    = seq_read,
	.write   = dev_life_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/nandswap/life_protect */
static int life_protect_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", life_protect_val);
	return 0;
}

static int life_protect_open(struct inode *inode, struct file *file)
{
	return single_open(file, life_protect_show, NULL);
}

static ssize_t life_protect_write(struct file *file, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	char kbuf[32];
	int val;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (kstrtoint(strstrip(kbuf), 0, &val))
		return -EINVAL;

	life_protect_val = val;
	return count;
}

static const struct file_operations life_protect_fops = {
	.open    = life_protect_open,
	.read    = seq_read,
	.write   = life_protect_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/nandswap/curr_ratio */
static int curr_ratio_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%u\n", curr_ratio_val);
	return 0;
}

static int curr_ratio_open(struct inode *inode, struct file *file)
{
	return single_open(file, curr_ratio_show, NULL);
}

static ssize_t curr_ratio_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char kbuf[32];
	unsigned int val;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (kstrtouint(strstrip(kbuf), 0, &val))
		return -EINVAL;

	curr_ratio_val = val;
	return count;
}

static const struct file_operations curr_ratio_fops = {
	.open    = curr_ratio_open,
	.read    = seq_read,
	.write   = curr_ratio_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/nandswap/threshold */
static int threshold_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", threshold_buf);
	return 0;
}

static int threshold_open(struct inode *inode, struct file *file)
{
	return single_open(file, threshold_show, NULL);
}

static ssize_t threshold_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	char kbuf[sizeof(threshold_buf)];
	size_t len = min(count, sizeof(kbuf) - 1);

	if (copy_from_user(kbuf, buf, len))
		return -EFAULT;
	kbuf[len] = '\0';

	strscpy(threshold_buf, strstrip(kbuf), sizeof(threshold_buf));
	return count;
}

static const struct file_operations threshold_fops = {
	.open    = threshold_open,
	.read    = seq_read,
	.write   = threshold_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* /proc/nandswap/stat_info */
static int stat_info_show(struct seq_file *m, void *v)
{
	struct sysinfo si;
	unsigned long total_swap_mb = 0;
	unsigned long free_swap_mb = 0;

	si_meminfo(&si);
#ifdef CONFIG_SWAP
	si_swapinfo(&si);
	total_swap_mb = si.totalswap >> (20 - PAGE_SHIFT);
	free_swap_mb = si.freeswap >> (20 - PAGE_SHIFT);
#endif

	seq_printf(m,
		   "nandswap_status: active\n"
		   "backend: zram\n"
		   "fn_enable: %s\n"
		   "total_swap_mb: %lu\n"
		   "free_swap_mb: %lu\n"
		   "dev_life: %d\n"
		   "life_protect: %d\n"
		   "vm_swappiness: %d\n",
		   fn_enable_val ? "true" : "false",
		   total_swap_mb,
		   free_swap_mb,
		   dev_life_val,
		   life_protect_val,
		   vm_swappiness);
	return 0;
}

static int stat_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, stat_info_show, NULL);
}

static const struct file_operations stat_info_fops = {
	.open    = stat_info_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

int __init oplus_nandswap_init(void)
{
	nandswap_dir = proc_mkdir("nandswap", NULL);
	if (!nandswap_dir) {
		pr_err("nandswap: failed to create /proc/nandswap\n");
		return -ENOMEM;
	}

	proc_symlink("nandswap_vnd", NULL, "nandswap");

	proc_create("fn_enable", 0664, nandswap_dir, &fn_enable_fops);
	proc_create("swap_ctl", 0664, nandswap_dir, &swap_ctl_fops);
	proc_create("swap_limit", 0664, nandswap_dir, &swap_limit_fops);
	proc_create("dev_life", 0664, nandswap_dir, &dev_life_fops);
	proc_create("life_protect", 0664, nandswap_dir, &life_protect_fops);
	proc_create("curr_ratio", 0664, nandswap_dir, &curr_ratio_fops);
	proc_create("threshold", 0664, nandswap_dir, &threshold_fops);
	proc_create("stat_info", 0444, nandswap_dir, &stat_info_fops);

	pr_info("nandswap: OPLUS virtual zRAM-backed NANDswap initialized successfully\n");
	return 0;
}

void __exit oplus_nandswap_exit(void)
{
	if (nandswap_dir) {
		remove_proc_entry("stat_info", nandswap_dir);
		remove_proc_entry("threshold", nandswap_dir);
		remove_proc_entry("curr_ratio", nandswap_dir);
		remove_proc_entry("life_protect", nandswap_dir);
		remove_proc_entry("dev_life", nandswap_dir);
		remove_proc_entry("swap_limit", nandswap_dir);
		remove_proc_entry("swap_ctl", nandswap_dir);
		remove_proc_entry("fn_enable", nandswap_dir);
		remove_proc_entry("nandswap_vnd", NULL);
		remove_proc_entry("nandswap", NULL);
	}
}

module_init(oplus_nandswap_init);
module_exit(oplus_nandswap_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("OPLUS Virtual zRAM-backed NANDswap Interface Driver");
MODULE_AUTHOR("Oplus Team");
MODULE_VERSION("1.0.0");
MODULE_ALIAS("oplus_nandswap");
