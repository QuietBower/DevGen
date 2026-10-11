# 🐛 WARN_ON in set_cpu_sibling_map via numa=fake

## 📌 Overview
* **Location:** `arch/x86/kernel/smpboot.c`
* **Current Status:** 👀 **Confirmed**
* **Notes:** The `numa=fake=N` parameter falsely triggers a topology consistency `WARN_ON_ONCE` because it artificially divides a single physical package into multiple software NUMA nodes.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] x86/smpboot: WARN_ON in set_cpu_sibling_map triggered by numa=fake=2 | <u>[mail](../email/091_%5BBUG%5D%20x86_smpboot_%20WARN_ON%20in%20set_cpu_sibling_map%20triggered%20by%20numa%3Dfa.txt)</u> |
| Re: [BUG] x86/smpboot: WARN_ON in set_cpu_sibling_map triggered by numa=fake=2 | <u>[mail](../email/093_Re_%20%5BBUG%5D%20x86_smpboot_%20WARN_ON%20in%20set_cpu_sibling_map%20triggered%20by%20num.txt)</u> |
| Re:Re: [BUG] x86/smpboot: WARN_ON in set_cpu_sibling_map triggered by numa=fake=2 | <u>[mail](../email/097_Re_Re_%20%5BBUG%5D%20x86_smpboot_%20WARN_ON%20in%20set_cpu_sibling_map%20triggered%20by%20.txt)</u> |

