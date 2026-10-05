# 🐛 ABBA deadlock in vkms vblank timer

## 📌 Overview
* **Location:** `drivers/gpu/drm/vkms/vkms_crtc.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** An ABBA deadlock occurs between `drm_vblank_disable_and_save()` and the `vkms_vblank_simulate()` hrtimer callback, causing an RCU preempt stall. Fixed by replacing `hrtimer_cancel()` with `hrtimer_try_to_cancel()` to safely abort the timer and prevent infinite spinning

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email_lore/139_Re_%20%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20and%20t.md)</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email_lore/139_Re_%20%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20and%20t.md)</u> |
| Re:Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email_lore/139_Re_%20%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20and%20t.md)</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email_lore/146_Re_%20Re_%20%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20a.md)</u> |
| Re:Re: Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email_lore/527_Re_Re_%20Re_%20%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disabl.md)</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email_lore/533_Re_%20%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20and%20t.md)</u> |
| [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email_lore/163_%5BPATCH%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20fix%20ABBA.md)</u> |
| [PATCH v2 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email_lore/163_%5BPATCH%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20fix%20ABBA.md)</u> |
| [PATCH v2 6.18.y 1/5] drm/vblank: Add vblank timer | <u>[mail](../email_lore/164_%5BPATCH%206.18.y%201_5%5D%20drm_vblank_%20Add%20vblank%20timer.md)</u> |
| [PATCH v2 6.18.y 2/5] drm/vblank: Add CRTC helpers for simple use cases | <u>[mail](../email_lore/165_%5BPATCH%206.18.y%202_5%5D%20drm_vblank_%20Add%20CRTC%20helpers%20for%20simple%20use%20cases.md)</u> |
| [PATCH v2 6.18.y 3/5] drm/vkms: Convert to DRM's vblank timer | <u>[mail](../email_lore/166_%5BPATCH%206.18.y%203_5%5D%20drm_vkms_%20Convert%20to%20DRM%27s%20vblank%20timer.md)</u> |
| [PATCH v2 6.18.y 4/5] drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks() | <u>[mail](../email_lore/167_%5BPATCH%206.18.y%204_5%5D%20drm_atomic_%20Increase%20timeout%20in%20drm_atomic_helper_w.md)</u> |
| [PATCH v2 6.18.y 5/5] drm/vblank: Fix kernel docs for vblank timer | <u>[mail](../email_lore/168_%5BPATCH%206.18.y%205_5%5D%20drm_vblank_%20Fix%20kernel%20docs%20for%20vblank%20timer.md)</u> |
| Maarten:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email_lore/501_Re_%20%5BPATCH%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20fix.md)</u> |
| Sasha:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email_lore/503_Re_%20%5BPATCH%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20fix.md)</u> |
| Maarten:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email_lore/505_Re_%20%5BPATCH%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20fix.md)</u> |
| Sasha:Re: [PATCH v2 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email_lore/506_Re_%20%5BPATCH%20v2%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20f.md)</u> |
| [PATCH 6.18 021/377] drm/vblank: Add vblank timer | <u>[mail](../email_lore/510_%5BPATCH%206.18%20021_377%5D%20drm_vblank_%20Add%20vblank%20timer.md)</u> |
| [PATCH 6.18 022/377] drm/vblank: Add CRTC helpers for simple use cases | <u>[mail](../email_lore/511_%5BPATCH%206.18%20022_377%5D%20drm_vblank_%20Add%20CRTC%20helpers%20for%20simple%20use%20cases.md)</u> |
| [PATCH 6.18 023/377] drm/vkms: Convert to DRMs vblank timer | <u>[mail](../email_lore/512_%5BPATCH%206.18%20023_377%5D%20drm_vkms_%20Convert%20to%20DRMs%20vblank%20timer.md)</u> |
| [PATCH 6.18 024/377] drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks() | <u>[mail](../email_lore/167_%5BPATCH%206.18.y%204_5%5D%20drm_atomic_%20Increase%20timeout%20in%20drm_atomic_helper_w.md)</u> |
| [PATCH 6.18 025/377] drm/vblank: Fix kernel docs for vblank timer | <u>[mail](../email_lore/514_%5BPATCH%206.18%20025_377%5D%20drm_vblank_%20Fix%20kernel%20docs%20for%20vblank%20timer.md)</u> |

