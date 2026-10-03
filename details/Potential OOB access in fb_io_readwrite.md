# 🐛 Potential OOB access in fb_io_read/write

## 📌 Overview
* **Location:** `drivers/video/fbdev/core/fb_io_fops.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** Legacy fbdev drivers incorrectly setting info->screen_size larger than the actual mapped framebuffer size (info->fix.smem_len) during mode switches can allow out-of-bounds memory accesses in fb_io_read() and fb_io_write().

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] fbdev: core: Clamp total_size to smem_len in fb_io_read/write | <u>lore.kernel.org</u> |
| AI-Result                                                    | <u>lore.kernel.org</u> |
| sashiko-bot:Re: [PATCH] fbdev: core: Clamp total_size to smem_len in fb_io_read/write | <u>lore.kernel.org</u> |
| [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions | <u>lore.kernel.org</u> |
| sashiko-bot:Re: [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions | <u>lore.kernel.org</u> |
| Helge Deller:Re: [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions | <u>lore.kernel.org</u> |
| fbdev: core: Clamp total_size to smem_len in read/write functions | <u>lore.kernel.org</u> |

