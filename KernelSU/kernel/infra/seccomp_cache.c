#include <linux/version.h>
#include <linux/fs.h>
#include <linux/nsproxy.h>
#include <linux/sched/task.h>
#include <linux/uaccess.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include "klog.h" // IWYU pragma: keep
#include "infra/seccomp_cache.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)

struct action_cache {
	DECLARE_BITMAP(allow_native, SECCOMP_ARCH_NATIVE_NR);
#ifdef SECCOMP_ARCH_COMPAT
	DECLARE_BITMAP(allow_compat, SECCOMP_ARCH_COMPAT_NR);
#endif
};

struct seccomp_filter {
	refcount_t refs;
	refcount_t users;
	bool log;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	bool wait_killable_recv;
#endif
	struct action_cache cache;
	struct seccomp_filter *prev;
	struct bpf_prog *prog;
	struct notification *notif;
	struct mutex notify_lock;
	wait_queue_head_t wqh;
};

void ksu_seccomp_clear_cache(struct seccomp_filter *filter, int nr)
{
	if (!filter) {
		return;
	}

	if (nr >= 0 && nr < SECCOMP_ARCH_NATIVE_NR) {
		clear_bit(nr, filter->cache.allow_native);
	}

#ifdef SECCOMP_ARCH_COMPAT
	if (nr >= 0 && nr < SECCOMP_ARCH_COMPAT_NR) {
		clear_bit(nr, filter->cache.allow_compat);
	}
#endif
}

void ksu_seccomp_allow_cache(struct seccomp_filter *filter, int nr)
{
	if (!filter) {
		return;
	}

	if (nr >= 0 && nr < SECCOMP_ARCH_NATIVE_NR) {
		set_bit(nr, filter->cache.allow_native);
	}

#ifdef SECCOMP_ARCH_COMPAT
	if (nr >= 0 && nr < SECCOMP_ARCH_COMPAT_NR) {
		set_bit(nr, filter->cache.allow_compat);
	}
#endif
}

#else /* LINUX_VERSION_CODE < 5.9.0 */

/*
 * Kernel 5.4 compatibility stub.
 *
 * On 5.4 the layout of struct seccomp_filter is defined privately in
 * kernel/seccomp.c and has neither action_cache nor the 6.x field order.
 * Mirroring that struct here and casting current->seccomp.filter to it
 * would read out of bounds and panic the kernel, so both helpers are
 * degraded to no-ops.
 *
 * Effect: the reboot syscall is no longer force-allowed through the
 * per-task seccomp filter after setresuid. Root functionality itself is
 * unaffected.
 */
void ksu_seccomp_clear_cache(struct seccomp_filter *filter, int nr)
{
	(void)filter;
	(void)nr;
}

void ksu_seccomp_allow_cache(struct seccomp_filter *filter, int nr)
{
	(void)filter;
	(void)nr;
}

#endif /* LINUX_VERSION_CODE >= 5.9.0 */
