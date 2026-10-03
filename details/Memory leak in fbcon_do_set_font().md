# 🐛 Memory leak in fbcon_do_set_font()

## 📌 Overview
* **Location:** `drivers/video/fbdev/core/fbcon.c`
* **Current Status:** 🆕 **Bug Confirmed** ❌ **WontFix**
* **Notes:** A flawed error path in `fbcon_do_set_font()` skips restoring the `userfont` state when attempting to set a default builtin font. This state corruption prevents `fbcon_free_font()` from freeing the previously allocated user font memory during console destruction. Fixed by unconditionally restoring the `userfont` state.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>lore.kernel.org</u> |
| Ping:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>lore.kernel.org</u> |
| sashiko-result:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>lore.kernel.org</u> |
| Re:Re:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>lore.kernel.org</u> |
| Greg KH: Re: Re:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>lore.kernel.org</u> |
| Re:Re: Re:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>lore.kernel.org</u> |

