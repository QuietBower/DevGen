# Re: [BUG] RCU stall in mac80211_hwsim during clone() syscall on 6.18.0

| Field | Value |
| :--- | :--- |
| From | Joel Fernandes <joelagnelf@nvidia.com> |
| To | Author B <author-b@example.com>, linux-wireless@vger.kernel.org |
| Cc | paulmck@kernel.org, johannes@sipsolutions.net, linux-kernel@vger.kernel.org, rcu@vger.kernel.org, Benjamin Berg <benjamin@sipsolutions.net> |
| Date | Sat, 3 Jan 2026 23:42:41 -0500 |
| Message-ID | `c92ccb99-3766-46d3-8a8a-1e8a6cb79d07@nvidia.com` |
| In-Reply-To | `37b368f9.92b3.19b87161d5b.Coremail.author-b@example.com` |
| List-ID | <linux-wireless.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/c92ccb99-3766-46d3-8a8a-1e8a6cb79d07@nvidia.com/` |

---

On 1/3/2026 10:38 PM, Author B wrote:
> Dear Developers,
> I am reporting an RCU CPU stall detected during fuzzing with Syzkaller on Linux 6.18.0-rc. 
> 
> Technical Summary: The stall occurs on CPU 0 while executing a clone syscall. The task was interrupted by a timer softirq which then invoked mac80211_hwsim_beacon. The trace shows it is stuck waiting for a spinlock in mac80211_hwsim_tx_frame_no_nl. 
> 
> Key Logs:
> RCU GP Starvation: 10509 jiffies
> Call Trace: native_queued_spin_lock_slowpath <- mac80211_hwsim_tx_frame_no_nl <- hrtimer_run_softirq
> Environment:
[...]
>  native_queued_spin_lock_slowpath+0xae/0x9c0
>  debug_spin_lock_before home/user/Fuzzer/third_tool/linux-6.18/kernel/locking/spinlock_debug.c:87 [inline]
>  do_raw_spin_lock+0x20d/0x2b0 home/user/Fuzzer/third_tool/linux-6.18/kernel/locking/spinlock_debug.c:115
>  mac80211_hwsim_tx_frame_no_nl.isra.0+0x6f1/0x12c0

This seems to be a case of softirq context acquiring a lock without
spin_lock_bh. That explains the stall.

A quick search on the mailing list showed this was fixed recently:
`[ARCHIVE] https://lore.kernel.org/all/20251128151537.0b4006217137.I688f8b19346e94c1f8de0cdadde072054d4b861c@changeid/`

Can you try the patch and see if it fixes? Also CC'ing the author of the patch.

 - Joel



