/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thunderobot.h - Thunderobot laptop platform driver common definitions
 *
 * Shared ACPI WSAA interface for GPU mode switching and LED control.
 *
 * Copyright (C) 2026 HollowDream
 */

#ifndef _THUNDEROBOT_H_
#define _THUNDEROBOT_H_

#include <linux/types.h>
#include <linux/unaligned.h>

/* ACPI interface */
#define TB_GWMI_PATH		"\\_SB.GWMI"
#define TB_WSAA_METHOD		"WSAA"

/* SMI command constants */
#define TB_SMI_CMD_GET		0xFA00
#define TB_SMI_CMD_SET		0xFB00

/* SMI function codes */
#define TB_SMI_FUNC_LED		0x0100
#define TB_SMI_FUNC_HWINFO	0x0200
#define TB_SMI_FUNC_GPU		0x0203
#define TB_SMI_FUNC_ADAPTER	0x0206
#define TB_SMI_FUNC_PERF	0x0300

/* SMI buffer size */
#define TB_SMI_BUF_SIZE		32

/*
 * Build a 32-byte SMI request buffer.
 *
 * Layout (little-endian):
 *   [0-1]   a0: command (0xFA00=get, 0xFB00=set)
 *   [2-3]   a1: function code
 *   [4-7]   a2: argument 1
 *   [8-11]  a3: argument 2
 *   [12-31] reserved
 */
static inline void tb_build_smi(u8 *buf, u16 cmd, u16 func, u32 arg1, u32 arg2)
{
	memset(buf, 0, TB_SMI_BUF_SIZE);
	put_unaligned_le16(cmd, &buf[0]);
	put_unaligned_le16(func, &buf[2]);
	put_unaligned_le32(arg1, &buf[4]);
	put_unaligned_le32(arg2, &buf[8]);
}

/* ACPI WSAA call - defined in thunderobot-core.c */
int tb_wsaa_call(const u8 *in_buf, u8 *out_buf);

/* Parent sysfs kobject - defined in thunderobot-core.c */
struct kobject *tb_get_kobj(void);

#endif /* _THUNDEROBOT_H_ */
