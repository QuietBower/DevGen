# [PATCH 6.18 024/377] drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks()

| Field | Value |
| :--- | :--- |
| From | Greg Kroah-Hartman <gregkh@linuxfoundation.org> |
| To | stable@vger.kernel.org |
| Cc | Greg Kroah-Hartman <gregkh@linuxfoundation.org>, patches@lists.linux.dev, Thomas Zimmermann <tzimmermann@suse.de>, syzbot+fcede535e7eb57cf5b43@syzkaller.appspotmail.com, =?UTF-8?q?Ville=20Syrj=C3=A4l=C3=A4?= <ville.syrjala@linux.intel.com>, Author A <author-a@example.com>, Sasha Levin <sashal@kernel.org> |
| Date | Thu, 28 May 2026 21:44:22 +0200 |
| Message-ID | `20260528194639.086627873@linuxfoundation.org` |
| In-Reply-To | `20260528194638.371537336@linuxfoundation.org` |
| Source | `[ANONYMIZED] local copy (original archive link removed)` |

---

6.18-stable review patch.  If anyone has any objections, please let me know.

------------------

From: Thomas Zimmermann <tzimmermann@suse.de>

[ Upstream commit 79ae8510b5b81b9500370f89c619b50ca9c0990f ]

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
Reviewed-by: Ville Syrjälä <ville.syrjala@linux.intel.com>
Fixes: 74afeb812850 ("drm/vblank: Add vblank timer")
Link: `[ARCHIVE] https://patch.msgid.link/20251209143325.102056-1-tzimmermann@suse.de`
Signed-off-by: Author A <author-a@example.com>
Signed-off-by: Sasha Levin <sashal@kernel.org>
---
 drivers/gpu/drm/drm_atomic_helper.c | 2 +-
 1 file changed, 1 insertion(+), 1 deletion(-)

diff --git a/drivers/gpu/drm/drm_atomic_helper.c b/drivers/gpu/drm/drm_atomic_helper.c
index bbec1c184f652..b06a5cba52958 100644
--- a/drivers/gpu/drm/drm_atomic_helper.c
+++ b/drivers/gpu/drm/drm_atomic_helper.c
@@ -1913,7 +1913,7 @@ drm_atomic_helper_wait_for_vblanks(struct drm_device *dev,
 		ret = wait_event_timeout(dev->vblank[i].queue,
 					 state->crtcs[i].last_vblank_count !=
 						drm_crtc_vblank_count(crtc),
-					 msecs_to_jiffies(100));
+					 msecs_to_jiffies(1000));
 
 		WARN(!ret, "[CRTC:%d:%s] vblank wait timed out\n",
 		     crtc->base.id, crtc->name);
-- 
2.53.0
