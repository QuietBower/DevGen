# 🐛 NULL ptr deref in amd64_fetch_size()

## 📌 Overview
* **Location:** `drivers/char/agp/amd64-agp.c`
* **Current Status:**  ✅ **Patch Accepted** CVE-2026-53325
* **Notes:** A missing NULL check for `node_to_amd_nb(0)` causes a kernel panic in mixed or emulated environments.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] agp: amd64: fix null-ptr-deref in amd64_fetch_size   | <u>lore.kernel.org</u> |
| [PATCH] char: agp: amd64 - fix null-ptr-deref in amd64_fetch_size and related functions | <u>lore.kernel.org</u> |
| Re: [PATCH] char: agp: amd64 - fix null-ptr-deref in amd64_fetch_size and related functions | <u>lore.kernel.org</u> |
| [PATCH v2] char: agp: amd64 - fix broken error propagation in agp_amd64_probe() | <u>lore.kernel.org</u> |
| Re:[PATCH v2] char: agp: amd64 - fix broken error propagation in agp_amd64_probe() | <u>lore.kernel.org</u> |
| Reviewed-Re: [PATCH v2] char: agp: amd64 - fix broken error propagation in agp_amd64_probe() | <u>lore.kernel.org</u> |
| Applied to drm-misc-next-fixes for v7.2                      | <u>lore.kernel.org</u> |
| agp/amd64: Fix broken error propagation in agp_amd64_probe() | <u>lore.kernel.org</u> |

