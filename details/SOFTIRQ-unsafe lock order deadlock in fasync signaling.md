# 🐛 SOFTIRQ-unsafe lock order deadlock in fasync signaling

## 📌 Overview
* **Location:** `fs/fcntl.c`
* **Current Status:** CVE-2026-52946 ✅ **Patch Accepted**  **HIGH 7.5**
* **Notes:** A SOFTIRQ-safe to SOFTIRQ-unsafe lock order deadlock in `send_sigio()` and `send_sigurg()`. When FASYNC is configured for a process group, taking `read_lock(&tasklist_lock)` in softirq context (e.g., during TCP URG packet reception) can deadlock against process-context writers due to rwlock fairness. Fixed by replacing the `tasklist_lock` with `rcu_read_lock()`, which also mitigates a potential remote DoS vector.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] fs/fcntl: fix SOFTIRQ-unsafe lock order in send_sigio() | <u>lore.kernel.org</u> |
| Re: [PATCH] fs/fcntl: fix SOFTIRQ-unsafe lock order in send_sigio() | <u>lore.kernel.org</u> |
| [PATCH v2] fs/fcntl: fix SOFTIRQ-unsafe lock order in fasync signaling | <u>lore.kernel.org</u> |
| Applied to the vfs-7.2.misc branch of the vfs/vfs.git tree   | <u>lore.kernel.org</u> |
| Please cherry-pick commit 00633c468382 to stable             | <u>lore.kernel.org</u> |
| Patch "fs/fcntl: fix SOFTIRQ-unsafe lock order in fasync signaling" has been added to the 5.10-stable tree | <u>lore.kernel.org</u> |
| Patch "fs/fcntl: fix SOFTIRQ-unsafe lock order in fasync signaling" has been added to the 7.1-stable tree | <u>lore.kernel.org</u> |

