# 🐛 ABBA deadlock in vkms vblank timer

## 📌 Overview
* **Location:** `drivers/gpu/drm/vkms/vkms_crtc.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** An ABBA deadlock occurs between `drm_vblank_disable_and_save()` and the `vkms_vblank_simulate()` hrtimer callback, causing an RCU preempt stall. Fixed by replacing `hrtimer_cancel()` with `hrtimer_try_to_cancel()` to safely abort the timer and prevent infinite spinning

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>lore.kernel.org</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>lore.kernel.org</u> |
| Re:Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>lore.kernel.org</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>lore.kernel.org</u> |
| Re:Re: Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>lore.kernel.org</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>lore.kernel.org</u> |
| [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>lore.kernel.org</u> |
| [PATCH v2 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>lore.kernel.org</u> |
| [PATCH v2 6.18.y 1/5] drm/vblank: Add vblank timer           | <u>lore.kernel.org</u> |
| [PATCH v2 6.18.y 2/5] drm/vblank: Add CRTC helpers for simple use cases | <u>lore.kernel.org</u> |
| [PATCH v2 6.18.y 3/5] drm/vkms: Convert to DRM's vblank timer | <u>lore.kernel.org</u> |
| [PATCH v2 6.18.y 4/5] drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks() | <u>lore.kernel.org</u> |
| [PATCH v2 6.18.y 5/5] drm/vblank: Fix kernel docs for vblank timer | <u>lore.kernel.org</u> |
| Maarten:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>lore.kernel.org</u> |
| Sasha:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>lore.kernel.org</u> |
| Maarten:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>lore.kernel.org</u> |
| Sasha:Re: [PATCH v2 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>lore.kernel.org</u> |
| [PATCH 6.18 021/377] drm/vblank: Add vblank timer            | <u>lore.kernel.org</u> |
| [PATCH 6.18 022/377] drm/vblank: Add CRTC helpers for simple use cases | <u>lore.kernel.org</u> |
| [PATCH 6.18 023/377] drm/vkms: Convert to DRMs vblank timer  | <u>lore.kernel.org</u> |
| [PATCH 6.18 024/377] drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks() | <u>lore.kernel.org</u> |
| [PATCH 6.18 025/377] drm/vblank: Fix kernel docs for vblank timer | <u>lore.kernel.org</u> |

