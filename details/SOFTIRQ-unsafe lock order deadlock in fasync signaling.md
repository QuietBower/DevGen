# 🐛 SOFTIRQ-unsafe lock order deadlock in fasync signaling

## 📌 Overview
* **Location:** `fs/fcntl.c`
* **Current Status:** CVE-2026-52946 ✅ **Patch Accepted**  **HIGH 7.5**
* **Notes:** A SOFTIRQ-safe to SOFTIRQ-unsafe lock order deadlock in `send_sigio()` and `send_sigurg()`. When FASYNC is configured for a process group, taking `read_lock(&tasklist_lock)` in softirq context (e.g., during TCP URG packet reception) can deadlock against process-context writers due to rwlock fairness. Fixed by replacing the `tasklist_lock` with `rcu_read_lock()`, which also mitigates a potential remote DoS vector.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] fs/fcntl: fix SOFTIRQ-unsafe lock order in send_sigio() | <u>[mail](../email_lore/158_Re_%20%5BPATCH%5D%20fs_fcntl_%20fix%20SOFTIRQ-unsafe%20lock%20order%20in%20send_sigio%28%29.md)</u> |
| Re: [PATCH] fs/fcntl: fix SOFTIRQ-unsafe lock order in send_sigio() | <u>[mail](../email_lore/158_Re_%20%5BPATCH%5D%20fs_fcntl_%20fix%20SOFTIRQ-unsafe%20lock%20order%20in%20send_sigio%28%29.md)</u> |
| [PATCH v2] fs/fcntl: fix SOFTIRQ-unsafe lock order in fasync signaling | <u>[mail](../email_lore/161_%5BPATCH%20v2%5D%20fs_fcntl_%20fix%20SOFTIRQ-unsafe%20lock%20order%20in%20fasync%20signaling.md)</u> |
| Applied to the vfs-7.2.misc branch of the vfs/vfs.git tree | <u>[mail](../email_lore/529_Re_%20%5BPATCH%20v2%5D%20fs_fcntl_%20fix%20SOFTIRQ-unsafe%20lock%20order%20in%20fasync%20signa.md)</u> |
| Please cherry-pick commit 00633c468382 to stable | <u>[mail](../email_lore/230_Please%20cherry-pick%20commit%2000633c468382%20to%20stable.md)</u> |
| Patch "fs/fcntl: fix SOFTIRQ-unsafe lock order in fasync signaling" has been added to the 5.10-stable tree | <u>[mail](../email_lore/521_Patch%20_fs_fcntl_%20fix%20SOFTIRQ-unsafe%20lock%20order%20in%20fasync%20signaling_%20ha.md)</u> |
| Patch "fs/fcntl: fix SOFTIRQ-unsafe lock order in fasync signaling" has been added to the 7.1-stable tree | <u>[mail](../email_lore/522_Patch%20_fs_fcntl_%20fix%20SOFTIRQ-unsafe%20lock%20order%20in%20fasync%20signaling_%20ha.md)</u> |

