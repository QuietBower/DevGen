# [PATCH] fbdev: core: Clamp total_size to smem_len in fb_io_read/write

| Field | Value |
| :--- | :--- |
| From | Author A <author-a@example.com> |
| To | simona@ffwll.ch, deller@gmx.de |
| Cc | linux-fbdev@vger.kernel.org, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Mon, 20 Jul 2026 21:55:34 +0800 |
| Message-ID | `20260720135534.103599-1-author-a@example.com` |
| List-ID | <linux-kernel.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260720135534.103599-1-author-a@example.com/` |

---

Some legacy fbdev drivers may incorrectly set info->screen_size to a
value larger than the actual mapped framebuffer size (info->fix.smem_len)
during mode switches. This could allow out-of-bounds I/O accesses in
fb_io_read() and fb_io_write().

Prevent this by clamping total_size to smem_len when smem_len is non-zero.
Virtual framebuffers (smem_len == 0) are unaffected.

This is a hardening measure; no specific crash is fixed by this patch.

Signed-off-by: Author A <author-a@example.com>
---
 drivers/video/fbdev/core/fb_io_fops.c | 16 ++++++++++++++++
 1 file changed, 16 insertions(+)

diff --git a/drivers/video/fbdev/core/fb_io_fops.c b/drivers/video/fbdev/core/fb_io_fops.c
index 6ab60fcd0050..335f16d2cc23 100644
--- a/drivers/video/fbdev/core/fb_io_fops.c
+++ b/drivers/video/fbdev/core/fb_io_fops.c
@@ -24,6 +24,14 @@ ssize_t fb_io_read(struct fb_info *info, char __user *buf, size_t count, loff_t
 	if (total_size == 0)
 		total_size = info->fix.smem_len;
 
+	/*
+	 * Security Hardening: Defend against buggy legacy drivers that may
+	 * calculate a malformed screen_size. Clamp total_size to the actual
+	 * hardware mapped memory limit (smem_len) to prevent OOB access.
+	 */
+	if (info->fix.smem_len && total_size > info->fix.smem_len)
+		total_size = info->fix.smem_len;
+
 	if (p >= total_size)
 		return 0;
 
@@ -88,6 +96,14 @@ ssize_t fb_io_write(struct fb_info *info, const char __user *buf, size_t count,
 	if (total_size == 0)
 		total_size = info->fix.smem_len;
 
+	/*
+	 * Security Hardening: Defend against buggy legacy drivers that may
+	 * calculate a malformed screen_size. Clamp total_size to the actual
+	 * hardware mapped memory limit (smem_len) to prevent OOB access.
+	 */
+	if (info->fix.smem_len && total_size > info->fix.smem_len)
+		total_size = info->fix.smem_len;
+
 	if (p > total_size)
 		return -EFBIG;
 
-- 
2.34.1
