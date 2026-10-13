# 🐛 ABBA deadlock in vkms vblank timer

## 📌 Overview
* **Location:** `drivers/gpu/drm/vkms/vkms_crtc.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** An ABBA deadlock occurs between `drm_vblank_disable_and_save()` and the `vkms_vblank_simulate()` hrtimer callback, causing an RCU preempt stall. Fixed by replacing `hrtimer_cancel()` with `hrtimer_try_to_cancel()` to safely abort the timer and prevent infinite spinning

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email/140_%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20and%20timer.txt)</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email/139_Re_%20%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20and%20t.txt)</u> |
| Re:Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email/145_Re_Re_%20%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20an.txt)</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email/146_Re_%20Re_%20%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20a.txt)</u> |
| Re:Re: Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email/150_Re_Re_%20Re_%20%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disabl.txt)</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[mail](../email/451_Re_%20%5BPATCH%206.18.y%5D%20drm_vkms_%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20and%20t.txt)</u> |
| [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email/163_%5BPATCH%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20fix%20ABBA.txt)</u> |
| [PATCH v2 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email/171_%5BPATCH%20v2%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20fix%20A.txt)</u> |
| [PATCH v2 6.18.y 1/5] drm/vblank: Add vblank timer | <u>[mail](../email/172_%5BPATCH%20v2%206.18.y%201_5%5D%20drm_vblank_%20Add%20vblank%20timer.txt)</u> |
| [PATCH v2 6.18.y 2/5] drm/vblank: Add CRTC helpers for simple use cases | <u>[mail](../email/173_%5BPATCH%20v2%206.18.y%202_5%5D%20drm_vblank_%20Add%20CRTC%20helpers%20for%20simple%20use%20case.txt)</u> |
| [PATCH v2 6.18.y 3/5] drm/vkms: Convert to DRM's vblank timer | <u>[mail](../email/174_%5BPATCH%20v2%206.18.y%203_5%5D%20drm_vkms_%20Convert%20to%20DRM%27s%20vblank%20timer.txt)</u> |
| [PATCH v2 6.18.y 4/5] drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks() | <u>[mail](../email/175_%5BPATCH%20v2%206.18.y%204_5%5D%20drm_atomic_%20Increase%20timeout%20in%20drm_atomic_helpe.txt)</u> |
| [PATCH v2 6.18.y 5/5] drm/vblank: Fix kernel docs for vblank timer | <u>[mail](../email/176_%5BPATCH%20v2%206.18.y%205_5%5D%20drm_vblank_%20Fix%20kernel%20docs%20for%20vblank%20timer.txt)</u> |
| Maarten:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email/450_Re_%20%5BPATCH%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20fix.txt)</u> |
| Sasha:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email/449_Re_%20%5BPATCH%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20fix.txt)</u> |
| Maarten:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email/448_Re_%20%5BPATCH%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20fix.txt)</u> |
| Sasha:Re: [PATCH v2 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[mail](../email/447_Re_%20%5BPATCH%20v2%206.18.y%200_5%5D%20drm_vkms_%20Backport%20generic%20vblank%20timer%20to%20f.txt)</u> |
| [PATCH 6.18 021/377] drm/vblank: Add vblank timer | <u>[mail](../email/187_%5BPATCH%206.18%20021_377%5D%20drm_vblank_%20Add%20vblank%20timer.txt)</u> |
| [PATCH 6.18 022/377] drm/vblank: Add CRTC helpers for simple use cases | <u>[mail](../email/188_%5BPATCH%206.18%20022_377%5D%20drm_vblank_%20Add%20CRTC%20helpers%20for%20simple%20use%20cases.txt)</u> |
| [PATCH 6.18 023/377] drm/vkms: Convert to DRMs vblank timer | <u>[mail](../email/189_%5BPATCH%206.18%20023_377%5D%20drm_vkms_%20Convert%20to%20DRMs%20vblank%20timer.txt)</u> |
| [PATCH 6.18 024/377] drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks() | <u>[mail](../email/190_%5BPATCH%206.18%20024_377%5D%20drm_atomic_%20Increase%20timeout%20in%20drm_atomic_helper.txt)</u> |
| [PATCH 6.18 025/377] drm/vblank: Fix kernel docs for vblank timer | <u>[mail](../email/191_%5BPATCH%206.18%20025_377%5D%20drm_vblank_%20Fix%20kernel%20docs%20for%20vblank%20timer.txt)</u> |

