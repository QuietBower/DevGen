# 🐛 Hrtimer livelock via unvalidated display mode

## 📌 Overview
* **Location:** `drivers/gpu/drm/vkms/vkms_crtc.c`
* **Current Status:**  🔄 **Indirectly Fixed**
* **Notes:** A lack of bounds checking on display mode parameters allows user-space to pass a malicious mode (e.g., massive `crtc_clock` and tiny `htotal`/`vtotal`). This causes the calculated vblank timer period to approach zero, triggering an hrtimer interrupt storm that deadlocks the CPU (RCU stall). Fixed by rejecting display modes in `vkms_crtc_atomic_check()` that result in an implied refresh rate greater than 1000 Hz.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] drm/vkms: sanitize display mode to prevent hrtimer livelock | <u>[lore.kernel.org](https://lore.kernel.org/all/20260527034733.701705-1-w15303746062@163.com/)</u> |

