# 🐛 Potential OOB access in fb_io_read/write

## 📌 Overview
* **Location:** `drivers/video/fbdev/core/fb_io_fops.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** Legacy fbdev drivers incorrectly setting info->screen_size larger than the actual mapped framebuffer size (info->fix.smem_len) during mode switches can allow out-of-bounds memory accesses in fb_io_read() and fb_io_write().

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] fbdev: core: Clamp total_size to smem_len in fb_io_read/write | <u>[lore.kernel.org](https://lore.kernel.org/all/20260720135534.103599-1-25181214217@stu.xidian.edu.cn/)</u> |
| AI-Result                                                    | <u>[lore.kernel.org](https://sashiko.dev/#/patchset/20260720135534.103599-1-25181214217%40stu.xidian.edu.cn)</u> |
| sashiko-bot:Re: [PATCH] fbdev: core: Clamp total_size to smem_len in fb_io_read/write | <u>[lore.kernel.org](https://lore.kernel.org/all/20260720141559.0912D1F00A3A@smtp.kernel.org/)</u> |
| [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions | <u>[lore.kernel.org](https://lore.kernel.org/all/20260721071522.106306-1-25181214217@stu.xidian.edu.cn/)</u> |
| sashiko-bot:Re: [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions | <u>[lore.kernel.org](https://lore.kernel.org/all/20260721073109.81A121F000E9@smtp.kernel.org/)</u> |
| Helge Deller:Re: [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions | <u>[lore.kernel.org](https://lore.kernel.org/all/bb8a8d13-07fb-4ee9-aed2-172c86cf3ac0@gmx.de/)</u> |
| fbdev: core: Clamp total_size to smem_len in read/write functions | <u>[lore.kernel.org](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=27b8e3c27d858156c6a6b94919d5964fabd40f7a)</u> |

