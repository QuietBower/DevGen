# 🐛 NULL ptr deref in amd64_fetch_size()

## 📌 Overview
* **Location:** `drivers/char/agp/amd64-agp.c`
* **Current Status:**  ✅ **Patch Accepted** CVE-2026-53325
* **Notes:** A missing NULL check for `node_to_amd_nb(0)` causes a kernel panic in mixed or emulated environments.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] agp: amd64: fix null-ptr-deref in amd64_fetch_size | <u>[mail](../email/089_%5BPATCH%5D%20agp_%20amd64_%20fix%20null-ptr-deref%20in%20amd64_fetch_size.txt)</u> |
| [PATCH] char: agp: amd64 - fix null-ptr-deref in amd64_fetch_size and related functions | <u>[mail](../email/095_%5BPATCH%5D%20char_%20agp_%20amd64%20-%20fix%20null-ptr-deref%20in%20amd64_fetch_size%20and%20.txt)</u> |
| Re: [PATCH] char: agp: amd64 - fix null-ptr-deref in amd64_fetch_size and related functions | <u>[mail](../email/092_Re_%20%5BPATCH%5D%20char_%20agp_%20amd64%20-%20fix%20null-ptr-deref%20in%20amd64_fetch_size%20.txt)</u> |
| [PATCH v2] char: agp: amd64 - fix broken error propagation in agp_amd64_probe() | <u>[mail](../email/096_%5BPATCH%20v2%5D%20char_%20agp_%20amd64%20-%20fix%20broken%20error%20propagation%20in%20agp_amd6.txt)</u> |
| Re:[PATCH v2] char: agp: amd64 - fix broken error propagation in agp_amd64_probe() | <u>[mail](../email/211_Re_%5BPATCH%20v2%5D%20char_%20agp_%20amd64%20-%20fix%20broken%20error%20propagation%20in%20agp_a.txt)</u> |
| Reviewed-Re: [PATCH v2] char: agp: amd64 - fix broken error propagation in agp_amd64_probe() | <u>[mail](../email/096_%5BPATCH%20v2%5D%20char_%20agp_%20amd64%20-%20fix%20broken%20error%20propagation%20in%20agp_amd6.txt)</u> |
| Applied to drm-misc-next-fixes for v7.2 | <u>[mail](../email/224_Re_%20%5BPATCH%20v2%5D%20char_%20agp_%20amd64%20-%20fix%20broken%20error%20propagation%20in%20agp_.txt)</u> |
| agp/amd64: Fix broken error propagation in agp_amd64_probe() | <u>[mail](../email/096_%5BPATCH%20v2%5D%20char_%20agp_%20amd64%20-%20fix%20broken%20error%20propagation%20in%20agp_amd6.txt)</u> |

