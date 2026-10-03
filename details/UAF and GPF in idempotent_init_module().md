# 🐛 UAF and GPF in idempotent_init_module()

## 📌 Overview
* **Location:** `kernel/module/main.c`
* **Current Status:** 🆕 **Bug Confirmed** ❌ **WontFix**
* **Notes:** A stack-allocated node linked to a global list causes a stale pointer dereference if the module-loading task is abruptly terminated. Fixed by using heap allocation to safely decouple the node's lifespan from the process stack.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>lore.kernel.org</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>lore.kernel.org</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>lore.kernel.org</u> |
| Petr Pavlu:Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>lore.kernel.org</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>lore.kernel.org</u> |
| Petr Pavlu:Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>lore.kernel.org</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>lore.kernel.org</u> |

