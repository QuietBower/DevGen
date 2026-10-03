# 🐛 Out-of-bounds read in err_out of fbcon_do_set_font()

## 📌 Overview
* **Location:** `drivers/video/fbdev/core/fbcon.c`
* **Current Status:** ✅ **Patch Accepted** **[CVE-2026-53402](https://www.cve.org/CVERecord?id=CVE-2026-53402)** **HIGH 7.1**
* **Notes:** When `fbcon_do_set_font()` fails (e.g., due to memory allocation failures inside `vc_resize()` under heavy memory pressure), it jumps to the `err_out` label to roll back the console state. However, the error recovery logic fails to roll back the `hi_font` state and screen buffer mutations applied by `set_vc_hi_font()`. This desynchronization allows inflation of character indices, leading to a deterministic out-of-bounds read against the reverted font array and potential kernel memory disclosure. Fixed by adding the missing `hi_font` mask and screen buffer rollback logic in the error recovery path.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260624083316.389677-1-w15303746062@163.com/)</u> |
| sashiko-result:[PATCH] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>[lore.kernel.org](https://sashiko.dev/#/patchset/20260624083316.389677-1-w15303746062%40163.com)</u> |
| Re: [PATCH] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>[lore.kernel.org](https://lore.kernel.org/all/7dd87e55-5170-4317-8c9e-38a7868f68fc@suse.de/)</u> |
| [PATCH v2] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260625160306.438847-1-25181214217@stu.xidian.edu.cn/)</u> |
| Re: [PATCH v2] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>[lore.kernel.org](https://lore.kernel.org/all/79ba53b6-97d6-41dc-aeaf-69181262e8b5@gmx.de/)</u> |
| fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font() | <u>[lore.kernel.org](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=8fdc8c2057eea08d40ce2c8eed41ff9e451c65c2)</u> |

