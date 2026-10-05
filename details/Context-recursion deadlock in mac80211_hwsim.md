# 🐛 Context-recursion deadlock in mac80211_hwsim

## 📌 Overview
* **Location:** `drivers/net/wireless/mac80211_hwsim.c`
* **Current Status:**  👀 **Confirmed** 🔄**Indirectly Fixed**
* **Notes:** An RCU CPU stall occurs on CPU 0 during a `clone()` syscall. The root cause is a context-recursion deadlock: a timer softirq interrupts the process context and invokes `mac80211_hwsim_beacon`, which then attempts to acquire a spinlock in `mac80211_hwsim_tx_frame_no_nl()` without properly disabling bottom halves (missing `spin_lock_bh`). The kernel maintainer confirmed the bug and noted that a patch for this specific missing BH disable had recently been merged upstream.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] RCU stall in mac80211_hwsim during clone() syscall on 6.18.0 | <u>[mail](../email_lore/001_Re_%20%5BBUG%5D%20RCU%20stall%20in%20mac80211_hwsim%20during%20clone%28%29%20syscall%20on%206.18.0.md)</u> |
| Re: [BUG] RCU stall in mac80211_hwsim during clone() syscall on 6.18.0 | <u>[mail](../email_lore/001_Re_%20%5BBUG%5D%20RCU%20stall%20in%20mac80211_hwsim%20during%20clone%28%29%20syscall%20on%206.18.0.md)</u> |
| Re: [BUG] RCU stall in mac80211_hwsim during clone() syscall on 6.18.0 | <u>[mail](../email_lore/002_Re_%20%5BBUG%5D%20RCU%20stall%20in%20mac80211_hwsim%20during%20clone%28%29%20syscall%20on%206.18.0.md)</u> |

