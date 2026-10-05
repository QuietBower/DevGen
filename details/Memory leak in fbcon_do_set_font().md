# 🐛 Memory leak in fbcon_do_set_font()

## 📌 Overview
* **Location:** `drivers/video/fbdev/core/fbcon.c`
* **Current Status:** 🆕 **Bug Confirmed** ❌ **WontFix**
* **Notes:** A flawed error path in `fbcon_do_set_font()` skips restoring the `userfont` state when attempting to set a default builtin font. This state corruption prevents `fbcon_free_font()` from freeing the previously allocated user font memory during console destruction. Fixed by unconditionally restoring the `userfont` state.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[mail](../email_lore/162_%5BPATCH%207.0%5D%20fbdev_%20fbcon_%20fix%20memory%20leak%20in%20error%20path%20of%20fbcon_do_se.md)</u> |
| Ping:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[mail](../email_lore/216_Re_%5BPATCH%207.0%5D%20fbdev_%20fbcon_%20fix%20memory%20leak%20in%20error%20path%20of%20fbcon_do.md)</u> |
| sashiko-result:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[mail](../email_lore/216_Re_%5BPATCH%207.0%5D%20fbdev_%20fbcon_%20fix%20memory%20leak%20in%20error%20path%20of%20fbcon_do.md)</u> |
| Re:Re:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[mail](../email_lore/216_Re_%5BPATCH%207.0%5D%20fbdev_%20fbcon_%20fix%20memory%20leak%20in%20error%20path%20of%20fbcon_do.md)</u> |
| Greg KH: Re: Re:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[mail](../email_lore/403_Re_%20Re_%5BPATCH%207.0%5D%20fbdev_%20fbcon_%20fix%20memory%20leak%20in%20error%20path%20of%20fbco.md)</u> |
| Re:Re: Re:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font() | <u>[mail](../email_lore/216_Re_%5BPATCH%207.0%5D%20fbdev_%20fbcon_%20fix%20memory%20leak%20in%20error%20path%20of%20fbcon_do.md)</u> |

