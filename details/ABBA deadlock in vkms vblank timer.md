# 🐛 ABBA deadlock in vkms vblank timer

## 📌 Overview
* **Location:** `drivers/gpu/drm/vkms/vkms_crtc.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** An ABBA deadlock occurs between `drm_vblank_disable_and_save()` and the `vkms_vblank_simulate()` hrtimer callback, causing an RCU preempt stall. Fixed by replacing `hrtimer_cancel()` with `hrtimer_try_to_cancel()` to safely abort the timer and prevent infinite spinning

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[lore.kernel.org](https://lore.kernel.org/all/20260515131826.388154-1-w15303746062@163.com/)</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[lore.kernel.org](https://lore.kernel.org/all/2026051557-thermal-petite-7da0@gregkh/)</u> |
| Re:Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[lore.kernel.org](https://lore.kernel.org/all/581657f0.ba8.19e2eaaf003.Coremail.w15303746062@163.com/)</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[lore.kernel.org](https://lore.kernel.org/all/2026051633-skyward-parrot-cdd3@gregkh/)</u> |
| Re:Re: Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/Re%20%5BPATCH%206.18.y%5D%20drmvkms%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20and%20timer%20callback.txt)</u> |
| Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/Re%20Re%20%5BPATCH%206.18.y%5D%20drmvkms%20Fix%20ABBA%20deadlock%20in%20vblank%20disable%20and%20timer%20callback.txt)</u> |
| [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[lore.kernel.org](https://lore.kernel.org/stable/20260525131610.608273-1-w15303746062@163.com/)</u> |
| [PATCH v2 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[lore.kernel.org](https://lore.kernel.org/stable/20260526133123.691465-1-w15303746062@163.com/)</u> |
| [PATCH v2 6.18.y 1/5] drm/vblank: Add vblank timer           | <u>[lore.kernel.org](https://lore.kernel.org/stable/20260526133123.691465-2-w15303746062@163.com/)</u> |
| [PATCH v2 6.18.y 2/5] drm/vblank: Add CRTC helpers for simple use cases | <u>[lore.kernel.org](https://lore.kernel.org/stable/20260526133123.691465-3-w15303746062@163.com/)</u> |
| [PATCH v2 6.18.y 3/5] drm/vkms: Convert to DRM's vblank timer | <u>[lore.kernel.org](https://lore.kernel.org/stable/20260526133123.691465-4-w15303746062@163.com/)</u> |
| [PATCH v2 6.18.y 4/5] drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks() | <u>[lore.kernel.org](https://lore.kernel.org/stable/20260526133123.691465-5-w15303746062@163.com/)</u> |
| [PATCH v2 6.18.y 5/5] drm/vblank: Fix kernel docs for vblank timer | <u>[lore.kernel.org](https://lore.kernel.org/stable/20260526133123.691465-6-w15303746062@163.com/)</u> |
| Maarten:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/1_Re%20%5BPATCH%206.18.y%2005%5D%20drmvkms%20Backport%20generic%20vblank%20timer%20to%20fix%20ABBA%20deadlock.txt)</u> |
| Sasha:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/2_Re%20%5BPATCH%206.18.y%2005%5D%20drmvkms%20Backport%20generic%20vblank%20timer%20to%20fix%20ABBA%20deadlock.txt)</u> |
| Maarten:Re: [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/3_Re%20%5BPATCH%206.18.y%2005%5D%20drmvkms%20Backport%20generic%20vblank%20timer%20to%20fix%20ABBA%20deadlock.txt)</u> |
| Sasha:Re: [PATCH v2 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/4_Re%20%5BPATCH%20v2%206.18.y%2005%5D%20drmvkms%20Backport%20generic%20vblank%20timer%20to%20fix%20ABBA%20deadlock.txt)</u> |
| [PATCH 6.18 021/377] drm/vblank: Add vblank timer            | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/%5BPATCH%206.18%20021377%5D%20drmvblank%20Add%20vblank%20timer.txt)</u> |
| [PATCH 6.18 022/377] drm/vblank: Add CRTC helpers for simple use cases | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/%5BPATCH%206.18%20022377%5D%20drmvblank%20Add%20CRTC%20helpers%20for%20simple%20use%20cases.txt)</u> |
| [PATCH 6.18 023/377] drm/vkms: Convert to DRMs vblank timer  | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/%5BPATCH%206.18%20023377%5D%20drmvkms%20Convert%20to%20DRMs%20vblank%20timer.txt)</u> |
| [PATCH 6.18 024/377] drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks() | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/%5BPATCH%206.18%20024377%5D%20drmatomic%20Increase%20timeout%20in%20drm_atomic_helper_wait_for_vblanks().txt)</u> |
| [PATCH 6.18 025/377] drm/vblank: Fix kernel docs for vblank timer | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/%5BPATCH%206.18%20025377%5D%20drmvblank%20Fix%20kernel%20docs%20for%20vblank%20timer.txt)</u> |

