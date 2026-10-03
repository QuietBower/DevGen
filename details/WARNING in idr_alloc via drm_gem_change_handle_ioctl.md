# 🐛 WARNING in idr_alloc via drm_gem_change_handle_ioctl

## 📌 Overview
* **Location:** `drivers/gpu/drm/drm_gem.c`
* **Current Status:** 👀**Patch Accepted**  CVE-2026-23149
* **Notes:** A `WARNING` is triggered in `idr_alloc()` (lib/idr.c) when `drm_gem_change_handle_ioctl()` attempts to allocate or modify a GEM handle. This is likely caused by a lack of proper bounds checking on user-supplied values from the IOCTL, allowing an invalid or negative starting range to be passed directly to the IDR allocator.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] WARNING in idr_alloc during drm_gem_change_handle_ioctl | <u>lore.kernel.org</u> |
| Re: [BUG] WARNING in idr_alloc during drm_gem_change_handle_ioctl | <u>lore.kernel.org</u> |
| 1_drm: Do not allow userspace to trigger kernel warnings in drm_gem_change_handle_ioctl() | <u>lore.kernel.org</u> |
| 2_drm: Do not allow userspace to trigger kernel warnings in drm_gem_change_handle_ioctl() | <u>lore.kernel.org</u> |

