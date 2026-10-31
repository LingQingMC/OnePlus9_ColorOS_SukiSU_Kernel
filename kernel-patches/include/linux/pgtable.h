/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * 兼容层：linux/pgtable.h 自 Linux 5.8 起才独立存在。
 * 本内核为 5.4，页表接口位于 asm/pgtable.h。
 * 仅供 SukiSU 等外部模块包含，避免 fatal error。
 */
#ifndef _LINUX_PGTABLE_COMPAT_H
#define _LINUX_PGTABLE_COMPAT_H

#include <asm/pgtable.h>
#include <asm/pgtable-types.h>

#endif /* _LINUX_PGTABLE_COMPAT_H */
