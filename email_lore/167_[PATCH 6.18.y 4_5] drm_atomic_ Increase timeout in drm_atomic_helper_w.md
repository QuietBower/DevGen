# [PATCH 6.18.y 4/5] drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks()

| Field | Value |
| :--- | :--- |
| From | author-a-alt@example.com |
| To | stable@vger.kernel.org, gregkh@linuxfoundation.org |
| Cc | tzimmermann@suse.de, maarten.lankhorst@linux.intel.com, mripard@kernel.org, louis.chauvet@bootlin.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, syzbot+fcede535e7eb57cf5b43@syzkaller.appspotmail.com, Ville Syrjälä <ville.syrjala@linux.intel.com> |
| Date | Mon, 25 May 2026 21:16:09 +0800 |
| Message-ID | `20260525131610.608273-5-author-a-alt@example.com` |
| In-Reply-To | `20260525131610.608273-1-author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260525131610.608273-5-author-a-alt@example.com/` |

---

From: Thomas Zimmermann <tzimmermann@suse.de>

Increase the timeout for vblank events from 100 ms to 1000 ms. This
is the same fix as in commit f050da08a4ed ("drm/vblank: Increase
timeout in drm_wait_one_vblank()") for another vblank timeout.

After merging generic DRM vblank timers [1] and converting several
DRM drivers for virtual hardware, these drivers synchronize their
vblank events to the display refresh rate. This can trigger timeouts
within the DRM framework.

Signed-off-by: Thomas Zimmermann <tzimmermann@suse.de>
Link: `[ARCHIVE] https://lore.kernel.org/dri-devel/20250904145806.430568-1-tzimmermann@suse.de/` # [1]
Reported-by: syzbot+fcede535e7eb57cf5b43@syzkaller.appspotmail.com
Closes: `[ARCHIVE] https://lore.kernel.org/dri-devel/69381d6c.050a0220.4004e.0017.GAE@google.com/`
Reviewed-by: Ville Syrj�l� <ville.syrjala@linux.intel.com>
Fixes: 74afeb812850 ("drm/vblank: Add vblank timer")
Link: `[ARCHIVE] https://patch.msgid.link/20251209143325.102056-1-tzimmermann@suse.de`
(cherry picked from commit 79ae8510b5b81b9500370f89c619b50ca9c0990f)
---
 drivers/gpu/drm/drm_atomic_helper.c | 2 +-
 1 file changed, 1 insertion(+), 1 deletion(-)

diff --git a/drivers/gpu/drm/drm_atomic_helper.c b/drivers/gpu/drm/drm_atomic_helper.c
index d5ebe6ea0acb..f9d328dcaed8 100644
--- a/drivers/gpu/drm/drm_atomic_helper.c
+++ b/drivers/gpu/drm/drm_atomic_helper.c
@@ -1837,7 +1837,7 @@ drm_atomic_helper_wait_for_vblanks(struct drm_device *dev,
 		ret = wait_event_timeout(dev->vblank[i].queue,
 					 state->crtcs[i].last_vblank_count !=
 						drm_crtc_vblank_count(crtc),
-					 msecs_to_jiffies(100));
+					 msecs_to_jiffies(1000));
 
 		WARN(!ret, "[CRTC:%d:%s] vblank wait timed out\n",
 		     crtc->base.id, crtc->name);
-- 
2.34.1
