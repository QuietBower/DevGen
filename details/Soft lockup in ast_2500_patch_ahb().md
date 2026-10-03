# 🐛 Soft lockup in ast_2500_patch_ahb()

## 📌 Overview
* **Location:** `drivers/gpu/drm/ast/ast_2500.c`
* **Current Status:** 👀 **Confirmed** 🛠️ **Patch Sent**
* **Notes:** A critical lack of timeout mechanisms in hardware polling loops (`ast_2500_patch_ahb` and `__ast_mindwm`) causes an infinite loop when the hardware is unresponsive. This leads to a severe CPU soft lockup (143s+) and complete system paralysis. 

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| BUG: drm/ast: soft lockup due to missing timeout in hardware polling (ast_2500_patch_ahb) | <u>lore.kernel.org</u> |
| Re: BUG: drm/ast: soft lockup due to missing timeout in hardware polling (ast_2500_patch_ahb) | <u>lore.kernel.org</u> |
| [PATCH] drm/ast: Add timeouts to AHB/SCU polling loops to prevent soft lockups | <u>lore.kernel.org</u> |
| Ping:[PATCH] drm/ast: Add timeouts to AHB/SCU polling loops to prevent soft lockups | <u>lore.kernel.org</u> |
| Re:Re:[PATCH] drm/ast: Add timeouts to AHB/SCU polling loops to prevent soft lockups | <u>lore.kernel.org</u> |
|                                                              |                                                              |

