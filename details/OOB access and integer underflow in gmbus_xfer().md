# 🐛 OOB access and integer underflow in gmbus_xfer()

## 📌 Overview
* **Location:** `drivers/gpu/drm/gma500/intel_gmbus.c`
* **Current Status:** 🆕 **Bug Reported**  🛠️ **Patch Sent**
* **Notes:** Zero-length GMBUS writes dereference an unchecked buffer and underflow `len` to 65535, leading to out-of-bounds memory accesses.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                | Link                                                         |
| :--------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] drm/gma500: Handle zero-length GMBUS writes | <u>[mail](../email_lore/442_%5BPATCH%5D%20drm_gma500_%20Handle%20zero-length%20GMBUS%20writes.md)</u> |
| Re: [PATCH] drm/gma500: Handle zero-length GMBUS writes | <u>[mail](../email_lore/443_Re_%20%5BPATCH%5D%20drm_gma500_%20Handle%20zero-length%20GMBUS%20writes.md)</u> |
| [PATCH v2] drm/gma500: Handle zero-length GMBUS writes | <u>[mail](../email_lore/442_%5BPATCH%5D%20drm_gma500_%20Handle%20zero-length%20GMBUS%20writes.md)</u> |
| Re: [PATCH v2] drm/gma500: Handle zero-length GMBUS writes | <u>[mail](../email_lore/445_Re_%20%5BPATCH%20v2%5D%20drm_gma500_%20Handle%20zero-length%20GMBUS%20writes.md)</u> |

