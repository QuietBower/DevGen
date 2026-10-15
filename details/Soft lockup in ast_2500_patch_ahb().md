# 🐛 Soft lockup in ast_2500_patch_ahb()

## 📌 Overview
* **Location:** `drivers/gpu/drm/ast/ast_2500.c`
* **Current Status:** 👀 **Confirmed** 🛠️ **Patch Sent**
* **Notes:** A critical lack of timeout mechanisms in hardware polling loops (`ast_2500_patch_ahb` and `__ast_mindwm`) causes an infinite loop when the hardware is unresponsive. This leads to a severe CPU soft lockup (143s+) and complete system paralysis. 

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| BUG: drm/ast: soft lockup due to missing timeout in hardware polling (ast_2500_patch_ahb) | <u>[lore.kernel.org](https://lore.kernel.org/all/20260513073649.352831-1-w15303746062@163.com/)</u> |
| Re: BUG: drm/ast: soft lockup due to missing timeout in hardware polling (ast_2500_patch_ahb) | <u>[lore.kernel.org](https://lore.kernel.org/all/b98e1013-25ae-44e1-8905-88f104cc0608@suse.de/)</u> |
| [PATCH] drm/ast: Add timeouts to AHB/SCU polling loops to prevent soft lockups | <u>[lore.kernel.org](https://lore.kernel.org/all/20260513113949.356537-1-w15303746062@163.com/)</u> |
| Ping:[PATCH] drm/ast: Add timeouts to AHB/SCU polling loops to prevent soft lockups | <u>[lore.kernel.org](https://lore.kernel.org/all/796812da.3444.19e95cdf066.Coremail.w15303746062@163.com/)</u> |
| Re:Re:[PATCH] drm/ast: Add timeouts to AHB/SCU polling loops to prevent soft lockups | <u>[lore.kernel.org](https://lore.kernel.org/all/3b2dd257.4efa.19fa24f8626.Coremail.w15303746062@163.com/)</u> |
|                                                              |                                                              |

