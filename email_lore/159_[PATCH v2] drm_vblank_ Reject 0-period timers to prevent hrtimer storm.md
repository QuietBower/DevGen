# [PATCH v2] drm/vblank: Reject 0-period timers to prevent hrtimer storm

| Field | Value |
| :--- | :--- |
| From | author-a-alt@example.com |
| To | maarten.lankhorst@linux.intel.com, mripard@kernel.org, tzimmermann@suse.de, airlied@gmail.com, simona@ffwll.ch |
| Cc | zack.rusin@broadcom.com, bcm-kernel-feedback-list@broadcom.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, stable@vger.kernel.org, Author A <author-a@example.com> |
| Date | Sat, 23 May 2026 10:54:47 +0800 |
| Message-ID | `20260523025447.581709-1-author-a-alt@example.com` |
| In-Reply-To | `fa91ccc0-7660-44fc-92a8-ab569ebe3a7c@suse.de` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260523025447.581709-1-author-a-alt@example.com/` |

---

From: Author A <author-a@example.com>

Fuzzers like Syzkaller can submit extremely malicious display modes
through DRM_IOCTL_MODE_SETCRTC. If userspace passes a mode with a
massive pixel clock (crtc_clock) and small resolution (htotal/vtotal),
the integer division in drm_calc_timestamping_constants() truncates
the resulting frame duration (vblank->framedur_ns) to 0.

When virtual display drivers (such as vmwgfx or vkms) rely on the DRM
core's software vblank simulation, drm_crtc_vblank_start_timer() is
called. It blindly converts this 0-ns framedur_ns into a ktime interval
and starts the hrtimer. An hrtimer with a 0-period fires instantly and
continuously. Since hrtimer_forward_now() cannot advance time for a
0-period, the CPU gets locked in an infinite hard-IRQ loop, starving
the system and causing massive RCU stalls.

Fix this DoS vulnerability by adding a defensive sanity check in
drm_crtc_vblank_start_timer() to reject a 0-ns frame duration, allowing
the DRM core to gracefully reject the malicious mode.

Signed-off-by: Author A <author-a@example.com>
---
Changes in v2:
- Moved the defensive check from vmwgfx to drm_vblank.c. The timer
  logic was refactored into the DRM core, so placing the check here
  protects all drivers relying on the core software vblank timer.
- Dropped WARN_ON_ONCE() to prevent unprivileged userspace from easily
  triggering kernel panics on systems with panic_on_warn enabled.

 drivers/gpu/drm/drm_vblank.c | 10 ++++++++++
 1 file changed, 10 insertions(+)

diff --git a/drivers/gpu/drm/drm_vblank.c b/drivers/gpu/drm/drm_vblank.c
index f90fb2d13e42..b38d0b30a651 100644
--- a/drivers/gpu/drm/drm_vblank.c
+++ b/drivers/gpu/drm/drm_vblank.c
@@ -2241,6 +2241,16 @@ int drm_crtc_vblank_start_timer(struct drm_crtc *crtc)
 
 	drm_calc_timestamping_constants(crtc, &crtc->mode);
 
+	/*
+	 * DEFENSIVE CHECK:
+	 * drm_calc_timestamping_constants() truncates framedur_ns to 0 if
+	 * userspace provides a malicious mode with a huge crtc_clock and
+	 * small htotal/vtotal. Prevent an infinite hard-IRQ loop from a
+	 * 0-period hrtimer by rejecting such modes.
+	 */
+	if (unlikely(vblank->framedur_ns == 0))
+		return -EINVAL;
+
 	spin_lock_irqsave(&vtimer->interval_lock, flags);
 	vtimer->interval = ns_to_ktime(vblank->framedur_ns);
 	spin_unlock_irqrestore(&vtimer->interval_lock, flags);
-- 
2.34.1
