# 🐛 UAF and GPF in idempotent_init_module()

## 📌 Overview
* **Location:** `kernel/module/main.c`
* **Current Status:** 🆕 **Bug Confirmed** ❌ **WontFix**
* **Notes:** A stack-allocated node linked to a global list causes a stale pointer dereference if the module-loading task is abruptly terminated. Fixed by using heap allocation to safely decouple the node's lifespan from the process stack.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[lore.kernel.org](https://lore.kernel.org/all/20260717082601.81704-1-25181214217@stu.xidian.edu.cn/)</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[lore.kernel.org](https://lore.kernel.org/all/20260717084017.A9CFE1F000E9@smtp.kernel.org/)</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[lore.kernel.org](https://lore.kernel.org/all/2459be1a-7507-4bc4-a235-09c18ca6b8a6@stu.xidian.edu.cn/)</u> |
| Petr Pavlu:Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[lore.kernel.org](https://lore.kernel.org/stable/8d507b50-b5a2-48a8-a4bb-f196ac28b5e4@suse.com/)</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[lore.kernel.org](https://lore.kernel.org/stable/f79bc014-a606-4ab5-adfd-1b56dbdc30ba@stu.xidian.edu.cn/)</u> |
| Petr Pavlu:Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[lore.kernel.org](https://lore.kernel.org/stable/1951733c-0d8f-4d1d-9da1-fd71d23781de@suse.com/)</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[lore.kernel.org](https://lore.kernel.org/stable/cb04f43b-c86f-44f2-ba13-d172d7e03783@stu.xidian.edu.cn/)</u> |

