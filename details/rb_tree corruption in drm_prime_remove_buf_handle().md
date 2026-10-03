# 🐛 rb_tree corruption in drm_prime_remove_buf_handle()

## 📌 Overview
* **Location:** `drivers/gpu/drm/drm_prime.c`
* **Current Status:** 👀 **Confirmed** ❌ **WontFix**
* **Notes:** A lack of mutex synchronization in the deletion path of `drm_prime_remove_buf_handle()` causes the `handles` and `dmabufs` rb_trees to become corrupted under concurrent operations. This leads to orphaned members and triggers a kernel panic via a `WARNING` in `drm_prime_destroy_file_private()`. Fixed by holding the `prime_fpriv->lock` during lookup and erasure, while safely deferring the memory cleanup (`dma_buf_put` and `kfree`) outside the lock to avoid deadlocks.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] drm/prime: Fix unsupervised rb_tree corruption in drm_prime_remove_buf_handle | <u>[lore.kernel.org](https://lore.kernel.org/all/20260528082912.1051262-1-w15303746062@163.com/#r)</u> |
| Re: [PATCH] drm/prime: Fix unsupervised rb_tree corruption in drm_prime_remove_buf_handle | <u>[lore.kernel.org](https://lore.kernel.org/all/0e12ce28-f5b7-4ffa-849c-df9ad1796e22@amd.com/)</u> |
| Re:Re: [PATCH] drm/prime: Fix unsupervised rb_tree corruption in drm_prime_remove_buf_handle | <u>[lore.kernel.org](../email/ReRe%20%5BPATCH%5D%20drmprime%20Fix%20unsupervised%20rb_tree%20corruption%20in%20drm_prime_remove_buf_handle.txt)</u> |
| [PATCH v2] drm/prime: fix dangling dmabuf entries after handle release | <u>[lore.kernel.org](https://lore.kernel.org/all/20260528132932.1078483-1-w15303746062@163.com/)</u> |
| Re: [PATCH v2] drm/prime: fix dangling dmabuf entries after handle release | <u>[lore.kernel.org](https://lore.kernel.org/all/62c256eb-1df4-4633-8040-222895b54f97@amd.com/)</u> |
| Re:Re: [PATCH v2] drm/prime: fix dangling dmabuf entries after | <u>[lore.kernel.org](../email/1_ReRe%20%5BPATCH%20v2%5D%20drmprime%20fix%20dangling%20dmabuf%20entries%20after%20handle%20release.txt)</u> |
| Re: [PATCH v2] drm/prime: fix dangling dmabuf entries after handle | <u>[lore.kernel.org](../email/Re%20%5BPATCH%20v2%5D%20drmprime%20fix%20dangling%20dmabuf%20entries%20after%20handle%20release.txt)</u> |
| Re:Re: [PATCH v2] drm/prime: fix dangling dmabuf entries after | <u>[lore.kernel.org](../email/2_ReRe%20%5BPATCH%20v2%5D%20drmprime%20fix%20dangling%20dmabuf%20entries%20after%20handle%20release.txt)</u> |



## ⏸️ Status Update **Confirmed but On Hold:** 

Although kernel maintainers acknowledge the crash is valid **(*"you have certainly stumbled over something"*)**, the current root cause analysis is incomplete, and a reliable reproducer cannot yet be generated. As advised by the maintainers **(*"find the root cause of what is going on here... and then we can look into how to fix that"*)**, patch development is temporarily suspended. The vulnerability is currently tracked as **Confirmed**, pending further analysis to isolate the exact execution path and reproducer before a fix is submitted.