>  mac80211_hwsim_tx_frame+0x1ed/0x2a0
>  mac80211_hwsim_beacon_tx+0x56d/0x9e0
>  __iterate_interfaces+0x2e0/0x650 home/user/Fuzzer/third_tool/linux-6.18/net/mac80211/util.c:761
>  ieee80211_iterate_active_interfaces_atomic+0x71/0x1b0 home/user/Fuzzer/third_tool/linux-6.18/net/mac80211/util.c:797
>  mac80211_hwsim_beacon+0x105/0x1b0
>  __run_hrtimer home/user/Fuzzer/third_tool/linux-6.18/kernel/time/hrtimer.c:1779 [inline]
>  __hrtimer_run_queues+0x1f5/0xb30 home/user/Fuzzer/third_tool/linux-6.18/kernel/time/hrtimer.c:1841
>  __hrtimer_get_next_event home/user/Fuzzer/third_tool/linux-6.18/kernel/time/hrtimer.c:578 [inline]
>  hrtimer_update_softirq_timer home/user/Fuzzer/third_tool/linux-6.18/kernel/time/hrtimer.c:1193 [inline]
>  hrtimer_run_softirq+0x17f/0x350 home/user/Fuzzer/third_tool/linux-6.18/kernel/time/hrtimer.c:1861
>  _test_bit home/user/Fuzzer/third_tool/linux-6.18/include/asm-generic/bitops/instrumented-non-atomic.h:141 [inline]
>  cpumask_test_cpu home/user/Fuzzer/third_tool/linux-6.18/include/linux/cpumask.h:646 [inline]
>  cpu_online home/user/Fuzzer/third_tool/linux-6.18/include/linux/cpumask.h:1205 [inline]
>  trace_softirq_exit home/user/Fuzzer/third_tool/linux-6.18/include/trace/events/irq.h:142 [inline]
>  handle_softirqs+0x1d4/0x870 home/user/Fuzzer/third_tool/linux-6.18/kernel/softirq.c:623
>  __irq_exit_rcu+0x109/0x170 home/user/Fuzzer/third_tool/linux-6.18/arch/x86/include/asm/bitops.h:202
>  irq_exit_rcu+0x9/0x30 home/user/Fuzzer/third_tool/linux-6.18/kernel/softirq.c:738
>  sysvec_apic_timer_interrupt+0xa8/0xc0 home/user/Fuzzer/third_tool/linux-6.18/arch/x86/kernel/apic/apic.c:2145
>  </IRQ>
>  <TASK>
>  asm_sysvec_apic_timer_interrupt+0x1a/0x20 home/user/Fuzzer/third_tool/linux-6.18/arch/x86/include/asm/idtentry.h:697
> RIP: 0010:__sanitizer_cov_trace_pc+0x4b/0x80 home/user/Fuzzer/third_tool/linux-6.18/kernel/kcov.c:217
> Code: 74 1d a9 00 00 0f 00 75 16 a9 00 00 f0 00 74 05 c3 cc cc cc cc 8b 82 7c 16 00 00 85 c0 75 05 e9 5b dc 89 09 8b 82 58 16 00 00 <83> f8 02 75 f0 48 8b 8a 60 16 00 00 8b 92 5c 16 00 00 48 8b 01 48
> RSP: 0018:ffffc90002d47698 EFLAGS: 00000246
> RAX: 0000000000000000 RBX: 0000000000000000 RCX: ffffffff82075ed9
> RDX: ffff88802a758000 RSI: ffffffff82075ee7 RDI: 0000000000000007
> RBP: dffffc0000000000 R08: 0000000000000001 R09: fffff940002a92c6
> R10: 0000000000000000 R11: 0000000000000000 R12: ffffea0001549630
> R13: ffffea0001549600 R14: ffffea0001549618 R15: ffffea0001549600
>  copy_pte_range home/user/Fuzzer/third_tool/linux-6.18/mm/memory.c:1284 [inline]
>  copy_pmd_range home/user/Fuzzer/third_tool/linux-6.18/mm/memory.c:1390 [inline]
>  copy_pud_range home/user/Fuzzer/third_tool/linux-6.18/mm/memory.c:1427 [inline]
>  copy_p4d_range home/user/Fuzzer/third_tool/linux-6.18/mm/memory.c:1451 [inline]
>  copy_page_range+0x1c67/0x63a0 home/user/Fuzzer/third_tool/linux-6.18/mm/memory.c:1539
>  rwsem_assert_held_write home/user/Fuzzer/third_tool/linux-6.18/include/linux/rwsem.h:215 [inline]
>  mmap_assert_write_locked home/user/Fuzzer/third_tool/linux-6.18/include/linux/mmap_lock.h:76 [inline]
>  vma_end_write_all home/user/Fuzzer/third_tool/linux-6.18/include/linux/mmap_lock.h:347 [inline]
>  mmap_write_unlock home/user/Fuzzer/third_tool/linux-6.18/include/linux/mmap_lock.h:354 [inline]
>  dup_mmap+0xd9e/0x20d0 home/user/Fuzzer/third_tool/linux-6.18/mm/mmap.c:1872
>  futex_init_task home/user/Fuzzer/third_tool/linux-6.18/include/linux/futex.h:73 [inline]
>  copy_process+0x36ba/0x76c0 home/user/Fuzzer/third_tool/linux-6.18/kernel/fork.c:2230
>  kernel_clone+0xea/0x8b0 home/user/Fuzzer/third_tool/linux-6.18/kernel/fork.c:2609
>  __do_sys_clone+0xce/0x120 home/user/Fuzzer/third_tool/linux-6.18/kernel/fork.c:2750
>  do_syscall_64+0xcb/0xfa0 home/user/Fuzzer/third_tool/linux-6.18/arch/x86/entry/syscall_64.c:99
>  entry_SYSCALL_64_after_hwframe+0x77/0x7f
> RIP: 0033:0x7f00649a6d07
> Code: 00 00 90 f3 0f 1e fa 64 48 8b 04 25 10 00 00 00 45 31 c0 31 d2 31 f6 bf 11 00 20 01 4c 8d 90 d0 02 00 00 b8 38 00 00 00 0f 05 <48> 3d 00 f0 ff ff 77 41 41 89 c0 85 c0 75 2c 64 48 8b 04 25 10 00
> RSP: 002b:00007ffe2d0a2b98 EFLAGS: 00000246 ORIG_RAX: 0000000000000038
> RAX: ffffffffffffffda RBX: 00007f006575d680 RCX: 00007f00649a6d07
> RDX: 0000000000000000 RSI: 0000000000000000 RDI: 0000000001200011
> RBP: 0000000000000001 R08: 0000000000000000 R09: 0000000000000001
> R10: 0000555572e6d7d0 R11: 0000000000000246 R12: 0000000000000000
> R13: 0000000000000032 R14: 00007ffe2d0a2d80 R15: 00000000000e717e
>  </TASK>
> rcu: rcu_preempt kthread starved for 10509 jiffies! g98817 f0x0 RCU_GP_WAIT_FQS(5) ->state=0x0 ->cpu=0
> rcu: 	Unless rcu_preempt kthread gets sufficient CPU time, OOM is now expected behavior.
> rcu: RCU grace-period kthread stack dump:
> task:rcu_preempt     state:R  running task     stack:28520 pid:16    tgid:16    ppid:2      task_flags:0x208040 flags:0x00080000
> Call Trace:
>  <TASK>
>  sched_info_arrive home/user/Fuzzer/third_tool/linux-6.18/kernel/sched/stats.h:267 [inline]
>  sched_info_switch home/user/Fuzzer/third_tool/linux-6.18/kernel/sched/stats.h:330 [inline]
>  prepare_task_switch home/user/Fuzzer/third_tool/linux-6.18/kernel/sched/core.c:5122 [inline]
>  context_switch home/user/Fuzzer/third_tool/linux-6.18/kernel/sched/core.c:5272 [inline]
>  __schedule+0x1044/0x5bb0 home/user/Fuzzer/third_tool/linux-6.18/kernel/sched/core.c:6929
>  __schedule_loop home/user/Fuzzer/third_tool/linux-6.18/kernel/sched/core.c:7011 [inline]
>  schedule+0xe7/0x3a0 home/user/Fuzzer/third_tool/linux-6.18/kernel/sched/core.c:7026
>  schedule_timeout+0x113/0x280 home/user/Fuzzer/third_tool/linux-6.18/kernel/time/sleep_timeout.c:98
>  rcu_gp_fqs_check_wake home/user/Fuzzer/third_tool/linux-6.18/kernel/rcu/tree.c:2007 [inline]
>  rcu_gp_fqs_loop+0x18c/0xa00 home/user/Fuzzer/third_tool/linux-6.18/kernel/rcu/tree.c:2083
>  rcu_gp_kthread+0x26f/0x370 home/user/Fuzzer/third_tool/linux-6.18/kernel/rcu/tree.c:2280
>  kthread+0x3d0/0x780 home/user/Fuzzer/third_tool/linux-6.18/kernel/kthread.c:463
>  ret_from_fork+0x676/0x7d0 home/user/Fuzzer/third_tool/linux-6.18/arch/x86/kernel/process.c:195
>  ret_from_fork_asm+0x1a/0x30 home/user/Fuzzer/third_tool/linux-6.18/arch/x86/entry/entry_64.S:245
>  </TASK>
> 
> Best regards,
> Author B
> 
>
