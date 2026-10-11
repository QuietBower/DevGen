# 🐛 UAF and GPF in idempotent_init_module()

## 📌 Overview
* **Location:** `kernel/module/main.c`
* **Current Status:** 🆕 **Bug Confirmed** ❌ **WontFix**
* **Notes:** A stack-allocated node linked to a global list causes a stale pointer dereference if the module-loading task is abruptly terminated. Fixed by using heap allocation to safely decouple the node's lifespan from the process stack.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[mail](../email/356_%5BPATCH%5D%20module_%20fix%20UAF%20and%20GPF%20in%20idempotent_init_module%20via%20heap%20all.txt)</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[mail](../email/351_Re_%20%5BPATCH%5D%20module_%20fix%20UAF%20and%20GPF%20in%20idempotent_init_module%20via%20heap.txt)</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[mail](../email/423_Re_%20%5BPATCH%5D%20module_%20fix%20UAF%20and%20GPF%20in%20idempotent_init_module%20via%20heap.txt)</u> |
| Petr Pavlu:Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[mail](../email/422_Re_%20%5BPATCH%5D%20module_%20fix%20UAF%20and%20GPF%20in%20idempotent_init_module%20via%20heap.txt)</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[mail](../email/425_Re_%20%5BPATCH%5D%20module_%20fix%20UAF%20and%20GPF%20in%20idempotent_init_module%20via%20heap.txt)</u> |
| Petr Pavlu:Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[mail](../email/446_Re_%20%5BPATCH%5D%20module_%20fix%20UAF%20and%20GPF%20in%20idempotent_init_module%20via%20heap.txt)</u> |
| Re: [PATCH] module: fix UAF and GPF in idempotent_init_module via heap allocation | <u>[mail](../email/426_Re_%20%5BPATCH%5D%20module_%20fix%20UAF%20and%20GPF%20in%20idempotent_init_module%20via%20heap.txt)</u> |

