// SPDX-License-Identifier: GPL-2.0
/*
 * thunderobot.c - Thunderobot unified platform driver
 *
 * Single-module integration of:
 *   - ACPI WSAA communication layer
 *   - GPU mode switching
 *   - LED control
 *
 * Sysfs layout:
 *   /sys/kernel/thunderobot/
 *       gpu/mode
 *       led/{mode,brightness,color,zone,status,apply}
 *
 * Copyright (C) 2026 HollowDream
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/string.h>
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
		if (ACPI_FAILURE(st)) {
			pr_err("thunderobot: ACPI WSAA call failed (status=0x%x)\n",
			       st);
			return -EIO;
		}

		if (!output.pointer) {
			pr_err("thunderobot: ACPI WSAA call returned NULL output\n");
			kfree(output.pointer);
			return -EIO;
		}

		if (((union acpi_object *)output.pointer)->type != ACPI_TYPE_BUFFER ||
		    ((union acpi_object *)output.pointer)->buffer.length < TB_SMI_BUF_SIZE) {
			pr_err("thunderobot: ACPI WSAA call returned invalid output\n");
			kfree(output.pointer);
			return -EIO;
		}

		memcpy(out_buf, ((union acpi_object *)output.pointer)->buffer.pointer,
		       TB_SMI_BUF_SIZE);
		kfree(output.pointer);
	} else {
		st = acpi_evaluate_object(h_gwmi, TB_WSAA_METHOD, &args, NULL);
		if (ACPI_FAILURE(st)) {
			pr_err("thunderobot: ACPI WSAA call failed (status=0x%x)\n",
			       st);
			return -EIO;
		}
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

/* -------------------- Power -------------------- */

static struct kobject *power_kobj;
static u32 cur_power_mode;
static DEFINE_MUTEX(power_lock);

static int power_get_mode(u32 *mode)
{
	u8 buf[TB_SMI_BUF_SIZE];
	u8 resp[TB_SMI_BUF_SIZE];
	int ret;

	tb_build_smi(buf, TB_SMI_CMD_GET, TB_SMI_FUNC_PERF, 0, 0);
	ret = tb_wsaa_call(buf, resp);
	if (ret)
		return ret;

	*mode = get_unaligned_le32(&resp[8]);
	return 0;
}

static int perf_set_mode(u32 mode)
{
	u8 buf[TB_SMI_BUF_SIZE];

	if (mode > 2)
		return -EINVAL;

	tb_build_smi(buf, TB_SMI_CMD_SET, TB_SMI_FUNC_PERF, mode, 0);
	return tb_wsaa_call(buf, NULL);
}

static ssize_t power_mode_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	u32 mode;
	int ret;

	mutex_lock(&power_lock);
	mode = cur_power_mode;
	mutex_unlock(&power_lock);

	ret = sysfs_emit(buf, "%u\n", mode);
	return ret;
}

static ssize_t power_mode_store(struct kobject *k, struct kobj_attribute *a,
				const char *buf, size_t count)
{
	unsigned long mode;
	int ret;

	ret = kstrtoul(buf, 0, &mode);
	if (ret)
		return ret;

	if (mode > 2)
		return -EINVAL;

	mutex_lock(&power_lock);
	ret = perf_set_mode((u32)mode);
	if (ret)
	{
		mutex_unlock(&power_lock);
		return ret;
	}

	cur_power_mode = (u32)mode;
	mutex_unlock(&power_lock);
	return count;
}

static struct kobj_attribute power_mode_attr =
	__ATTR(mode, 0644, power_mode_show, power_mode_store);

static struct attribute *power_attrs[] = {
	&power_mode_attr.attr,
	NULL,
};

static struct attribute_group power_attr_group = {
	.attrs = power_attrs,
};

