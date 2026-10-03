# 🐛 OOB access and integer underflow in gmbus_xfer()

## 📌 Overview
* **Location:** `drivers/gpu/drm/gma500/intel_gmbus.c`
* **Current Status:** 🆕 **Bug Reported**  🛠️ **Patch Sent**
* **Notes:** Zero-length GMBUS writes dereference an unchecked buffer and underflow `len` to 65535, leading to out-of-bounds memory accesses.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                | Link                                                         |
| :--------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] drm/gma500: Handle zero-length GMBUS writes        | <u>[lore.kernel.org](https://lore.kernel.org/all/20260721133200.844629-1-x14786623579@163.com/)</u> |
| Re: [PATCH] drm/gma500: Handle zero-length GMBUS writes    | <u>[lore.kernel.org](https://lore.kernel.org/all/20260721134050.89BD51F00A3D@smtp.kernel.org/)</u> |
| [PATCH v2] drm/gma500: Handle zero-length GMBUS writes     | <u>[lore.kernel.org](https://lore.kernel.org/all/20260721134910.844678-1-x14786623579@163.com/)</u> |
| Re: [PATCH v2] drm/gma500: Handle zero-length GMBUS writes | <u>[lore.kernel.org](https://lore.kernel.org/all/20260721135726.CFD0C1F00A3D@smtp.kernel.org/)</u> |

