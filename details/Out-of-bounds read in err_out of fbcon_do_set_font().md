# 🐛 Out-of-bounds read in err_out of fbcon_do_set_font()

## 📌 Overview
* **Location:** `drivers/video/fbdev/core/fbcon.c`
* **Current Status:** ✅ **Patch Accepted** **CVE-2026-53402** **HIGH 7.1**
* **Notes:** When `fbcon_do_set_font()` fails (e.g., due to memory allocation failures inside `vc_resize()` under heavy memory pressure), it jumps to the `err_out` label to roll back the console state. However, the error recovery logic fails to roll back the `hi_font` state and screen buffer mutations applied by `set_vc_hi_font()`. This desynchronization allows inflation of character indices, leading to a deterministic out-of-bounds read against the reverted font array and potential kernel memory disclosure. Fixed by adding the missing `hi_font` mask and screen buffer rollback logic in the error recovery path.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>lore.kernel.org</u> |
| sashiko-result:[PATCH] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>lore.kernel.org</u> |
| Re: [PATCH] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>lore.kernel.org</u> |
| [PATCH v2] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>lore.kernel.org</u> |
| Re: [PATCH v2] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>lore.kernel.org</u> |
| fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>lore.kernel.org</u> |

