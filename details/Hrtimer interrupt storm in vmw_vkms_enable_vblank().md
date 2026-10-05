# 🐛 Hrtimer interrupt storm in vmw_vkms_enable_vblank()

## 📌 Overview
* **Location:** `drivers/gpu/drm/vmwgfx/vmwgfx_vkms.c`
* **Current Status:** 👀 **Confirmed** 🛠️ **Patch Sent**
* **Notes:** Submitting a malicious display mode with a massive pixel clock and small resolution causes integer truncation in `drm_calc_timestamping_constants()`, resulting in a 0-ns frame duration. This completely starves the CPU in an infinite hrtimer hard-IRQ loop (`vkms_vblank_simulate()`), leading to severe RCU stalls and a system lockup. Fixed by adding a defensive sanity check to reject 0-period vblanks.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] drm/vmwgfx: Fix hrtimer interrupt storm due to 0-period vblank | <u>[mail](../email_lore/152_%5BPATCH%5D%20drm_vmwgfx_%20Fix%20hrtimer%20interrupt%20storm%20due%20to%200-period%20vblank.md)</u> |
| Re: [PATCH] drm/vmwgfx: Fix hrtimer interrupt storm due to 0-period vblank | <u>[mail](../email_lore/156_Re_%20%5BPATCH%5D%20drm_vmwgfx_%20Fix%20hrtimer%20interrupt%20storm%20due%20to%200-period%20vb.md)</u> |
| [PATCH v2] drm/vblank: Reject 0-period timers to prevent hrtimer storm | <u>[mail](../email_lore/159_%5BPATCH%20v2%5D%20drm_vblank_%20Reject%200-period%20timers%20to%20prevent%20hrtimer%20storm.md)</u> |
| Ping:[PATCH v2] drm/vblank: Reject 0-period timers to prevent hrtimer storm | <u>[mail](../email_lore/159_%5BPATCH%20v2%5D%20drm_vblank_%20Reject%200-period%20timers%20to%20prevent%20hrtimer%20storm.md)</u> |
| Re:Re:[PATCH v2] drm/vblank: Reject 0-period timers to prevent hrtimer storm | <u>[mail](../email_lore/159_%5BPATCH%20v2%5D%20drm_vblank_%20Reject%200-period%20timers%20to%20prevent%20hrtimer%20storm.md)</u> |

