# 🐛 Hrtimer interrupt storm in vmw_vkms_enable_vblank()

## 📌 Overview
* **Location:** `drivers/gpu/drm/vmwgfx/vmwgfx_vkms.c`
* **Current Status:** 👀 **Confirmed** 🛠️ **Patch Sent**
* **Notes:** Submitting a malicious display mode with a massive pixel clock and small resolution causes integer truncation in `drm_calc_timestamping_constants()`, resulting in a 0-ns frame duration. This completely starves the CPU in an infinite hrtimer hard-IRQ loop (`vkms_vblank_simulate()`), leading to severe RCU stalls and a system lockup. Fixed by adding a defensive sanity check to reject 0-period vblanks.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] drm/vmwgfx: Fix hrtimer interrupt storm due to 0-period vblank | <u>[lore.kernel.org](https://lore.kernel.org/all/20260518071741.441794-1-w15303746062@163.com/)</u> |
| Re: [PATCH] drm/vmwgfx: Fix hrtimer interrupt storm due to 0-period vblank | <u>[lore.kernel.org](https://lore.kernel.org/all/fa91ccc0-7660-44fc-92a8-ab569ebe3a7c@suse.de/)</u> |
| [PATCH v2] drm/vblank: Reject 0-period timers to prevent hrtimer storm | <u>[lore.kernel.org](https://lore.kernel.org/all/20260523025447.581709-1-w15303746062@163.com/)</u> |
| Ping:[PATCH v2] drm/vblank: Reject 0-period timers to prevent hrtimer storm | <u>[lore.kernel.org](https://lore.kernel.org/all/33239270.5344.19e9683e439.Coremail.w15303746062@163.com/)</u> |
| Re:Re:[PATCH v2] drm/vblank: Reject 0-period timers to prevent hrtimer storm | <u>[lore.kernel.org](https://lore.kernel.org/all/238fcbf9.514c.19fa257da64.Coremail.w15303746062@163.com/)</u> |

