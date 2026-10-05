# 🐛 Soft lockup in ast_2500_patch_ahb()

## 📌 Overview
* **Location:** `drivers/gpu/drm/ast/ast_2500.c`
* **Current Status:** 👀 **Confirmed** 🛠️ **Patch Sent**
* **Notes:** A critical lack of timeout mechanisms in hardware polling loops (`ast_2500_patch_ahb` and `__ast_mindwm`) causes an infinite loop when the hardware is unresponsive. This leads to a severe CPU soft lockup (143s+) and complete system paralysis. 

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| BUG: drm/ast: soft lockup due to missing timeout in hardware polling (ast_2500_patch_ahb) | <u>[mail](../email_lore/130_BUG_%20drm_ast_%20soft%20lockup%20due%20to%20missing%20timeout%20in%20hardware%20polling%20%28.md)</u> |
| Re: BUG: drm/ast: soft lockup due to missing timeout in hardware polling (ast_2500_patch_ahb) | <u>[mail](../email_lore/126_Re_%20BUG_%20drm_ast_%20soft%20lockup%20due%20to%20missing%20timeout%20in%20hardware%20polli.md)</u> |
| [PATCH] drm/ast: Add timeouts to AHB/SCU polling loops to prevent soft lockups | <u>[mail](../email_lore/131_%5BPATCH%5D%20drm_ast_%20Add%20timeouts%20to%20AHB_SCU%20polling%20loops%20to%20prevent%20soft.md)</u> |
| Ping:[PATCH] drm/ast: Add timeouts to AHB/SCU polling loops to prevent soft lockups | <u>[mail](../email_lore/131_%5BPATCH%5D%20drm_ast_%20Add%20timeouts%20to%20AHB_SCU%20polling%20loops%20to%20prevent%20soft.md)</u> |
| Re:Re:[PATCH] drm/ast: Add timeouts to AHB/SCU polling loops to prevent soft lockups | <u>[mail](../email_lore/131_%5BPATCH%5D%20drm_ast_%20Add%20timeouts%20to%20AHB_SCU%20polling%20loops%20to%20prevent%20soft.md)</u> |
|                                                              |                                                              |