static int tb_feature_power_init(struct kobject *parent)
{
	int ret;

	power_kobj = kobject_create_and_add("power", parent);
	if (!power_kobj) {
		pr_err("thunderobot: failed to create power sysfs directory\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(power_kobj, &power_attr_group);
	if (ret) {
		pr_err("thunderobot: failed to create power sysfs group\n");
		kobject_put(power_kobj);
		power_kobj = NULL;
		return ret;
	}

	ret = power_get_mode(&cur_power_mode);
	if (ret) {
		cur_power_mode = 2;
		pr_warn("thunderobot: failed to read power mode, fallback to %u\n",
			cur_power_mode);
	}
	pr_info("thunderobot: power feature registered\n");
	return 0;
}

static void tb_feature_power_exit(void)
{
	if (power_kobj) {
		sysfs_remove_group(power_kobj, &power_attr_group);
		kobject_put(power_kobj);
		power_kobj = NULL;
	}
}

/* -------------------- Fan -------------------- */

static struct kobject *fan_kobj;
static DEFINE_MUTEX(fan_lock);
static u8 cur_fan_mode; /* 0 = auto, 1 = manual */
static u8 cur_cpu_duty;
static u8 cur_gpu_duty;
static u8 cur_sys_duty;
static u8 fans_count = 2;
static u32 fan_profile_id;

static int fan_get_hwinfo(u8 *cpu_temp, u8 *gpu_temp, u16 *cpu_rpm, u16 *gpu_rpm)
{
	u8 buf[TB_SMI_BUF_SIZE];
	u8 resp[TB_SMI_BUF_SIZE];
	int ret;

	tb_build_smi(buf, TB_SMI_CMD_GET, TB_SMI_FUNC_HWINFO, 0, 0);

	mutex_lock(&fan_lock);
	ret = tb_wsaa_call(buf, resp);
	mutex_unlock(&fan_lock);

	if (ret)
		return ret;

	if (cpu_temp)
		*cpu_temp = resp[4];
	if (gpu_temp)
		*gpu_temp = resp[8];
	if (cpu_rpm)
		*cpu_rpm = get_unaligned_le16(&resp[12]);
	if (gpu_rpm)
		*gpu_rpm = get_unaligned_le16(&resp[16]);

	return 0;
}

static int fan_get_hwinfo2(u8 *sys_temp, u16 *sys_rpm)
{
	u8 buf[TB_SMI_BUF_SIZE];
	u8 resp[TB_SMI_BUF_SIZE];
	int ret;

	tb_build_smi(buf, TB_SMI_CMD_GET, TB_SMI_FUNC_HWINFO2, 0, 0);

	mutex_lock(&fan_lock);
	ret = tb_wsaa_call(buf, resp);
	mutex_unlock(&fan_lock);

	if (ret)
		return ret;

	if (sys_temp)
		*sys_temp = resp[4];
	if (sys_rpm)
		*sys_rpm = get_unaligned_le16(&resp[8]);

	return 0;
}

static int fan_get_mode(u8 *mode)
{
	u8 buf[TB_SMI_BUF_SIZE];
	u8 resp[TB_SMI_BUF_SIZE];
	int ret;

	tb_build_smi(buf, TB_SMI_CMD_GET, TB_SMI_FUNC_FAN_CTRL, 0, 0);

	mutex_lock(&fan_lock);
	ret = tb_wsaa_call(buf, resp);
	mutex_unlock(&fan_lock);

	if (ret)
		return ret;

	if (mode)
		*mode = (u8)get_unaligned_le32(&resp[4]);

	return 0;
}

static int fan_set_mode_raw(u8 mode)
{
	u8 buf[TB_SMI_BUF_SIZE];

	tb_build_smi(buf, TB_SMI_CMD_SET, TB_SMI_FUNC_FAN_CTRL, mode, 0);
	return tb_wsaa_call(buf, NULL);
}

static int fan_set_speed_raw(u8 cpu_duty, u8 gpu_duty, u8 sys_duty)
{
	u8 buf[TB_SMI_BUF_SIZE];

	tb_build_smi3(buf, TB_SMI_CMD_SET, TB_SMI_FUNC_FAN_SPEED,
		      cpu_duty, gpu_duty, sys_duty);
	return tb_wsaa_call(buf, NULL);
}

static int fan_get_profile_raw(u32 *profile)
{
	u8 buf[TB_SMI_BUF_SIZE];
	u8 resp[TB_SMI_BUF_SIZE];
	int ret;

	tb_build_smi(buf, TB_SMI_CMD_GET, TB_SMI_FUNC_FAN_SPEED, 0, 0);
	ret = tb_wsaa_call(buf, resp);
	if (ret)
		return ret;

	if (profile)
		*profile = get_unaligned_le32(&resp[4]);

	return 0;
}

static int fan_detect_capabilities(void)
{
	u8 buf[TB_SMI_BUF_SIZE];
	u8 resp[TB_SMI_BUF_SIZE];
	u32 skuid;
	int ret;

	tb_build_smi(buf, TB_SMI_CMD_GET, TB_SMI_FUNC_BIOS, 0, 0);
	ret = tb_wsaa_call(buf, resp);
	if (ret == 0) {
		skuid = get_unaligned_le32(&resp[4]);
		if (skuid == 52 || skuid == 53)
			fans_count = 3;
		else
			fans_count = 2;
	} else {
		fans_count = 2;
	}

	fan_get_profile_raw(&fan_profile_id);
	fan_get_mode(&cur_fan_mode);

	return 0;
}

static ssize_t fan_mode_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", cur_fan_mode);
}

static ssize_t fan_mode_store(struct kobject *k, struct kobj_attribute *a,
			      const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 1)
		return -EINVAL;

	mutex_lock(&fan_lock);
	if (val == 0) {
		ret = fan_set_mode_raw(0);
		if (ret == 0)
			fan_set_speed_raw(0xFF, 0xFF, 0xFF);
	} else {
		ret = fan_set_mode_raw(1);
		if (ret == 0) {
			u8 c = cur_cpu_duty ? cur_cpu_duty : 50;
			u8 g = cur_gpu_duty ? cur_gpu_duty : 50;
			u8 s = cur_sys_duty ? cur_sys_duty : (fans_count == 3 ? 50 : 0xFF);
			ret = fan_set_speed_raw(c, g, s);
		}
	}
	if (ret == 0)
		cur_fan_mode = (u8)val;
	mutex_unlock(&fan_lock);

	return ret ? ret : count;
}

