# 🐛 Hrtimer interrupt storm in vmw_vkms_enable_vblank()

## 📌 Overview
* **Location:** `drivers/gpu/drm/vmwgfx/vmwgfx_vkms.c`
* **Current Status:** 👀 **Confirmed** 🛠️ **Patch Sent**
* **Notes:** Submitting a malicious display mode with a massive pixel clock and small resolution causes integer truncation in `drm_calc_timestamping_constants()`, resulting in a 0-ns frame duration. This completely starves the CPU in an infinite hrtimer hard-IRQ loop (`vkms_vblank_simulate()`), leading to severe RCU stalls and a system lockup. Fixed by adding a defensive sanity check to reject 0-period vblanks.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] drm/vmwgfx: Fix hrtimer interrupt storm due to 0-period vblank | <u>[mail](../email/152_%5BPATCH%5D%20drm_vmwgfx_%20Fix%20hrtimer%20interrupt%20storm%20due%20to%200-period%20vblank.txt)</u> |
| Re: [PATCH] drm/vmwgfx: Fix hrtimer interrupt storm due to 0-period vblank | <u>[mail](../email/156_Re_%20%5BPATCH%5D%20drm_vmwgfx_%20Fix%20hrtimer%20interrupt%20storm%20due%20to%200-period%20vb.txt)</u> |
| [PATCH v2] drm/vblank: Reject 0-period timers to prevent hrtimer storm | <u>[mail](../email/159_%5BPATCH%20v2%5D%20drm_vblank_%20Reject%200-period%20timers%20to%20prevent%20hrtimer%20storm.txt)</u> |
| Ping:[PATCH v2] drm/vblank: Reject 0-period timers to prevent hrtimer storm | <u>[mail](../email/218_Re_%5BPATCH%20v2%5D%20drm_vblank_%20Reject%200-period%20timers%20to%20prevent%20hrtimer%20st.txt)</u> |
| Re:Re:[PATCH v2] drm/vblank: Reject 0-period timers to prevent hrtimer storm | <u>[mail](../email/405_Re_Re_%5BPATCH%20v2%5D%20drm_vblank_%20Reject%200-period%20timers%20to%20prevent%20hrtimer.txt)</u> |

