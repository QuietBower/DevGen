# 🐛 Pointer desynchronization and OOB read in fb_io_read()

## 📌 Overview
* **Location:** `drivers/video/fbdev/core/fb_io_fops.c`
* **Current Status:** **CVE-2026-80578**✅ **Patch Accepted** **HIGH 7.3**
* **Notes:** In fb_io_read(), a partial copy_to_user() due to a faulty user buffer leaves the hardware 'src' pointer over-advanced relative to the remaining byte count. This desynchronization causes subsequent loop iterations to perform out-of-bounds hardware I/O reads.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] fbdev: core: Fix pointer desynchronization in fb_io_read() | <u>[mail](../email_lore/378_%5BPATCH%5D%20fbdev_%20core_%20Fix%20pointer%20desynchronization%20in%20fb_io_read%28%29.md)</u> |
| AI-Result                                                    | <u>lore.kernel.org</u> |
| Helge Deller:Re: [PATCH] fbdev: core: Fix pointer desynchronization in fb_io_read() | <u>[mail](../email_lore/400_Re_%20%5BPATCH%5D%20fbdev_%20core_%20Fix%20pointer%20desynchronization%20in%20fb_io_read%28%29.md)</u> |
| Author A: Re: [PATCH] fbdev: core: Fix pointer desynchronization in fb_io_read() | <u>[mail](../email_lore/400_Re_%20%5BPATCH%5D%20fbdev_%20core_%20Fix%20pointer%20desynchronization%20in%20fb_io_read%28%29.md)</u> |
| fbdev: core: Fix pointer desynchronization in fb_io_read() | <u>[mail](../email_lore/378_%5BPATCH%5D%20fbdev_%20core_%20Fix%20pointer%20desynchronization%20in%20fb_io_read%28%29.md)</u> |

