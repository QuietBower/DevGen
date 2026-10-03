# 🐛 Memory leak in fbcon_do_set_font()

## 📌 Overview
* **Location:** `drivers/video/fbdev/core/fbcon.c`
* **Current Status:** 🆕 **Bug Confirmed** ❌ **WontFix**
* **Notes:** A flawed error path in `fbcon_do_set_font()` skips restoring the `userfont` state when attempting to set a default builtin font. This state corruption prevents `fbcon_free_font()` from freeing the previously allocated user font memory during console destruction. Fixed by unconditionally restoring the `userfont` state.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260525082741.600003-1-w15303746062@163.com/)</u> |
| Ping:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[lore.kernel.org](https://lore.kernel.org/all/5b288246.378d.19e95d82602.Coremail.w15303746062@163.com/)</u> |
| sashiko-result:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[lore.kernel.org](https://sashiko.dev/#/patchset/20260525082741.600003-1-w15303746062%40163.com)</u> |
| Re:Re:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[lore.kernel.org](https://lore.kernel.org/all/726d129f.53bd.19fa2601661.Coremail.w15303746062@163.com/)</u> |
| Greg KH: Re: Re:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[lore.kernel.org](https://lore.kernel.org/all/2026072719-kissable-antonym-d31e@gregkh/)</u> |
| Re:Re: Re:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[lore.kernel.org](https://lore.kernel.org/all/39aeae7d.5bf9.19fa2796cc0.Coremail.w15303746062@163.com/)</u> |