static struct kobj_attribute fan_mode_attr =
	__ATTR(mode, 0644, fan_mode_show, fan_mode_store);

static ssize_t fan_speed_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	if (fans_count == 3)
		return sysfs_emit(buf, "%u %u %u\n", cur_cpu_duty, cur_gpu_duty, cur_sys_duty);
	else
		return sysfs_emit(buf, "%u %u\n", cur_cpu_duty, cur_gpu_duty);
}

static ssize_t fan_speed_store(struct kobject *k, struct kobj_attribute *a,
			       const char *buf, size_t count)
{
	unsigned int c = 0, g = 0, s = 0xFF;
	int n, ret;

	n = sscanf(buf, "%u %u %u", &c, &g, &s);
	if (n == 1) {
		if (c > 100 && c != 255)
			return -EINVAL;
		g = c;
		s = (fans_count == 3) ? c : 0xFF;
	} else if (n == 2) {
		if ((c > 100 && c != 255) || (g > 100 && g != 255))
			return -EINVAL;
		s = (fans_count == 3) ? g : 0xFF;
	} else if (n == 3) {
		if ((c > 100 && c != 255) || (g > 100 && g != 255) || (s > 100 && s != 255))
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	mutex_lock(&fan_lock);
	if (c == 255 && g == 255 && (s == 255 || s == 0xFF)) {
		ret = fan_set_mode_raw(0);
		if (ret == 0) {
			fan_set_speed_raw(0xFF, 0xFF, 0xFF);
			cur_fan_mode = 0;
		}
	} else {
		ret = fan_set_mode_raw(1);
		if (ret == 0) {
			ret = fan_set_speed_raw((u8)c, (u8)g, (u8)s);
			if (ret == 0) {
				cur_cpu_duty = (u8)c;
				cur_gpu_duty = (u8)g;
				cur_sys_duty = (u8)s;
				cur_fan_mode = 1;
			}
		}
	}
	mutex_unlock(&fan_lock);

	return ret ? ret : count;
}

static struct kobj_attribute fan_speed_attr =
	__ATTR(speed, 0644, fan_speed_show, fan_speed_store);

static ssize_t fan_cpu_speed_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", cur_cpu_duty);
}

static ssize_t fan_cpu_speed_store(struct kobject *k, struct kobj_attribute *a,
				   const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 100 && val != 255)
		return -EINVAL;

	mutex_lock(&fan_lock);
	cur_cpu_duty = (u8)val;
	if (val == 255 && cur_gpu_duty == 255 && (cur_sys_duty == 255 || fans_count == 2)) {
		ret = fan_set_mode_raw(0);
		if (ret == 0) {
			fan_set_speed_raw(0xFF, 0xFF, 0xFF);
			cur_fan_mode = 0;
		}
	} else {
		ret = fan_set_mode_raw(1);
		if (ret == 0) {
			ret = fan_set_speed_raw(cur_cpu_duty,
						cur_gpu_duty ? cur_gpu_duty : cur_cpu_duty,
						fans_count == 3 ? (cur_sys_duty ? cur_sys_duty : cur_cpu_duty) : 0xFF);
			if (ret == 0)
				cur_fan_mode = 1;
		}
	}
	mutex_unlock(&fan_lock);

	return ret ? ret : count;
}

static struct kobj_attribute fan_cpu_speed_attr =
	__ATTR(cpu_speed, 0644, fan_cpu_speed_show, fan_cpu_speed_store);

static ssize_t fan_gpu_speed_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", cur_gpu_duty);
}

