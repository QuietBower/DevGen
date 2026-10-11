# 🐛 Potential OOB access in fb_io_read/write

## 📌 Overview
* **Location:** `drivers/video/fbdev/core/fb_io_fops.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** Legacy fbdev drivers incorrectly setting info->screen_size larger than the actual mapped framebuffer size (info->fix.smem_len) during mode switches can allow out-of-bounds memory accesses in fb_io_read() and fb_io_write().

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] fbdev: core: Clamp total_size to smem_len in fb_io_read/write | <u>[mail](../email/374_%5BPATCH%5D%20fbdev_%20core_%20Clamp%20total_size%20to%20smem_len%20in%20fb_io_read_write.txt)</u> |
| AI-Result                                                    | <u>lore.kernel.org</u> |
| sashiko-bot:Re: [PATCH] fbdev: core: Clamp total_size to smem_len in fb_io_read/write | <u>[mail](../email/371_Re_%20%5BPATCH%5D%20fbdev_%20core_%20Clamp%20total_size%20to%20smem_len%20in%20fb_io_read_wr.txt)</u> |
| [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions | <u>[mail](../email/376_%5BPATCH%20v2%5D%20fbdev_%20core_%20Clamp%20total_size%20to%20smem_len%20in%20read_write%20fun.txt)</u> |
| sashiko-bot:Re: [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions | <u>[mail](../email/375_Re_%20%5BPATCH%20v2%5D%20fbdev_%20core_%20Clamp%20total_size%20to%20smem_len%20in%20read_write.txt)</u> |
| Helge Deller:Re: [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions | <u>[mail](../email/401_Re_%20%5BPATCH%20v2%5D%20fbdev_%20core_%20Clamp%20total_size%20to%20smem_len%20in%20read_write.txt)</u> |
| fbdev: core: Clamp total_size to smem_len in read/write functions | <u>[mail](../email/376_%5BPATCH%20v2%5D%20fbdev_%20core_%20Clamp%20total_size%20to%20smem_len%20in%20read_write%20fun.txt)</u> |

