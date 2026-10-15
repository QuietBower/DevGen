# 🐛 Pointer desynchronization and OOB read in fb_io_read()

## 📌 Overview
* **Location:** `drivers/video/fbdev/core/fb_io_fops.c`
* **Current Status:** **[CVE-2026-80578](https://www.cve.org/CVERecord?id=CVE-2026-80578)**✅ **Patch Accepted** **HIGH 7.3**
* **Notes:** In fb_io_read(), a partial copy_to_user() due to a faulty user buffer leaves the hardware 'src' pointer over-advanced relative to the remaining byte count. This desynchronization causes subsequent loop iterations to perform out-of-bounds hardware I/O reads.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] fbdev: core: Fix pointer desynchronization in fb_io_read() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260721081942.107024-1-25181214217@stu.xidian.edu.cn/)</u> |
| AI-Result                                                    | <u>[lore.kernel.org](https://sashiko.dev/#/patchset/20260721081942.107024-1-25181214217%40stu.xidian.edu.cn)</u> |
| Helge Deller:Re: [PATCH] fbdev: core: Fix pointer desynchronization in fb_io_read() | <u>[lore.kernel.org](https://lore.kernel.org/all/86a0387c-b704-4ea5-8117-02b30c4faeb3@gmx.de/)</u> |
| Mingyu Wang: Re: [PATCH] fbdev: core: Fix pointer desynchronization in fb_io_read() | <u>[lore.kernel.org](https://lore.kernel.org/all/8b900197-f633-4917-b1f4-5c8cd46f025f@stu.xidian.edu.cn/)</u> |
| fbdev: core: Fix pointer desynchronization in fb_io_read()   | <u>[lore.kernel.org](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=81cc73be40c6f028f1ee3f438ace46afe666dbae)</u> |

