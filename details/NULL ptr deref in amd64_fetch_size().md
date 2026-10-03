# 🐛 NULL ptr deref in amd64_fetch_size()

## 📌 Overview
* **Location:** `drivers/char/agp/amd64-agp.c`
* **Current Status:**  ✅ **Patch Accepted** [CVE-2026-53325](https://www.cve.org/CVERecord?id=CVE-2026-53325)
* **Notes:** A missing NULL check for `node_to_amd_nb(0)` causes a kernel panic in mixed or emulated environments.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] agp: amd64: fix null-ptr-deref in amd64_fetch_size   | <u>[lore.kernel.org](https://lore.kernel.org/all/20260429022548.37070-1-25181214217@stu.xidian.edu.cn/)</u> |
| [PATCH] char: agp: amd64 - fix null-ptr-deref in amd64_fetch_size and related functions | <u>[lore.kernel.org](https://lore.kernel.org/all/20260504065441.99033-1-w15303746062@163.com/)</u> |
| Re: [PATCH] char: agp: amd64 - fix null-ptr-deref in amd64_fetch_size and related functions | <u>[lore.kernel.org](https://lore.kernel.org/all/20260504073407.A8A35C2BCB8@smtp.kernel.org/)</u> |
| [PATCH v2] char: agp: amd64 - fix broken error propagation in agp_amd64_probe() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260504074823.99377-1-w15303746062@163.com/)</u> |
| Re:[PATCH v2] char: agp: amd64 - fix broken error propagation in agp_amd64_probe() | <u>[lore.kernel.org](https://lore.kernel.org/all/71742084.a1c9.19e92c4425c.Coremail.w15303746062@163.com/)</u> |
| Reviewed-Re: [PATCH v2] char: agp: amd64 - fix broken error propagation in agp_amd64_probe() | <u>[lore.kernel.org](../email/Review%20Re%20%5BPATCH%20v2%5D%20char%20agp%20amd64%20-%20fix%20broken%20error%20propagation%20in%20agp_amd64_probe%28%29.txt)</u> |
| Applied to drm-misc-next-fixes for v7.2                      | <u>[lore.kernel.org](https://lore.kernel.org/all/aiZ5PomNR3ZoRzbM@wunner.de/)</u> |
| agp/amd64: Fix broken error propagation in agp_amd64_probe() | <u>[lore.kernel.org](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=b08472db93b1ccff84a7adec5779d47f0e9d3a30)</u> |

