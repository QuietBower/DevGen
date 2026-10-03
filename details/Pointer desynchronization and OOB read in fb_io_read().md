# 🐛 Pointer desynchronization and OOB read in fb_io_read()

## 📌 Overview
* **Location:** `drivers/video/fbdev/core/fb_io_fops.c`
* **Current Status:** **CVE-2026-80578**✅ **Patch Accepted** **HIGH 7.3**
* **Notes:** In fb_io_read(), a partial copy_to_user() due to a faulty user buffer leaves the hardware 'src' pointer over-advanced relative to the remaining byte count. This desynchronization causes subsequent loop iterations to perform out-of-bounds hardware I/O reads.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] fbdev: core: Fix pointer desynchronization in fb_io_read() | <u>lore.kernel.org</u> |
| AI-Result                                                    | <u>lore.kernel.org</u> |
| Helge Deller:Re: [PATCH] fbdev: core: Fix pointer desynchronization in fb_io_read() | <u>lore.kernel.org</u> |
| Mingyu Wang: Re: [PATCH] fbdev: core: Fix pointer desynchronization in fb_io_read() | <u>lore.kernel.org</u> |
| fbdev: core: Fix pointer desynchronization in fb_io_read()   | <u>lore.kernel.org</u> |

