# [PATCH] drm/gma500: Handle zero-length GMBUS writes

| Field | Value |
| :--- | :--- |
| From | Author G <author-g@example.com> |
| To | patrik.r.jakobsson@gmail.com, maarten.lankhorst@linux.intel.com, mripard@kernel.org, tzimmermann@suse.de, airlied@gmail.com, simona@ffwll.ch |
| Cc | dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, Author G <author-g@example.com>, stable@vger.kernel.org |
| Date | Tue, 21 Jul 2026 21:32:00 +0800 |
| Message-ID | `20260721133200.844629-1-author-g@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260721133200.844629-1-author-g@example.com/` |

---

gmbus_xfer() uses a do-while loop to preload the first bytes of a
write message into GMBUS3. The loop accesses the message buffer
before checking whether any bytes remain.

For a zero-length write, the loop therefore dereferences the buffer
once. Decrementing the unsigned length from zero also wraps it to
65535, which may lead to further out-of-bounds accesses.

Check the remaining length before accessing the buffer. This keeps
the existing GMBUS transaction flow unchanged and preserves the
behavior of all non-zero-length writes.

Fixes: 5c0c1d50d7ba ("gma500: Add support for Intel GMBUS")
Cc: stable@vger.kernel.org
Signed-off-by: Author G <author-g@example.com>
---
 drivers/gpu/drm/gma500/intel_gmbus.c | 7 ++++---
 1 file changed, 4 insertions(+), 3 deletions(-)

diff --git a/drivers/gpu/drm/gma500/intel_gmbus.c b/drivers/gpu/drm/gma500/intel_gmbus.c
index 5933ae04a6aa..bf060a62b3b7 100644
--- a/drivers/gpu/drm/gma500/intel_gmbus.c
+++ b/drivers/gpu/drm/gma500/intel_gmbus.c
@@ -294,9 +294,10 @@ gmbus_xfer(struct i2c_adapter *adapter,
 			u32 val, loop;
 
 			val = loop = 0;
-			do {
-				val |= *buf++ << (8 * loop);
-			} while (--len && ++loop < 4);
+			while (len & loop < 4) {
+				val |= *buf++ << (8 * loop++);
+				len -= 1;
+			}
 
 			GMBUS_REG_WRITE(GMBUS3 + reg_offset, val);
 			GMBUS_REG_WRITE(GMBUS1 + reg_offset,
-- 
2.34.1
