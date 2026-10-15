# 🐛 Context-recursion deadlock in mac80211_hwsim

## 📌 Overview
* **Location:** `drivers/net/wireless/mac80211_hwsim.c`
* **Current Status:**  👀 **Confirmed** 🔄**Indirectly Fixed**
* **Notes:** An RCU CPU stall occurs on CPU 0 during a `clone()` syscall. The root cause is a context-recursion deadlock: a timer softirq interrupts the process context and invokes `mac80211_hwsim_beacon`, which then attempts to acquire a spinlock in `mac80211_hwsim_tx_frame_no_nl()` without properly disabling bottom halves (missing `spin_lock_bh`). The kernel maintainer confirmed the bug and noted that a patch for this specific missing BH disable had recently been merged upstream.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] RCU stall in mac80211_hwsim during clone() syscall on 6.18.0 | <u>[lore.kernel.org](https://lore.kernel.org/all/37b368f9.92b3.19b87161d5b.Coremail.wangzhi_xd@stu.xidian.edu.cn/)</u> |
| Re: [BUG] RCU stall in mac80211_hwsim during clone() syscall on 6.18.0 | <u>[lore.kernel.org](https://lore.kernel.org/all/c92ccb99-3766-46d3-8a8a-1e8a6cb79d07@nvidia.com/)</u> |
| Re: [BUG] RCU stall in mac80211_hwsim during clone() syscall on 6.18.0 | <u>[lore.kernel.org](https://lore.kernel.org/all/f265b442-30b0-45db-8149-786f30bcc613@nvidia.com/)</u> |

