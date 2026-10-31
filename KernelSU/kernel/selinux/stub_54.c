// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux 5.4 stubs for SukiSU's runtime SELinux policy machinery.
 *
 * selinux_hide.c operates on `struct selinux_policy` (5.5+), so on 5.4 the
 * whole selinux_hide path is stubbed out.  The policy INJECTION path
 * (apply_kernelsu_rules / handle_sepolicy) is instead implemented for 5.4 in
 * rules_54.c, which mutates selinux_state.ss->policydb in place.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/version.h>
#include "selinux/selinux.h"
#include "feature/selinux_hide.h"

void ksu_selinux_hide_init(void)
{
}

void ksu_selinux_hide_exit(void)
{
}

void ksu_selinux_hide_drop_backup_if_unused(void)
{
}

void ksu_selinux_hide_handle_second_stage(void)
{
}

void ksu_selinux_hide_handle_post_fs_data(void)
{
}
