# [PATCH] fbdev: core: Fix pointer desynchronization in fb_io_read()

| Field | Value |
| :--- | :--- |
| From | Author A <author-a@example.com> |
| To | simona@ffwll.ch, deller@gmx.de |
| Cc | tzimmermann@suse.de, sam@ravnborg.org, javierm@redhat.com, linux-fbdev@vger.kernel.org, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com>, stable@vger.kernel.org |
| Date | Tue, 21 Jul 2026 16:19:42 +0800 |
| Message-ID | `20260721081942.107024-1-author-a@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260721081942.107024-1-author-a@example.com/` |

---

In fb_io_read(), if copy_to_user() performs a partial copy (e.g., due to
a faulty user buffer), the loop adjusts the chunk size 'c' and updates
the remaining 'count'. However, the hardware 'src' pointer has already
been eagerly advanced by the original chunk size.

If the loop is allowed to continue, the read will resume from an
incorrect, over-advanced offset. Since the remaining 'count' was only
decremented by the successful bytes, this desynchronization causes the
next iterations to execute more hardware reads than originally bounded,
eventually leading to out-of-bounds I/O reads.

Fix this by breaking out of the loop immediately upon a partial
copy_to_user(). A partial copy indicates a faulty user buffer, making
subsequent read attempts futile. Breaking out ensures we return the
number of successfully read bytes without risking out-of-bounds hardware
accesses in subsequent mismatched iterations.

Fixes: 6121cd9ef911 ("fbdev: Move I/O read and write code into helper functions")
Cc: stable@vger.kernel.org
Signed-off-by: Author A <author-a@example.com>
---
 drivers/video/fbdev/core/fb_io_fops.c | 8 ++++++++
 1 file changed, 8 insertions(+)

diff --git a/drivers/video/fbdev/core/fb_io_fops.c b/drivers/video/fbdev/core/fb_io_fops.c
index 6ab60fcd0050..0798e88799eb 100644
--- a/drivers/video/fbdev/core/fb_io_fops.c
+++ b/drivers/video/fbdev/core/fb_io_fops.c
@@ -61,6 +61,14 @@ ssize_t fb_io_read(struct fb_info *info, char __user *buf, size_t count, loff_t
 		buf += c;
 		cnt += c;
 		count -= c;
+
+		/*
+		 * If there was a partial copy, the user buffer is faulty.
+		 * Break out to avoid over-advancing the src pointer and
+		 * reading out of bounds in the next iteration.
+		 */
+		if (trailing)
+			break;
 	}
 
 	kfree(buffer);
-- 
2.34.1
