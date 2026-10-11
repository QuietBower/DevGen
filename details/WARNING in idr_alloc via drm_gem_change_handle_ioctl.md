# 🐛 WARNING in idr_alloc via drm_gem_change_handle_ioctl

## 📌 Overview
* **Location:** `drivers/gpu/drm/drm_gem.c`
* **Current Status:** 👀**Patch Accepted**  CVE-2026-23149
* **Notes:** A `WARNING` is triggered in `idr_alloc()` (lib/idr.c) when `drm_gem_change_handle_ioctl()` attempts to allocate or modify a GEM handle. This is likely caused by a lack of proper bounds checking on user-supplied values from the IOCTL, allowing an invalid or negative starting range to be passed directly to the IDR allocator.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] WARNING in idr_alloc during drm_gem_change_handle_ioctl | <u>[mail](../email/031_%5BBUG%5D%20WARNING%20in%20idr_alloc%20during%20drm_gem_change_handle_ioctl.txt)</u> |
| Re: [BUG] WARNING in idr_alloc during drm_gem_change_handle_ioctl | <u>[mail](../email/036_Re_%20%5BBUG%5D%20WARNING%20in%20idr_alloc%20during%20drm_gem_change_handle_ioctl.txt)</u> |
| 1_drm: Do not allow userspace to trigger kernel warnings in drm_gem_change_handle_ioctl() | <u>[mail](../email/487_drm_%20Do%20not%20allow%20userspace%20to%20trigger%20kernel%20warnings%20in%20drm_gem_change_handle_ioctl%28%29.txt)</u> |
| 2_drm: Do not allow userspace to trigger kernel warnings in drm_gem_change_handle_ioctl() | <u>[mail](../email/488_drm_%20Do%20not%20allow%20userspace%20to%20trigger%20kernel%20warnings%20in%20drm_gem_change_handle_ioctl%28%29%20%5Bae8831ee0fb2%5D.txt)</u> |

