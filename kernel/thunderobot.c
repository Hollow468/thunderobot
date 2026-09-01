// SPDX-License-Identifier: GPL-2.0
/*
 * thunderobot.c - Thunderobot unified platform driver
 *
 * Single-module integration of:
 *   - ACPI WSAA communication layer
 *   - GPU mode switching
 *   - LED control
 *   - Adapter type detection
 *
 * Sysfs layout:
 *   /sys/kernel/thunderobot/
 *       gpu/mode
 *       led/{mode,brightness,color,zone,status,apply}
 *       adapter/{type,is_type_c}
 *
 * Copyright (C) 2026 HollowDream
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include "include/thunderobot.h"

/* -------------------- Core -------------------- */

static acpi_handle h_gwmi;
static struct kobject *tb_kobj;

struct kobject *tb_get_kobj(void)
{
	return tb_kobj;
}
EXPORT_SYMBOL_GPL(tb_get_kobj);

int tb_wsaa_call(const u8 *in_buf, u8 *out_buf)
{
	union acpi_object argv[2];
	struct acpi_object_list args;
	acpi_status st;

	argv[0].type = ACPI_TYPE_INTEGER;
	argv[0].integer.value = 0;

	argv[1].type = ACPI_TYPE_BUFFER;
	argv[1].buffer.length = TB_SMI_BUF_SIZE;
	argv[1].buffer.pointer = (u8 *)in_buf;

	args.count = 2;
	args.pointer = argv;

	if (out_buf) {
		struct acpi_buffer output = {
			.length = ACPI_ALLOCATE_BUFFER,
			.pointer = NULL,
		};

		st = acpi_evaluate_object(h_gwmi, TB_WSAA_METHOD, &args, &output);
		if (ACPI_SUCCESS(st) && output.pointer) {
			union acpi_object *obj = output.pointer;

			if (obj->type == ACPI_TYPE_BUFFER &&
			    obj->buffer.length >= TB_SMI_BUF_SIZE)
				memcpy(out_buf, obj->buffer.pointer,
				       TB_SMI_BUF_SIZE);
			kfree(output.pointer);
		}
	} else {
		st = acpi_evaluate_object(h_gwmi, TB_WSAA_METHOD, &args, NULL);
	}

	if (ACPI_FAILURE(st)) {
		pr_err("thunderobot: ACPI WSAA call failed (status=0x%x)\n",
		       st);
		return -EIO;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(tb_wsaa_call);

/* -------------------- Feature -------------------- */

struct tb_feature {
	const char *name;
	int (*init)(struct kobject *parent);
	void (*exit)(void);
};

/* -------------------- GPU -------------------- */

static struct kobject *gpu_kobj;

static int gpu_set_mode(u32 mode)
{
	u8 buf[TB_SMI_BUF_SIZE];

	tb_build_smi(buf, TB_SMI_CMD_SET, TB_SMI_FUNC_GPU, mode, 0);
	return tb_wsaa_call(buf, NULL);
}

static ssize_t gpu_mode_show(struct kobject *k, struct kobj_attribute *a,
			     char *buf)
{
	return sysfs_emit(buf, "read not supported, use 'lspci | grep VGA'\n");
}

static ssize_t gpu_mode_store(struct kobject *k, struct kobj_attribute *a,
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

static struct kobj_attribute gpu_mode_attr =
	__ATTR(mode, 0644, gpu_mode_show, gpu_mode_store);

static struct attribute *gpu_attrs[] = {
	&gpu_mode_attr.attr,
	NULL,
};

static struct attribute_group gpu_attr_group = {
	.attrs = gpu_attrs,
};

static int tb_feature_gpu_init(struct kobject *parent)
{
	int ret;

	gpu_kobj = kobject_create_and_add("gpu", parent);
	if (!gpu_kobj) {
		pr_err("thunderobot: failed to create gpu sysfs directory\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(gpu_kobj, &gpu_attr_group);
	if (ret) {
		pr_err("thunderobot: failed to create gpu sysfs group\n");
		kobject_put(gpu_kobj);
		gpu_kobj = NULL;
		return ret;
	}

	pr_info("thunderobot: gpu feature registered\n");
	return 0;
}

static void tb_feature_gpu_exit(void)
{
	if (gpu_kobj) {
		sysfs_remove_group(gpu_kobj, &gpu_attr_group);
		kobject_put(gpu_kobj);
		gpu_kobj = NULL;
	}
}

/* -------------------- LED -------------------- */

/* LED zone IDs */
#define TB_ZONE_ALL		0
#define TB_ZONE_LED3		3
#define TB_ZONE_LED2		4
#define TB_ZONE_LED1		5
#define TB_ZONE_KB_ALL		6
#define TB_ZONE_TRUNK		7
#define TB_ZONE_LOGO		8

/* LED modes */
#define TB_LED_MODE_OFF		0
#define TB_LED_MODE_STATIC	1
#define TB_LED_MODE_BREATHING	3
#define TB_LED_MODE_CYCLE	6
#define TB_LED_MODE_AMBIENT	7

static struct kobject *led_kobj;
static DEFINE_MUTEX(led_lock);
static u8 cur_zone = TB_ZONE_KB_ALL;
static u8 cur_mode = TB_LED_MODE_STATIC;
static u8 cur_brightness = 15;
static u8 cur_red = 255, cur_green = 255, cur_blue = 255;

static inline u32 tb_make_led_data(u8 mode, u8 brightness,
				   u8 red, u8 green, u8 blue)
{
	return ((u32)mode << 28) | ((u32)brightness << 24) |
	       ((u32)red << 16) | ((u32)green << 8) | blue;
}

static int led_set(u8 zone, u8 mode, u8 brightness,
		   u8 red, u8 green, u8 blue)
{
	u8 buf[TB_SMI_BUF_SIZE];
	u32 led_data;
	int ret;

	led_data = tb_make_led_data(mode, brightness, red, green, blue);
	tb_build_smi(buf, TB_SMI_CMD_SET, TB_SMI_FUNC_LED, zone, led_data);

	mutex_lock(&led_lock);
	ret = tb_wsaa_call(buf, NULL);
	mutex_unlock(&led_lock);

	return ret;
}

static int led_get(u32 *led_data)
{
	u8 buf[TB_SMI_BUF_SIZE];
	u8 resp[TB_SMI_BUF_SIZE];
	int ret;

	tb_build_smi(buf, TB_SMI_CMD_GET, TB_SMI_FUNC_LED, 0, 0);

	mutex_lock(&led_lock);
	ret = tb_wsaa_call(buf, resp);
	mutex_unlock(&led_lock);

	if (ret == 0 && led_data)
		*led_data = get_unaligned_le32(&resp[8]);
	return ret;
}

static ssize_t led_mode_show(struct kobject *k, struct kobj_attribute *a,
			     char *buf)
{
	return sysfs_emit(buf, "%u\n", cur_mode);
}

static ssize_t led_mode_store(struct kobject *k, struct kobj_attribute *a,
			      const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 7)
		return -EINVAL;

	cur_mode = (u8)val;
	ret = led_set(cur_zone, cur_mode, cur_brightness,
		      cur_red, cur_green, cur_blue);
	return ret ? ret : count;
}

static struct kobj_attribute led_mode_attr =
	__ATTR(mode, 0644, led_mode_show, led_mode_store);

static ssize_t led_brightness_show(struct kobject *k, struct kobj_attribute *a,
				   char *buf)
{
	return sysfs_emit(buf, "%u\n", cur_brightness);
}

static ssize_t led_brightness_store(struct kobject *k, struct kobj_attribute *a,
				    const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 15)
		return -EINVAL;

	cur_brightness = (u8)val;
	ret = led_set(cur_zone, cur_mode, cur_brightness,
		      cur_red, cur_green, cur_blue);
	return ret ? ret : count;
}

static struct kobj_attribute led_brightness_attr =
	__ATTR(brightness, 0644, led_brightness_show, led_brightness_store);

static ssize_t led_color_show(struct kobject *k, struct kobj_attribute *a,
			      char *buf)
{
	return sysfs_emit(buf, "%02x%02x%02x\n", cur_red, cur_green, cur_blue);
}

static ssize_t led_color_store(struct kobject *k, struct kobj_attribute *a,
			       const char *buf, size_t count)
{
	unsigned long rgb;
	int ret;

	ret = kstrtoul(buf, 16, &rgb);
	if (ret)
		return ret;

	if (rgb > 0xFFFFFF)
		return -EINVAL;

	cur_red = (rgb >> 16) & 0xFF;
	cur_green = (rgb >> 8) & 0xFF;
	cur_blue = rgb & 0xFF;

	ret = led_set(cur_zone, cur_mode, cur_brightness,
		      cur_red, cur_green, cur_blue);
	return ret ? ret : count;
}

static struct kobj_attribute led_color_attr =
	__ATTR(color, 0644, led_color_show, led_color_store);

static ssize_t led_zone_show(struct kobject *k, struct kobj_attribute *a,
			     char *buf)
{
	return sysfs_emit(buf, "%u\n", cur_zone);
}

static ssize_t led_zone_store(struct kobject *k, struct kobj_attribute *a,
			      const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 8 || (val >= 1 && val <= 2))
		return -EINVAL;

	cur_zone = (u8)val;
	return count;
}

static struct kobj_attribute led_zone_attr =
	__ATTR(zone, 0644, led_zone_show, led_zone_store);

static ssize_t status_show(struct kobject *k, struct kobj_attribute *a,
			       char *buf)
{
	u32 led_data;
	int ret;

	ret = led_get(&led_data);
	if (ret)
		return ret;

	return sysfs_emit(buf,
			  "raw=0x%08x mode=%u brightness=%u rgb=(%u,%u,%u)\n",
			  led_data,
			  (led_data >> 28) & 0xF,
			  (led_data >> 24) & 0xF,
			  (led_data >> 16) & 0xFF,
			  (led_data >> 8) & 0xFF,
			  led_data & 0xFF);
}

static struct kobj_attribute led_status_attr =
	__ATTR_RO(status);

static ssize_t apply_store(struct kobject *k, struct kobj_attribute *a,
			       const char *buf, size_t count)
{
	int ret;

	ret = led_set(cur_zone, cur_mode, cur_brightness,
		      cur_red, cur_green, cur_blue);
	return ret ? ret : count;
}

static struct kobj_attribute led_apply_attr =
	__ATTR_WO(apply);

static struct attribute *led_attrs[] = {
	&led_mode_attr.attr,
	&led_brightness_attr.attr,
	&led_color_attr.attr,
	&led_zone_attr.attr,
	&led_status_attr.attr,
	&led_apply_attr.attr,
	NULL,
};

static struct attribute_group led_attr_group = {
	.attrs = led_attrs,
};

static int tb_feature_led_init(struct kobject *parent)
{
	int ret;

	led_kobj = kobject_create_and_add("led", parent);
	if (!led_kobj) {
		pr_err("thunderobot: failed to create led sysfs directory\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(led_kobj, &led_attr_group);
	if (ret) {
		pr_err("thunderobot: failed to create led sysfs group\n");
		kobject_put(led_kobj);
		led_kobj = NULL;
		return ret;
	}

	pr_info("thunderobot: led feature registered\n");
	return 0;
}

static void tb_feature_led_exit(void)
{
	if (led_kobj) {
		sysfs_remove_group(led_kobj, &led_attr_group);
		kobject_put(led_kobj);
		led_kobj = NULL;
	}
}

/* -------------------- Adapter -------------------- */

static struct kobject *adapter_kobj;

static int adapter_get_type(bool *is_type_c)
{
	u8 buf[TB_SMI_BUF_SIZE];
	u8 resp[TB_SMI_BUF_SIZE];
	u32 a4;
	int ret;

	tb_build_smi(buf, TB_SMI_CMD_GET, TB_SMI_FUNC_ADAPTER, 0, 0);
	ret = tb_wsaa_call(buf, resp);
	if (ret)
		return ret;

	a4 = get_unaligned_le32(&resp[12]);
	if (a4 == 1)
		*is_type_c = true;
	else
		*is_type_c = false;

	return 0;
}

static ssize_t type_show(struct kobject *k, struct kobj_attribute *a,
				 char *buf)
{
	bool is_type_c = false;
	int ret;

	ret = adapter_get_type(&is_type_c);
	if (ret)
		return sysfs_emit(buf, "Unknown\n");

	return sysfs_emit(buf, "%s\n", is_type_c ? "Type-C" : "AC");
}

static struct kobj_attribute adapter_type_attr =
	__ATTR_RO(type);

static ssize_t is_type_c_show(struct kobject *k,
				      struct kobj_attribute *a, char *buf)
{
	bool is_type_c = false;
	int ret;

	ret = adapter_get_type(&is_type_c);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", is_type_c ? 1 : 0);
}

static struct kobj_attribute adapter_is_type_c_attr =
	__ATTR_RO(is_type_c);

static struct attribute *adapter_attrs[] = {
	&adapter_type_attr.attr,
	&adapter_is_type_c_attr.attr,
	NULL,
};

static struct attribute_group adapter_attr_group = {
	.attrs = adapter_attrs,
};

static int tb_feature_adapter_init(struct kobject *parent)
{
	int ret;

	adapter_kobj = kobject_create_and_add("adapter", parent);
	if (!adapter_kobj) {
		pr_err("thunderobot: failed to create adapter sysfs directory\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(adapter_kobj, &adapter_attr_group);
	if (ret) {
		pr_err("thunderobot: failed to create adapter sysfs group\n");
		kobject_put(adapter_kobj);
		adapter_kobj = NULL;
		return ret;
	}

	pr_info("thunderobot: adapter feature registered\n");
	return 0;
}

static void tb_feature_adapter_exit(void)
{
	if (adapter_kobj) {
		sysfs_remove_group(adapter_kobj, &adapter_attr_group);
		kobject_put(adapter_kobj);
		adapter_kobj = NULL;
	}
}

/* -------------------- Unified lifecycle -------------------- */

static const struct tb_feature tb_features[] = {
	{
		.name = "gpu",
		.init = tb_feature_gpu_init,
		.exit = tb_feature_gpu_exit,
	},
	{
		.name = "led",
		.init = tb_feature_led_init,
		.exit = tb_feature_led_exit,
	},
	{
		.name = "adapter",
		.init = tb_feature_adapter_init,
		.exit = tb_feature_adapter_exit,
	},
};

static int __init thunderobot_init(void)
{
	acpi_status st;
	int i;
	int ret;

	st = acpi_get_handle(NULL, TB_GWMI_PATH, &h_gwmi);
	if (ACPI_FAILURE(st)) {
		pr_err("thunderobot: failed to get ACPI handle for %s\n",
		       TB_GWMI_PATH);
		return -ENODEV;
	}

	tb_kobj = kobject_create_and_add("thunderobot", kernel_kobj);
	if (!tb_kobj) {
		pr_err("thunderobot: failed to create sysfs directory\n");
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(tb_features); i++) {
		ret = tb_features[i].init(tb_kobj);
		if (ret)
			pr_err("thunderobot: feature %s init failed: %d\n",
			       tb_features[i].name, ret);
	}

	pr_info("thunderobot: unified module loaded\n");
	return 0;
}

static void __exit thunderobot_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tb_features); i++)
		tb_features[i].exit();

	if (tb_kobj) {
		kobject_put(tb_kobj);
		tb_kobj = NULL;
	}

	pr_info("thunderobot: unified module unloaded\n");
}

module_init(thunderobot_init);
module_exit(thunderobot_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("HollowDream");
MODULE_DESCRIPTION("Thunderobot unified platform driver");
