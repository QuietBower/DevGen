# RE: [BUG] KASAN: slab-use-after-free Read in adf_dev_up

| Field | Value |
| :--- | :--- |
| From | "Cabiddu, Giovanni" <giovanni.cabiddu@intel.com> |
| To | "author-b@example.com" <author-b@example.com>, Herbert Xu <herbert@gondor.apana.org.au> |
| Cc | qat-linux <qat-linux@intel.com>, "linux-crypto@vger.kernel.org" <linux-crypto@vger.kernel.org>, "linux-kernel@vger.kernel.org" <linux-kernel@vger.kernel.org>, "Cabiddu, Giovanni" <giovanni.cabiddu@intel.com> |
| Date | Mon, 12 Jan 2026 13:14:00 +0000 |
| Message-ID | `CY5PR11MB6366E30B590833A634B453018281A@CY5PR11MB6366.namprd11.prod.outlook.com` |
| In-Reply-To | `782a4d0e.c456.19bb25459b4.Coremail.author-b@example.com` |
| List-ID | <linux-crypto.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/CY5PR11MB6366E30B590833A634B453018281A@CY5PR11MB6366.namprd11.prod.outlook.com/` |

---

Thanks for the report. I'm looking at it.

Regards,

-- 
Giovanni

-----Original Message-----
From: Author B <author-b@example.com> 
Sent: Monday, January 12, 2026 1:11 PM
To: Cabiddu, Giovanni <giovanni.cabiddu@intel.com>; Herbert Xu <herbert@gondor.apana.org.au>
Cc: qat-linux <qat-linux@intel.com>; linux-crypto@vger.kernel.org; linux-kernel@vger.kernel.org
Subject: [BUG] KASAN: slab-use-after-free Read in adf_dev_up

Dear Maintainers,

When using our customized Syzkaller to fuzz the latest Linux kernel, the following crash was triggered.
HEAD commit:7d0a66e4bb9081d75c82ec4957c50034cb0ea449
git tree: upstream
Output:https://github.com/anonymous-artifact/crash/blob/main/report_c6xxvf.txt
Kernel config: https://github.com/anonymous-artifact/crash/blob/main/config.txt
C reproducer: https://github.com/anonymous-artifact/crash/blob/main/repro_c6xxvf.c
Syz reproducer: https://github.com/anonymous-artifact/crash/blob/main/repro_c6xxvf.syz

KASAN reports a slab use-after-free in the Intel QAT driver during adf_dev_up(). The driver accesses a names_cache object that has already been freed while performing mutex operations (__mutex_lock_common → mutex_optimistic_spin). The issue is triggered when opening /dev/qat_adf_ctl and performing an ioctl to start the accelerator device with minimal configuration. In short, the QAT driver reads freed memory during device initialization, leading to use-after-free. 

If you fix this issue, please add the following tag to the commit:
Reported-by: Author B <author-b@example.com>, Author C<author-c@example.com>, Author A<author-a-alt@example.com>

QAT: failed to copy from user cfg_data.
c6xxvf 0000:00:05.0: Starting acceleration device qat_dev0.
==================================================================
BUG: KASAN: slab-use-after-free in owner_on_cpu home/user/Fuzzer/third_tool/linux-6.18/include/linux/sched.h:2282 [inline]
BUG: KASAN: slab-use-after-free in mutex_can_spin_on_owner home/user/Fuzzer/third_tool/linux-6.18/kernel/locking/mutex.c:397 [inline]
BUG: KASAN: slab-use-after-free in mutex_optimistic_spin home/user/Fuzzer/third_tool/linux-6.18/kernel/locking/mutex.c:440 [inline]
BUG: KASAN: slab-use-after-free in __mutex_lock_common home/user/Fuzzer/third_tool/linux-6.18/kernel/locking/mutex.c:602 [inline]
BUG: KASAN: slab-use-after-free in __mutex_lock+0xd0a/0x1160 home/user/Fuzzer/third_tool/linux-6.18/kernel/locking/mutex.c:760
Read of size 4 at addr ffff8881125c9ab4 by task syz.2.6067/17362

CPU: 1 UID: 0 PID: 17362 Comm: syz.2.6067 Tainted: G      D             6.18.0 #3 PREEMPT(voluntary) 
Tainted: [D]=DIE
Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS rel-1.16.3-0-ga6ed6b701f0a-prebuilt.qemu.org 04/01/2014 Call Trace:
 <TASK>
 __dump_stack home/user/Fuzzer/third_tool/linux-6.18/lib/dump_stack.c:94 [inline]
 dump_stack_lvl+0xdb/0x140 home/user/Fuzzer/third_tool/linux-6.18/lib/dump_stack.c:120
 print_address_description home/user/Fuzzer/third_tool/linux-6.18/mm/kasan/report.c:378 [inline]
 print_report+0xcb/0x610 home/user/Fuzzer/third_tool/linux-6.18/mm/kasan/report.c:482
 kasan_report+0xca/0x100 home/user/Fuzzer/third_tool/linux-6.18/mm/kasan/report.c:595
 owner_on_cpu home/user/Fuzzer/third_tool/linux-6.18/include/linux/sched.h:2282 [inline]  mutex_can_spin_on_owner home/user/Fuzzer/third_tool/linux-6.18/kernel/locking/mutex.c:397 [inline]  mutex_optimistic_spin home/user/Fuzzer/third_tool/linux-6.18/kernel/locking/mutex.c:440 [inline]  __mutex_lock_common home/user/Fuzzer/third_tool/linux-6.18/kernel/locking/mutex.c:602 [inline]
 __mutex_lock+0xd0a/0x1160 home/user/Fuzzer/third_tool/linux-6.18/kernel/locking/mutex.c:760
 adf_dev_up+0x44/0x14c0 home/user/Fuzzer/third_tool/linux-6.18/drivers/crypto/intel/qat/qat_common/adf_init.c:473 [intel_qat]
 adf_ctl_ioctl+0x1d6/0x1080 [intel_qat]
 vfs_ioctl home/user/Fuzzer/third_tool/linux-6.18/fs/ioctl.c:51 [inline]  __do_sys_ioctl home/user/Fuzzer/third_tool/linux-6.18/fs/ioctl.c:597 [inline]  __se_sys_ioctl home/user/Fuzzer/third_tool/linux-6.18/fs/ioctl.c:583 [inline]
 __x64_sys_ioctl+0x194/0x210 home/user/Fuzzer/third_tool/linux-6.18/fs/ioctl.c:583
 do_syscall_x64 home/user/Fuzzer/third_tool/linux-6.18/arch/x86/entry/syscall_64.c:63 [inline]
 do_syscall_64+0xc6/0x390 home/user/Fuzzer/third_tool/linux-6.18/arch/x86/entry/syscall_64.c:94
 entry_SYSCALL_64_after_hwframe+0x77/0x7f
RIP: 0033:0x7fae87c4059d
Code: 02 b8 ff ff ff ff c3 66 0f 1f 44 00 00 f3 0f 1e fa 48 89 f8 48 89 f7 48 89 d6 48 89 ca 4d 89 c2 4d 89 c8 4c 8b 4c 24 08 0f 05 <48> 3d 01 f0 ff ff 73 01 c3 48 c7 c1 a8 ff ff ff f7 d8 64 89 01 48
RSP: 002b:00007fae86686f98 EFLAGS: 00000246 ORIG_RAX: 0000000000000010
RAX: ffffffffffffffda RBX: 00007fae87eb5fa0 RCX: 00007fae87c4059d
RDX: 0000200000000000 RSI: 0000000040096102 RDI: 0000000000000003
RBP: 00007fae87cde078 R08: 0000000000000000 R09: 0000000000000000
R10: 0000000000000000 R11: 0000000000000246 R12: 0000000000000000
R13: 00007fae87eb6038 R14: 00007fae87eb5fa0 R15: 00007fae86667000  </TASK>

Thanks,
Author B
