# 🐛 WARN_ON in set_cpu_sibling_map via numa=fake

## 📌 Overview
* **Location:** `arch/x86/kernel/smpboot.c`
* **Current Status:** 👀 **Confirmed**
* **Notes:** The `numa=fake=N` parameter falsely triggers a topology consistency `WARN_ON_ONCE` because it artificially divides a single physical package into multiple software NUMA nodes.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] x86/smpboot: WARN_ON in set_cpu_sibling_map triggered by numa=fake=2 | <u>[lore.kernel.org](https://lore.kernel.org/all/20260429034125.37368-1-w15303746062@163.com/)</u> |
| Re: [BUG] x86/smpboot: WARN_ON in set_cpu_sibling_map triggered by numa=fake=2 | <u>[lore.kernel.org](https://lore.kernel.org/all/20260504074830.GM3126523@noisy.programming.kicks-ass.net/)</u> |
| Re:Re: [BUG] x86/smpboot: WARN_ON in set_cpu_sibling_map triggered by numa=fake=2 | <u>[lore.kernel.org](https://lore.kernel.org/all/709c0e27.25df.19df347fe05.Coremail.w15303746062@163.com/)</u> |