static ssize_t fan_gpu_speed_store(struct kobject *k, struct kobj_attribute *a,
				   const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 100 && val != 255)
		return -EINVAL;

	mutex_lock(&fan_lock);
	cur_gpu_duty = (u8)val;
	if (cur_cpu_duty == 255 && val == 255 && (cur_sys_duty == 255 || fans_count == 2)) {
		ret = fan_set_mode_raw(0);
		if (ret == 0) {
			fan_set_speed_raw(0xFF, 0xFF, 0xFF);
			cur_fan_mode = 0;
		}
	} else {
		ret = fan_set_mode_raw(1);
		if (ret == 0) {
			ret = fan_set_speed_raw(cur_cpu_duty ? cur_cpu_duty : (u8)val,
						cur_gpu_duty,
						fans_count == 3 ? (cur_sys_duty ? cur_sys_duty : (u8)val) : 0xFF);
			if (ret == 0)
				cur_fan_mode = 1;
		}
	}
	mutex_unlock(&fan_lock);

	return ret ? ret : count;
}

static struct kobj_attribute fan_gpu_speed_attr =
	__ATTR(gpu_speed, 0644, fan_gpu_speed_show, fan_gpu_speed_store);

static ssize_t fan_sys_speed_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", cur_sys_duty);
}

static ssize_t fan_sys_speed_store(struct kobject *k, struct kobj_attribute *a,
				   const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 100 && val != 255)
		return -EINVAL;

	mutex_lock(&fan_lock);
	cur_sys_duty = (u8)val;
	if (cur_cpu_duty == 255 && cur_gpu_duty == 255 && val == 255) {
		ret = fan_set_mode_raw(0);
		if (ret == 0) {
			fan_set_speed_raw(0xFF, 0xFF, 0xFF);
			cur_fan_mode = 0;
		}
	} else {
		ret = fan_set_mode_raw(1);
		if (ret == 0) {
			ret = fan_set_speed_raw(cur_cpu_duty ? cur_cpu_duty : 50,
						cur_gpu_duty ? cur_gpu_duty : 50,
						cur_sys_duty);
			if (ret == 0)
				cur_fan_mode = 1;
		}
	}
	mutex_unlock(&fan_lock);

	return ret ? ret : count;
}

static struct kobj_attribute fan_sys_speed_attr =
	__ATTR(sys_speed, 0644, fan_sys_speed_show, fan_sys_speed_store);

static ssize_t fan_cpu_temp_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	u8 cpu_temp = 0;
	int ret = fan_get_hwinfo(&cpu_temp, NULL, NULL, NULL);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%u\n", cpu_temp);
}

static struct kobj_attribute fan_cpu_temp_attr =
	__ATTR(cpu_temp, 0444, fan_cpu_temp_show, NULL);

static ssize_t fan_gpu_temp_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	u8 gpu_temp = 0;
	int ret = fan_get_hwinfo(NULL, &gpu_temp, NULL, NULL);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%u\n", gpu_temp);
}

static struct kobj_attribute fan_gpu_temp_attr =
	__ATTR(gpu_temp, 0444, fan_gpu_temp_show, NULL);

static ssize_t fan_sys_temp_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	u8 sys_temp = 0;
	int ret = fan_get_hwinfo2(&sys_temp, NULL);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%u\n", sys_temp);
}

static struct kobj_attribute fan_sys_temp_attr =
	__ATTR(sys_temp, 0444, fan_sys_temp_show, NULL);

static ssize_t fan_cpu_rpm_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	u16 cpu_rpm = 0;
	int ret = fan_get_hwinfo(NULL, NULL, &cpu_rpm, NULL);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%u\n", cpu_rpm);
}

static struct kobj_attribute fan_cpu_rpm_attr =
	__ATTR(cpu_rpm, 0444, fan_cpu_rpm_show, NULL);

static ssize_t fan_gpu_rpm_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	u16 gpu_rpm = 0;
	int ret = fan_get_hwinfo(NULL, NULL, NULL, &gpu_rpm);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%u\n", gpu_rpm);
}

static struct kobj_attribute fan_gpu_rpm_attr =
	__ATTR(gpu_rpm, 0444, fan_gpu_rpm_show, NULL);

static ssize_t fan_sys_rpm_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	u16 sys_rpm = 0;
	int ret = fan_get_hwinfo2(NULL, &sys_rpm);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%u\n", sys_rpm);
}

static struct kobj_attribute fan_sys_rpm_attr =
	__ATTR(sys_rpm, 0444, fan_sys_rpm_show, NULL);

static ssize_t fan_fans_count_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", fans_count);
}

