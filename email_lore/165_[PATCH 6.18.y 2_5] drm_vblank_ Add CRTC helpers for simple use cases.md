# [PATCH 6.18.y 2/5] drm/vblank: Add CRTC helpers for simple use cases

| Field | Value |
| :--- | :--- |
| From | author-a-alt@example.com |
| To | stable@vger.kernel.org, gregkh@linuxfoundation.org |
| Cc | tzimmermann@suse.de, maarten.lankhorst@linux.intel.com, mripard@kernel.org, louis.chauvet@bootlin.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, Javier Martinez Canillas <javierm@redhat.com>, Michael Kelley <mhklinux@outlook.com> |
| Date | Mon, 25 May 2026 21:16:07 +0800 |
| Message-ID | `20260525131610.608273-3-author-a-alt@example.com` |
| In-Reply-To | `20260525131610.608273-1-author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260525131610.608273-3-author-a-alt@example.com/` |

---

From: Thomas Zimmermann <tzimmermann@suse.de>

Implement atomic_flush, atomic_enable and atomic_disable of struct
drm_crtc_helper_funcs for vblank handling. Driver with no further
requirements can use these functions instead of adding their own.
Also simplifies the use of vblank timers.

The code has been adopted from vkms, which added the funtionality
in commit 3a0709928b17 ("drm/vkms: Add vblank events simulated by
hrtimers").

v3:
- mention vkms (Javier)
v2:
- fix docs

Signed-off-by: Thomas Zimmermann <tzimmermann@suse.de>
Reviewed-by: Javier Martinez Canillas <javierm@redhat.com>
Tested-by: Michael Kelley <mhklinux@outlook.com>
Link: `[ARCHIVE] https://lore.kernel.org/r/20250916083816.30275-3-tzimmermann@suse.de`
(cherry picked from commit d54dbb5963bdbdf8559903fe2b2343e871adcb30)
---
 drivers/gpu/drm/drm_vblank_helper.c | 80 +++++++++++++++++++++++++++++
 include/drm/drm_vblank_helper.h     | 23 +++++++++
 2 files changed, 103 insertions(+)

diff --git a/drivers/gpu/drm/drm_vblank_helper.c b/drivers/gpu/drm/drm_vblank_helper.c
index f94d1e706191..a04a6ba1b0ca 100644
--- a/drivers/gpu/drm/drm_vblank_helper.c
+++ b/drivers/gpu/drm/drm_vblank_helper.c
@@ -1,5 +1,6 @@
 // SPDX-License-Identifier: MIT
 
+#include <drm/drm_atomic.h>
 #include <drm/drm_crtc.h>
 #include <drm/drm_managed.h>
 #include <drm/drm_modeset_helper_vtables.h>
@@ -17,6 +18,12 @@
  * Drivers enable support for vblank timers by setting the vblank callbacks
  * in struct &drm_crtc_funcs to the helpers provided by this library. The
  * initializer macro DRM_CRTC_VBLANK_TIMER_FUNCS does this conveniently.
+ * The driver further has to send the VBLANK event from its atomic_flush
+ * callback and control vblank from the CRTC's atomic_enable and atomic_disable
+ * callbacks. The callbacks are located in struct &drm_crtc_helper_funcs.
+ * The vblank helper library provides implementations of these callbacks
+ * for drivers without further requirements. The initializer macro
+ * DRM_CRTC_HELPER_VBLANK_FUNCS sets them coveniently.
  *
  * Once the driver enables vblank support with drm_vblank_init(), each
  * CRTC's vblank timer fires according to the programmed display mode. By
@@ -25,6 +32,79 @@
  * struct &drm_crtc_helper_funcs.handle_vblank_timeout.
  */
 
+/*
+ * VBLANK helpers
+ */
+
+/**
+ * drm_crtc_vblank_atomic_flush -
+ *	Implements struct &drm_crtc_helper_funcs.atomic_flush
+ * @crtc: The CRTC
+ * @state: The atomic state to apply
+ *
+ * The helper drm_crtc_vblank_atomic_flush() implements atomic_flush of
+ * struct drm_crtc_helper_funcs for CRTCs that only need to send out a
+ * VBLANK event.
+ *
+ * See also struct &drm_crtc_helper_funcs.atomic_flush.
+ */
+void drm_crtc_vblank_atomic_flush(struct drm_crtc *crtc,
+				  struct drm_atomic_state *state)
+{
+	struct drm_device *dev = crtc->dev;
+	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
+	struct drm_pending_vblank_event *event;
+
+	spin_lock_irq(&dev->event_lock);
+
+	event = crtc_state->event;
+	crtc_state->event = NULL;
+
+	if (event) {
+		if (drm_crtc_vblank_get(crtc) == 0)
+			drm_crtc_arm_vblank_event(crtc, event);
+		else
+			drm_crtc_send_vblank_event(crtc, event);
+	}
+
+	spin_unlock_irq(&dev->event_lock);
+}
+EXPORT_SYMBOL(drm_crtc_vblank_atomic_flush);
+
+/**
+ * drm_crtc_vblank_atomic_enable - Implements struct &drm_crtc_helper_funcs.atomic_enable
+ * @crtc: The CRTC
+ * @state: The atomic state
+ *
+ * The helper drm_crtc_vblank_atomic_enable() implements atomic_enable
+ * of struct drm_crtc_helper_funcs for CRTCs the only need to enable VBLANKs.
+ *
+ * See also struct &drm_crtc_helper_funcs.atomic_enable.
+ */
+void drm_crtc_vblank_atomic_enable(struct drm_crtc *crtc,
+				   struct drm_atomic_state *state)
+{
+	drm_crtc_vblank_on(crtc);
+}
+EXPORT_SYMBOL(drm_crtc_vblank_atomic_enable);
+
+/**
+ * drm_crtc_vblank_atomic_disable - Implements struct &drm_crtc_helper_funcs.atomic_disable
+ * @crtc: The CRTC
+ * @state: The atomic state
+ *
+ * The helper drm_crtc_vblank_atomic_disable() implements atomic_disable
+ * of struct drm_crtc_helper_funcs for CRTCs the only need to disable VBLANKs.
+ *
+ * See also struct &drm_crtc_funcs.atomic_disable.
+ */
+void drm_crtc_vblank_atomic_disable(struct drm_crtc *crtc,
+				    struct drm_atomic_state *state)
+{
+	drm_crtc_vblank_off(crtc);
+}
+EXPORT_SYMBOL(drm_crtc_vblank_atomic_disable);
+
 /*
  * VBLANK timer
  */
diff --git a/include/drm/drm_vblank_helper.h b/include/drm/drm_vblank_helper.h
index 74a971d0cfba..fcd8a9b35846 100644
--- a/include/drm/drm_vblank_helper.h
+++ b/include/drm/drm_vblank_helper.h
@@ -6,8 +6,31 @@
 #include <linux/hrtimer_types.h>
 #include <linux/types.h>
 
+struct drm_atomic_state;
 struct drm_crtc;
 
+/*
+ * VBLANK helpers
+ */
+
+void drm_crtc_vblank_atomic_flush(struct drm_crtc *crtc,
+				  struct drm_atomic_state *state);
+void drm_crtc_vblank_atomic_enable(struct drm_crtc *crtc,
+				   struct drm_atomic_state *state);
+void drm_crtc_vblank_atomic_disable(struct drm_crtc *crtc,
+				    struct drm_atomic_state *crtc_state);
+
+/**
+ * DRM_CRTC_HELPER_VBLANK_FUNCS - Default implementation for VBLANK helpers
+ *
+ * This macro initializes struct &drm_crtc_helper_funcs to default helpers
+ * for VBLANK handling.
+ */
+#define DRM_CRTC_HELPER_VBLANK_FUNCS \
+	.atomic_flush = drm_crtc_vblank_atomic_flush, \
+	.atomic_enable = drm_crtc_vblank_atomic_enable, \
+	.atomic_disable = drm_crtc_vblank_atomic_disable
+
 /*
  * VBLANK timer
  */
-- 
2.34.1
