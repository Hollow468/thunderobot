// SPDX-License-Identifier: GPL-2.0
/*
 * thunderobot-led.c - Thunderobot LED control
 *
 * Controls keyboard LEDs, logo, and trunk lights via ACPI WSAA method.
 * Migrated from tb-led project.
 *
 * Copyright (C) 2026 HollowDream
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include "include/thunderobot.h"

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

/* Current state */
static u8 cur_zone = TB_ZONE_KB_ALL;
static u8 cur_mode = TB_LED_MODE_STATIC;
static u8 cur_brightness = 15;
static u8 cur_red = 255, cur_green = 255, cur_blue = 255;

/*
 * Construct LED data value from components.
 *
 * Bit layout of the 32-bit value:
 *   [31:28] mode
 *   [27:24] brightness
 *   [23:16] red
 *   [15:8]  green
 *   [7:0]   blue
 */
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

/* ---- sysfs attributes ---- */

static ssize_t mode_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", cur_mode);
}

static ssize_t mode_store(struct kobject *k, struct kobj_attribute *a,
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

static struct kobj_attribute mode_attr =
	__ATTR(mode, 0644, mode_show, mode_store);

static ssize_t brightness_show(struct kobject *k, struct kobj_attribute *a,
				char *buf)
{
	return sysfs_emit(buf, "%u\n", cur_brightness);
}

static ssize_t brightness_store(struct kobject *k, struct kobj_attribute *a,
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

static struct kobj_attribute brightness_attr =
	__ATTR(brightness, 0644, brightness_show, brightness_store);

static ssize_t color_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%02x%02x%02x\n", cur_red, cur_green, cur_blue);
}

static ssize_t color_store(struct kobject *k, struct kobj_attribute *a,
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

static struct kobj_attribute color_attr =
	__ATTR(color, 0644, color_show, color_store);

static ssize_t zone_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", cur_zone);
}

static ssize_t zone_store(struct kobject *k, struct kobj_attribute *a,
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

static struct kobj_attribute zone_attr =
	__ATTR(zone, 0644, zone_show, zone_store);

static ssize_t status_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	u32 led_data;
	int ret;

	ret = led_get(&led_data);
	if (ret)
		return ret;

	return sysfs_emit(buf, "raw=0x%08x mode=%u brightness=%u rgb=(%u,%u,%u)\n",
			  led_data,
			  (led_data >> 28) & 0xF,
			  (led_data >> 24) & 0xF,
			  (led_data >> 16) & 0xFF,
			  (led_data >> 8) & 0xFF,
			  led_data & 0xFF);
}

static struct kobj_attribute status_attr =
	__ATTR(status, 0444, status_show, NULL);

static ssize_t apply_store(struct kobject *k, struct kobj_attribute *a,
			    const char *buf, size_t count)
{
	int ret;

	ret = led_set(cur_zone, cur_mode, cur_brightness,
		      cur_red, cur_green, cur_blue);
	return ret ? ret : count;
}

static struct kobj_attribute apply_attr =
	__ATTR(apply, 0200, NULL, apply_store);

static struct attribute *led_attrs[] = {
	&mode_attr.attr,
	&brightness_attr.attr,
	&color_attr.attr,
	&zone_attr.attr,
	&status_attr.attr,
	&apply_attr.attr,
	NULL,
};

static struct attribute_group led_attr_group = {
	.attrs = led_attrs,
};

static int __init thunderobot_led_init(void)
{
	struct kobject *tb_kobj;
	int ret;

	tb_kobj = tb_get_kobj();
	if (!tb_kobj) {
		pr_err("thunderobot-led: thunderobot-core not loaded\n");
		return -ENODEV;
	}

	led_kobj = kobject_create_and_add("led", tb_kobj);
	if (!led_kobj) {
		pr_err("thunderobot-led: failed to create led sysfs directory\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(led_kobj, &led_attr_group);
	if (ret) {
		pr_err("thunderobot-led: failed to create sysfs group\n");
		kobject_put(led_kobj);
		led_kobj = NULL;
		return ret;
	}

	pr_info("thunderobot-led: module loaded\n");
	return 0;
}

static void __exit thunderobot_led_exit(void)
{
	if (led_kobj) {
		sysfs_remove_group(led_kobj, &led_attr_group);
		kobject_put(led_kobj);
		led_kobj = NULL;
	}
	pr_info("thunderobot-led: module unloaded\n");
}

module_init(thunderobot_led_init);
module_exit(thunderobot_led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("HollowDream");
MODULE_DESCRIPTION("Thunderobot LED control via ACPI WSAA");
MODULE_SOFTDEP("pre: thunderobot_core");
