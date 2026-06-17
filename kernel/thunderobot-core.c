// SPDX-License-Identifier: GPL-2.0
/*
 * thunderobot-core.c - Thunderobot ACPI WSAA communication layer
 *
 * Shared module providing the WSAA ACPI interface for sub-modules.
 *
 * Copyright (C) 2026 HollowDream
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include "include/thunderobot.h"

static acpi_handle h_gwmi;
static struct kobject *tb_kobj;

/**
 * tb_get_kobj - Get the parent /sys/kernel/thunderobot kobject
 *
 * Returns the shared kobject, or NULL if core is not loaded.
 */
struct kobject *tb_get_kobj(void)
{
	return tb_kobj;
}
EXPORT_SYMBOL_GPL(tb_get_kobj);

/**
 * tb_wsaa_call - Call the ACPI WSAA method
 * @in_buf:  32-byte input SMI buffer
 * @out_buf: 32-byte output buffer (or NULL if no response needed)
 *
 * Returns 0 on success, negative errno on failure.
 */
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

static int __init thunderobot_core_init(void)
{
	acpi_status st;

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

	pr_info("thunderobot: core module loaded\n");
	return 0;
}

static void __exit thunderobot_core_exit(void)
{
	if (tb_kobj) {
		kobject_put(tb_kobj);
		tb_kobj = NULL;
	}
	pr_info("thunderobot: core module unloaded\n");
}

module_init(thunderobot_core_init);
module_exit(thunderobot_core_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("HollowDream");
MODULE_DESCRIPTION("Thunderobot platform driver - ACPI WSAA core");
