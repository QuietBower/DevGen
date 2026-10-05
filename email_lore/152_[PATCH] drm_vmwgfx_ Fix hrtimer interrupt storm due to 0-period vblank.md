# [PATCH] drm/vmwgfx: Fix hrtimer interrupt storm due to 0-period vblank

| Field | Value |
| :--- | :--- |
| From | author-a-alt@example.com |
| To | zack.rusin@broadcom.com, maarten.lankhorst@linux.intel.com, mripard@kernel.org, tzimmermann@suse.de, airlied@gmail.com, simona@ffwll.ch |
| Cc | bcm-kernel-feedback-list@broadcom.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, stable@vger.kernel.org, Author A <author-a@example.com> |
| Date | Mon, 18 May 2026 15:17:41 +0800 |
| Message-ID | `20260518071741.441794-1-author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260518071741.441794-1-author-a-alt@example.com/` |

---

From: Author A <author-a@example.com>

When vmwgfx is configured to use VKMS for vblank simulation, it relies
on drm_calc_timestamping_constants() to calculate the frame duration
(vblank->framedur_ns).

However, Fuzzers (like Syzkaller) can submit extremely malicious
display modes through DRM_IOCTL_MODE_SETCRTC. If the user-space passes
a mode with a massive pixel clock (crtc_clock) and small resolution
(htotal/vtotal), the integer division in drm_calc_timestamping_constants()
truncates the result to 0.

Consequently, vmw_vkms_enable_vblank() blindly sets the hrtimer period
to 0. When the timer is started, it fires instantly and continuously.
Because hrtimer_forward_now() cannot advance time for a 0-period,
the overrun value skyrockets, locking the CPU in an infinite hard-IRQ
loop (vkms_vblank_simulate() -> HRTIMER_RESTART).

This completely starves the CPU, leading to massive RCU stalls and
blocking other essential tasks (like jbd2 and writeback workers)
indefinitely:

  [ C1] vkms_vblank_simulate: vblank timer overrun
  ...
  INFO: task kworker/u18:2:50 blocked for more than 143 seconds.
  Workqueue: writeback wb_workfn (flush-8:0)
  Call Trace:
   <TASK>
   __schedule+0x1044/0x5bb0
   wbt_wait+0x1c8/0x3b0
   blk_mq_submit_bio+0x29fa/0x31f0
   submit_bio_noacct+0xca7/0x1f90
   ext4_bio_write_folio+0x95a/0x1d10
   ...

  NMI backtrace for cpu 1
  Call Trace:
   <IRQ>
   vkms_vblank_simulate+0x8f/0x390
   __hrtimer_run_queues+0x1f5/0xb30
   hrtimer_interrupt+0x39a/0x880

Fix this DoS vulnerability by adding a defensive sanity check in
vmw_vkms_enable_vblank() to reject a 0-ns frame duration, allowing
DRM core to gracefully fallback/reject the mode without crashing.

Fixes: cd2eb57df1b8 ("drm/vmwgfx: Implement virtual kms")
Cc: stable@vger.kernel.org
Signed-off-by: Author A <author-a@example.com>
---
 drivers/gpu/drm/vmwgfx/vmwgfx_vkms.c | 10 ++++++++++
 1 file changed, 10 insertions(+)

diff --git a/drivers/gpu/drm/vmwgfx/vmwgfx_vkms.c b/drivers/gpu/drm/vmwgfx/vmwgfx_vkms.c
index 5abd7f5ad2db..b3950ae424f3 100644
--- a/drivers/gpu/drm/vmwgfx/vmwgfx_vkms.c
+++ b/drivers/gpu/drm/vmwgfx/vmwgfx_vkms.c
@@ -288,6 +288,16 @@ vmw_vkms_enable_vblank(struct drm_crtc *crtc)
 
 	drm_calc_timestamping_constants(crtc, &crtc->mode);
 
+	/*
+	 * DEFENSIVE CHECK:
+	 * drm_calc_timestamping_constants() can calculate a framedur_ns
+	 * of 0 if user-space provides a malicious mode with a huge
+	 * crtc_clock and small htotal/vtotal due to integer division
+	 * truncation. Prevent hrtimer interrupt storms by refusing such modes.
+	 */
+	if (WARN_ON_ONCE(vblank->framedur_ns == 0))
+		return -EINVAL;
+
 	hrtimer_setup(&du->vkms.timer, &vmw_vkms_vblank_simulate, CLOCK_MONOTONIC,
 		      HRTIMER_MODE_REL);
 	du->vkms.period_ns = ktime_set(0, vblank->framedur_ns);
-- 
2.34.1
