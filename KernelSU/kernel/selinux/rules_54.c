// SPDX-License-Identifier: GPL-2.0-only
/*
 * SukiSU 5.4 runtime SELinux policy injection (in-place).
 *
 * The main-branch rules.c operates on `struct selinux_policy` (introduced in
 * 5.5) and swaps selinux_state.policy under policy_mutex.  On 5.4 the policy
 * lives inside selinux_state.ss->policydb (an embedded struct, not a pointer),
 * protected by selinux_state.ss->policy_rwlock, and there is no
 * struct selinux_policy at all.
 *
 * We therefore inject rules IN PLACE into the live policydb.  The low-level
 * primitives (ksu_type / ksu_allow / ksu_permissive / ...) in sepolicy.c only
 * depend on `struct policydb *`, which exists on 5.4, so they are reused
 * directly.  We skip the dup/destroy cycle (ksu_dup_sepolicy/ksu_destroy_sepolicy
 * are 5.5+ only): instead we hold the write lock, mutate the live db, then
 * bump latest_granting and reset the AVC cache so the new rules take effect.
 *
 * The SID table is untouched: new types get fresh SIDs lazily assigned by
 * context_struct_to_sid() on first use (e.g. when setup_ksu_cred() resolves
 * "u:r:ksu:s0").  This mirrors what security_load_policy() does structurally.
 */
#include <linux/version.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/lockdep.h>

#include "security.h"
#include "avc_ss.h"
#include "ss/services.h"
#include "ss/policydb.h"
#include "ss/symtab.h"
#include "klog.h" // IWYU pragma: keep
#include "selinux.h"
#include "sepolicy.h"
#include "xfrm.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 5, 0)

#define ALL NULL

static void reset_avc_cache_54(void)
{
    struct selinux_avc *avc = selinux_state.avc;
    u32 seqno = ++selinux_state.ss->latest_granting;

    avc_ss_reset(avc, seqno);
    selnl_notify_policyload(seqno);
    selinux_status_update_policyload(&selinux_state, seqno);
    selinux_xfrm_notify_policyload();
}

void apply_kernelsu_rules(void)
{
    struct policydb *db = &selinux_state.ss->policydb;

    if (!getenforce()) {
        pr_info("SELinux permissive or disabled, apply rules!\n");
    }

    write_lock_irq(&selinux_state.ss->policy_rwlock);

    ksu_type(db, KERNEL_SU_DOMAIN, "domain");
    ksu_permissive(db, KERNEL_SU_DOMAIN);
    ksu_typeattribute(db, KERNEL_SU_DOMAIN, "mlstrustedsubject");
    ksu_typeattribute(db, KERNEL_SU_DOMAIN, "netdomain");
    ksu_typeattribute(db, KERNEL_SU_DOMAIN, "bluetoothdomain");

    // Create unconstrained file type
    ksu_type(db, KERNEL_SU_FILE, "file_type");
    ksu_typeattribute(db, KERNEL_SU_FILE, "mlstrustedobject");
    ksu_allow(db, "domain", KERNEL_SU_FILE, ALL, ALL);

    // allow all!
    ksu_allow(db, KERNEL_SU_DOMAIN, ALL, ALL, ALL);

    // allow us do any ioctl
    if (db->policyvers >= POLICYDB_VERSION_XPERMS_IOCTL) {
        ksu_allowxperm(db, KERNEL_SU_DOMAIN, ALL, "blk_file", ALL);
        ksu_allowxperm(db, KERNEL_SU_DOMAIN, ALL, "fifo_file", ALL);
        ksu_allowxperm(db, KERNEL_SU_DOMAIN, ALL, "chr_file", ALL);
        ksu_allowxperm(db, KERNEL_SU_DOMAIN, ALL, "file", ALL);
    }

    // our ksud triggered by init
    ksu_allow(db, "init", KERNEL_SU_DOMAIN, ALL, ALL);

    // copied from Magisk rules
    // suRights
    ksu_allow(db, "servicemanager", KERNEL_SU_DOMAIN, "dir", "search");
    ksu_allow(db, "servicemanager", KERNEL_SU_DOMAIN, "dir", "read");
    ksu_allow(db, "servicemanager", KERNEL_SU_DOMAIN, "file", "open");
    ksu_allow(db, "servicemanager", KERNEL_SU_DOMAIN, "file", "read");
    ksu_allow(db, "servicemanager", KERNEL_SU_DOMAIN, "process", "getattr");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "process", "sigchld");

    // allowLog
    ksu_allow(db, "logd", KERNEL_SU_DOMAIN, "dir", "search");
    ksu_allow(db, "logd", KERNEL_SU_DOMAIN, "file", "read");
    ksu_allow(db, "logd", KERNEL_SU_DOMAIN, "file", "open");
    ksu_allow(db, "logd", KERNEL_SU_DOMAIN, "file", "getattr");

    // dumpsys, send fd
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "fd", "use");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "fifo_file", "write");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "fifo_file", "read");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "fifo_file", "open");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "fifo_file", "getattr");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "unix_stream_socket", "read");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "unix_stream_socket", "write");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "unix_stream_socket", "connectto");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "unix_stream_socket", "getopt");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "unix_stream_socket", "getattr");

    // use memfd created by su domain
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "memfd_file", "execute");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "memfd_file", "getattr");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "memfd_file", "map");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "memfd_file", "read");
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "memfd_file", "write");

    // bootctl
    ksu_allow(db, "hwservicemanager", KERNEL_SU_DOMAIN, "dir", "search");
    ksu_allow(db, "hwservicemanager", KERNEL_SU_DOMAIN, "file", "read");
    ksu_allow(db, "hwservicemanager", KERNEL_SU_DOMAIN, "file", "open");
    ksu_allow(db, "hwservicemanager", KERNEL_SU_DOMAIN, "process", "getattr");

    // Allow all binder transactions
    ksu_allow(db, "domain", KERNEL_SU_DOMAIN, "binder", ALL);

    // Allow system server kill su process
    ksu_allow(db, "system_server", KERNEL_SU_DOMAIN, "process", "getpgid");
    ksu_allow(db, "system_server", KERNEL_SU_DOMAIN, "process", "sigkill");

    write_unlock_irq(&selinux_state.ss->policy_rwlock);

    reset_avc_cache_54();

    pr_info("SukiSU 5.4: sepolicy rules applied in-place\n");
}

/*
 * Called after security_load_policy() replaces the live policydb (memcpy)
 * and sidtab.  ColorOS compiles+loads its policy at runtime, so an in-place
 * injection done earlier (e.g. from the init second_stage hook) can be wiped
 * by a later reload.  Re-apply the rules so u:r:ksu:s0 survives the reload,
 * then refresh the cached SIDs and ksu_cred so sucompat keeps working.
 */
void ksu_reapply_sepolicy_after_reload(void)
{
    apply_kernelsu_rules();
    cache_sid();
    setup_ksu_cred();
}

/*
 * On 5.4 we mutate the live policydb in place, so the manager-driven
 * sepolicy.rule batch (which needs a swappable selinux_policy) is not
 * supported.  Return -EOPNOTSUPP so the manager degrades gracefully.
 */
int handle_sepolicy(void __user *user_data, u64 data_len)
{
    (void)user_data;
    (void)data_len;
    return -EOPNOTSUPP;
}

#endif /* < 5.5 */
