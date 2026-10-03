# 🐛 rb_tree corruption in drm_prime_remove_buf_handle()

## 📌 Overview
* **Location:** `drivers/gpu/drm/drm_prime.c`
* **Current Status:** 👀 **Confirmed** ❌ **WontFix**
* **Notes:** A lack of mutex synchronization in the deletion path of `drm_prime_remove_buf_handle()` causes the `handles` and `dmabufs` rb_trees to become corrupted under concurrent operations. This leads to orphaned members and triggers a kernel panic via a `WARNING` in `drm_prime_destroy_file_private()`. Fixed by holding the `prime_fpriv->lock` during lookup and erasure, while safely deferring the memory cleanup (`dma_buf_put` and `kfree`) outside the lock to avoid deadlocks.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] drm/prime: Fix unsupervised rb_tree corruption in drm_prime_remove_buf_handle | <u>lore.kernel.org</u> |
| Re: [PATCH] drm/prime: Fix unsupervised rb_tree corruption in drm_prime_remove_buf_handle | <u>lore.kernel.org</u> |
| Re:Re: [PATCH] drm/prime: Fix unsupervised rb_tree corruption in drm_prime_remove_buf_handle | <u>lore.kernel.org</u> |
| [PATCH v2] drm/prime: fix dangling dmabuf entries after handle release | <u>lore.kernel.org</u> |
| Re: [PATCH v2] drm/prime: fix dangling dmabuf entries after handle release | <u>lore.kernel.org</u> |
| Re:Re: [PATCH v2] drm/prime: fix dangling dmabuf entries after | <u>lore.kernel.org</u> |
| Re: [PATCH v2] drm/prime: fix dangling dmabuf entries after handle | <u>lore.kernel.org</u> |
| Re:Re: [PATCH v2] drm/prime: fix dangling dmabuf entries after | <u>lore.kernel.org</u> |



## ⏸️ Status Update **Confirmed but On Hold:** 

Although kernel maintainers acknowledge the crash is valid **(*"you have certainly stumbled over something"*)**, the current root cause analysis is incomplete, and a reliable reproducer cannot yet be generated. As advised by the maintainers **(*"find the root cause of what is going on here... and then we can look into how to fix that"*)**, patch development is temporarily suspended. The vulnerability is currently tracked as **Confirmed**, pending further analysis to isolate the exact execution path and reproducer before a fix is submitted.

