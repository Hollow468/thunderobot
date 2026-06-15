// SPDX-License-Identifier: GPL-2.0
/*
 * thunderobot-gpu.c - Thunderobot GPU mode switching
 *
 * Controls GPU mode (hybrid/discrete/integrated) via ACPI WSAA method.
 * Migrated from lsgpu project.
 *
 * Copyright (C) 2026 HollowDream
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include "include/thunderobot.h"

static struct kobject *gpu_kobj;

static int gpu_set_mode(u32 mode)
{
	u8 buf[TB_SMI_BUF_SIZE];

	tb_build_smi(buf, TB_SMI_CMD_SET, TB_SMI_FUNC_GPU, mode, 0);
	return tb_wsaa_call(buf, NULL);
}

static ssize_t mode_store(struct kobject *k, struct kobj_attribute *a,
			   const char *buf, size_t count)
{
	unsigned long mode;
	int ret;

	ret = kstrtoul(buf, 0, &mode);
	if (ret)
		return ret;

	if (mode < 1 || mode > 3)
		return -EINVAL;

	ret = gpu_set_mode((u32)mode);
	if (ret)
		return ret;

	return count;
}

static ssize_t mode_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "read not supported, use 'lspci | grep VGA'\n");
}

static struct kobj_attribute mode_attr =
	__ATTR(mode, 0644, mode_show, mode_store);

static struct attribute *gpu_attrs[] = {
	&mode_attr.attr,
	NULL,
};

static struct attribute_group gpu_attr_group = {
	.attrs = gpu_attrs,
};

static int __init thunderobot_gpu_init(void)
{
	struct kobject *tb_kobj;
	int ret;

	/* Find or create parent kobject */
	tb_kobj = kobject_create_and_add("thunderobot", kernel_kobj);
	if (!tb_kobj) {
		pr_err("thunderobot-gpu: failed to create parent sysfs directory\n");
		return -ENOMEM;
	}

	gpu_kobj = kobject_create_and_add("gpu", tb_kobj);
	kobject_put(tb_kobj);
	if (!gpu_kobj) {
		pr_err("thunderobot-gpu: failed to create gpu sysfs directory\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(gpu_kobj, &gpu_attr_group);
	if (ret) {
		pr_err("thunderobot-gpu: failed to create sysfs group\n");
		kobject_put(gpu_kobj);
		gpu_kobj = NULL;
		return ret;
	}

	pr_info("thunderobot-gpu: module loaded\n");
	return 0;
}

static void __exit thunderobot_gpu_exit(void)
{
	if (gpu_kobj) {
		sysfs_remove_group(gpu_kobj, &gpu_attr_group);
		kobject_put(gpu_kobj);
		gpu_kobj = NULL;
	}
	pr_info("thunderobot-gpu: module unloaded\n");
}

module_init(thunderobot_gpu_init);
module_exit(thunderobot_gpu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("HollowDream");
MODULE_DESCRIPTION("Thunderobot GPU mode switching via ACPI WSAA");
