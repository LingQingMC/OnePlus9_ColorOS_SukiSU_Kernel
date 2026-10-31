#include <linux/compiler.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/thread_info.h>
#include <linux/seccomp.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#include "policy/allowlist.h"
#include "hook/setuid_hook.h"
#include "klog.h" // IWYU pragma: keep
#include "manager/manager_identity.h"
#include "infra/seccomp_cache.h"
#include "supercall/supercall.h"
#include "hook/tp_marker.h"
#include "feature/kernel_umount.h"
#include "feature/mount_hide.h"

int ksu_handle_setresuid(uid_t old_uid, uid_t new_uid)
{
    // we rely on the fact that zygote always call setresuid(3) with same uids

    pr_info("handle_setresuid from %d to %d\n", old_uid, new_uid);

    if (unlikely(is_uid_manager(new_uid))) {
        spin_lock_irq(&current->sighand->siglock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
        /*
         * ksu_seccomp_allow_cache() needs struct seccomp_filter.action_cache
         * (5.9+); on 5.4 it is a no-op, so the reboot() driver-fd install
         * syscall stays trapped by SECCOMP_RET_TRAP.
         *
         * Clearing TIF_SECCOMP alone is not enough: fork's copy_seccomp()
         * re-sets it for any child whose seccomp.mode != SECCOMP_MODE_DISABLED,
         * so the exec'd ksud would be filtered again.  Flip the mode as well --
         * secure_computing() then short-circuits on the (cleared) flag for this
         * task, copy_seccomp() skips the flag for children, and exec never
         * calls seccomp_assign_mode() so it stays cleared.
         *
         * Coarser than 5.5+'s "allow __NR_reboot only", but the trust boundary
         * is unchanged: manager appid only, which is already full-privilege.
         */
        current->seccomp.mode = SECCOMP_MODE_DISABLED;
        clear_thread_flag(TIF_SECCOMP);
#else
        ksu_seccomp_allow_cache(current->seccomp.filter, __NR_reboot);
#endif
        ksu_set_task_tracepoint_flag(current);
        spin_unlock_irq(&current->sighand->siglock);

        pr_info("install fd for manager: %d\n", new_uid);
        ksu_install_fd();
        return 0;
    }

    if (ksu_is_allow_uid_for_current(new_uid)) {
        if (current->seccomp.mode == SECCOMP_MODE_FILTER && current->seccomp.filter) {
            spin_lock_irq(&current->sighand->siglock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
            /* 5.4: same downgrade as the manager branch above -- the allowlisted
             * app must be able to fork+exec ksud, which also needs reboot(). */
            current->seccomp.mode = SECCOMP_MODE_DISABLED;
            clear_thread_flag(TIF_SECCOMP);
#else
            ksu_seccomp_allow_cache(current->seccomp.filter, __NR_reboot);
#endif
            spin_unlock_irq(&current->sighand->siglock);
        }
        ksu_set_task_tracepoint_flag(current);
    } else {
        ksu_clear_task_tracepoint_flag_if_needed(current);
    }

    // Handle kernel umount
    ksu_handle_umount(old_uid, new_uid);

    return 0;
}

void __init ksu_setuid_hook_init(void)
{
    ksu_kernel_umount_init();
    ksu_mount_hide_init();
}

void __exit ksu_setuid_hook_exit(void)
{
    pr_info("ksu_core_exit\n");
    ksu_kernel_umount_exit();
    ksu_mount_hide_exit();
}