static struct kobj_attribute fan_fans_count_attr =
	__ATTR(fans_count, 0444, fan_fans_count_show, NULL);

static ssize_t fan_profile_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", fan_profile_id);
}

static struct kobj_attribute fan_profile_attr =
	__ATTR(profile, 0444, fan_profile_show, NULL);

static ssize_t fan_status_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	u8 cpu_temp = 0, gpu_temp = 0, sys_temp = 0;
	u16 cpu_rpm = 0, gpu_rpm = 0, sys_rpm = 0;
	int ret;

	ret = fan_get_hwinfo(&cpu_temp, &gpu_temp, &cpu_rpm, &gpu_rpm);
	if (ret)
		return ret;

	if (fans_count == 3) {
		fan_get_hwinfo2(&sys_temp, &sys_rpm);
		return sysfs_emit(buf,
				  "mode=%u (%s)\n"
				  "fans_count=%u\n"
				  "profile=%u\n"
				  "cpu_temp=%u C\n"
				  "cpu_rpm=%u RPM\n"
				  "cpu_duty=%u%%\n"
				  "gpu_temp=%u C\n"
				  "gpu_rpm=%u RPM\n"
				  "gpu_duty=%u%%\n"
				  "sys_temp=%u C\n"
				  "sys_rpm=%u RPM\n"
				  "sys_duty=%u%%\n",
				  cur_fan_mode, cur_fan_mode ? "manual" : "auto",
				  fans_count,
				  fan_profile_id,
				  cpu_temp, cpu_rpm, cur_cpu_duty,
				  gpu_temp, gpu_rpm, cur_gpu_duty,
				  sys_temp, sys_rpm, cur_sys_duty);
	} else {
		return sysfs_emit(buf,
				  "mode=%u (%s)\n"
				  "fans_count=%u\n"
				  "profile=%u\n"
				  "cpu_temp=%u C\n"
				  "cpu_rpm=%u RPM\n"
				  "cpu_duty=%u%%\n"
				  "gpu_temp=%u C\n"
				  "gpu_rpm=%u RPM\n"
				  "gpu_duty=%u%%\n",
				  cur_fan_mode, cur_fan_mode ? "manual" : "auto",
				  fans_count,
				  fan_profile_id,
				  cpu_temp, cpu_rpm, cur_cpu_duty,
				  gpu_temp, gpu_rpm, cur_gpu_duty);
	}
}

static struct kobj_attribute fan_status_attr =
	__ATTR(status, 0444, fan_status_show, NULL);

static struct attribute *fan_attrs[] = {
	&fan_mode_attr.attr,
	&fan_speed_attr.attr,
	&fan_cpu_speed_attr.attr,
	&fan_gpu_speed_attr.attr,
	&fan_sys_speed_attr.attr,
	&fan_cpu_temp_attr.attr,
	&fan_gpu_temp_attr.attr,
	&fan_sys_temp_attr.attr,
	&fan_cpu_rpm_attr.attr,
	&fan_gpu_rpm_attr.attr,
	&fan_sys_rpm_attr.attr,
	&fan_fans_count_attr.attr,
	&fan_profile_attr.attr,
	&fan_status_attr.attr,
	NULL,
};

static struct attribute_group fan_attr_group = {
	.attrs = fan_attrs,
};

static int tb_feature_fan_init(struct kobject *parent)
{
	int ret;

	fan_kobj = kobject_create_and_add("fan", parent);
	if (!fan_kobj) {
		pr_err("thunderobot: failed to create fan sysfs directory\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(fan_kobj, &fan_attr_group);
	if (ret) {
		pr_err("thunderobot: failed to create fan sysfs group\n");
		kobject_put(fan_kobj);
		fan_kobj = NULL;
		return ret;
	}

	fan_detect_capabilities();
	pr_info("thunderobot: fan feature registered (fans=%u, profile=%u)\n",
		fans_count, fan_profile_id);
	return 0;
}

static void tb_feature_fan_exit(void)
{
	if (cur_fan_mode == 1) {
		/* Restore EC auto fan control on module unload */
		fan_set_mode_raw(0);
		fan_set_speed_raw(0xFF, 0xFF, 0xFF);
	}

	if (fan_kobj) {
		sysfs_remove_group(fan_kobj, &fan_attr_group);
		kobject_put(fan_kobj);
		fan_kobj = NULL;
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
		.name = "power",
		.init = tb_feature_power_init,
		.exit = tb_feature_power_exit,
	},
	{
		.name = "fan",
		.init = tb_feature_fan_init,
		.exit = tb_feature_fan_exit,
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
