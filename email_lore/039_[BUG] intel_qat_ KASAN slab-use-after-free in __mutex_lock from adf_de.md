# [BUG] intel_qat: KASAN slab-use-after-free in __mutex_lock from adf_dev_up via IOCTL_START_ACCEL_DEV (syzkaller)

| Field | Value |
| :--- | :--- |
| From | Author A <author-a@example.com> |
| To | giovanni.cabiddu@intel.com, herbert@gondor.apana.org.au, davem@davemloft.net |
| Cc | qat-linux@intel.com, linux-crypto@vger.kernel.org, linux-kernel@vger.kernel.org |
| Date | Tue, 3 Feb 2026 11:59:10 +0800 (GMT+08:00) |
| Message-ID | `1bc08963.a48.19c21a77cf7.Coremail.author-a@example.com` |
| List-ID | <linux-crypto.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/1bc08963.a48.19c21a77cf7.Coremail.author-a@example.com/` |

---

Dear QAT / Crypto maintainers,

When using our customized Syzkaller to fuzz the upstream Linux kernel, we triggered the following crash in the Intel QAT driver.

HEAD commit:7d0a66e4bb9081d75c82ec4957c50034cb0ea449
Git tree: upstream
Kernel: 6.18.0 #9 PREEMPT(voluntary) (KASAN enabled)
Hardware: https://github.com/anonymous-artifact/Crashes/blob/main/c461d4626dd557638db007ae7cd4aba57718c0d2/c6xxvf_pci.c 
Output: https://github.com/anonymous-artifact/Crashes/blob/main/c461d4626dd557638db007ae7cd4aba57718c0d2/repro.txt
dmesg: https://github.com/anonymous-artifact/Crashes/blob/main/c461d4626dd557638db007ae7cd4aba57718c0d2/dmesg.txt
Kernel config: https://github.com/anonymous-artifact/Crashes/blob/main/6.18.config
C reproducer: https://github.com/anonymous-artifact/Crashes/blob/main/c461d4626dd557638db007ae7cd4aba57718c0d2/repro.c
Syz reproducer: https://github.com/anonymous-artifact/Crashes/blob/main/c461d4626dd557638db007ae7cd4aba57718c0d2/repro.syz

== Summary ==
KASAN reports a slab-use-after-free in __mutex_lock() while starting a QAT acceleration device via IOCTL_START_ACCEL_DEV on /dev/qat_adf_ctl. The call trace indicates the access originates from the Intel QAT driver path:
adf_ctl_ioctl() -> adf_dev_up() [intel_qat] -> __mutex_lock()

The kernel prints "Starting acceleration device qat_dev0" immediately before the crash. The KASAN report indicates the freed object belongs to the task_struct slab cache, suggesting a stale pointer is being dereferenced during mutex locking.

== Crash log (dmesg excerpt) ==
[ 230.005400] c6xxvf 0000:00:04.0: Starting acceleration device qat_dev0.
[ 230.015865] ==================================================================
[ 230.018251] BUG: KASAN: slab-use-after-free in __mutex_lock+0xd0a/0x1160
[ 230.020642] Read of size 4 at addr ffff888012ea1ab4 by task syz.1.18/4237
[ 230.023669] CPU: 1 UID: 0 PID: 4237 Comm: syz.1.18 Tainted: G D 6.18.0 #9 PREEMPT(voluntary)
[ 230.023680] Tainted: [D]=DIE
[ 230.023682] Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS rel-1.16.3-0-ga6ed6b701f0a-prebuilt.qemu.org 04/01/2014
[ 230.023688] Call Trace:
[ 230.023691]
[ 230.023694] dump_stack_lvl+0xdb/0x140
[ 230.023745] print_report+0xcb/0x610
[ 230.023781] kasan_report+0xca/0x100
[ 230.023800] __mutex_lock+0xd0a/0x1160
[ 230.024107] adf_dev_up+0x44/0x14c0 [intel_qat]
[ 230.024228] adf_ctl_ioctl+0x1d6/0x1080 [intel_qat]
[ 230.024618] __x64_sys_ioctl+0x194/0x210
[ 230.024628] do_syscall_64+0xc6/0x390
[ 230.024639] entry_SYSCALL_64_after_hwframe+0x77/0x7f
[ 230.024646] RIP: 0033:0x7f4ae220059d
[ 230.024690]

Allocated by task 850:
[ 230.107919] copy_process+0x49c/0x72b0
[ 230.110710] user_mode_thread+0xcd/0x110
[ 230.112049] call_usermodehelper_exec_work+0x72/0x190
[ 230.115085] worker_thread+0x683/0xe90
[ 230.117718] ret_from_fork+0x3a1/0x490

Freed by task 4176:
[ 230.128576] kmem_cache_free+0x2ad/0x620
[ 230.129886] rcu_core+0x846/0x1940
[ 230.133622] irq_exit_rcu+0xe/0x20
[ 230.136394] asm_sysvec_apic_timer_interrupt+0x1a/0x20

The buggy address belongs to:
[ 230.179816] The buggy address belongs to the object at ffff888012ea1a80
which belongs to the cache task_struct of size 6528
[ 230.184129] The buggy address is located 52 bytes inside of
freed 6528-byte region [ffff888012ea1a80, ffff888012ea3400)

== Reproducer (syz snippet) ==
r0 = openat$qat_adf_ctl(0xffffffffffffff9c, &(0x7f0000000000), 0x2, 0x0)
r1 = syz_open_dev$dri(0x0, 0x4, 0x400800)
ioctl$DRM_IOCTL_ADD_CTX(r1, 0xc0086420, &(0x7f0000000140))
ioctl$IOCTL_START_ACCEL_DEV(r0, 0x40096102, &(0x7f00000003c0))
ioctl$DRM_IOCTL_SET_MASTER(r1, 0x641e)
ioctl$DRM_IOCTL_MODE_GETPLANERESOURCES(r0, 0xc01064b5, 0x0)
ioctl$DRM_IOCTL_GEM_OPEN(r1, 0xc010640b, &(0x7f00000000c0))

Thank you for your time. Please let us know if you need the complete report/config/reproducer links.

Best regards,
Author A
