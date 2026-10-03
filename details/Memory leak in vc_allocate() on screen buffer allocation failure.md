# 🐛 Memory leak in vc_allocate() on screen buffer allocation failure

## 📌 Overview
* **Location:** `drivers/tty/vt/vt.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** When screen buffer allocation fails in vc_allocate(), the error path frees the virtual console structure without releasing its associated unicode screen map. This leaves the unicode dictionary with an elevated reference count and leaks memory when the console is subsequently deallocated

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] tty: vt: fix memory leak in vc_allocate()            | <u>[lore.kernel.org](https://lore.kernel.org/all/20260723070530.117552-1-25181214217@stu.xidian.edu.cn/)</u> |
| Greg KH:Re: [PATCH] tty: vt: fix memory leak in vc_allocate() | <u>[lore.kernel.org](https://lore.kernel.org/all/2026072345-copartner-shrink-9596@gregkh/)</u> |
| Mingyu Wang:Re: [PATCH] tty: vt: fix memory leak in vc_allocate() | <u>[lore.kernel.org](https://lore.kernel.org/stable/88f9ac58-8079-415d-902c-3b9455a9d17c@stu.xidian.edu.cn/)</u> |
| Greg KH:Re: [PATCH] tty: vt: fix memory leak in vc_allocate() | <u>[lore.kernel.org](https://lore.kernel.org/stable/2026073028-icing-dallying-9229@gregkh/)</u> |
| [PATCH v2] tty: vt: fix memory leak in vc_allocate()         | <u>[lore.kernel.org](https://lore.kernel.org/stable/20260731024550.158801-1-25181214217@stu.xidian.edu.cn/)</u> |
| Greg KH:[PATCH v2] tty: vt: fix memory leak in vc_allocate() | <u>[lore.kernel.org](https://lore.kernel.org/stable/2026080339-proofing-catsup-bee9@gregkh/)</u> |
| Re: [PATCH v2] tty: vt: fix memory leak in vc_allocate()     | <u>[lore.kernel.org](https://lore.kernel.org/stable/514ebe5c-4867-47e8-9500-0b0cab9eb243@stu.xidian.edu.cn/)</u> |
| PATCH v3] tty: vt: fix memory leak in vc_allocate()          | <u>[lore.kernel.org](https://lore.kernel.org/stable/20260803144556.163856-1-25181214217@stu.xidian.edu.cn/)</u> |
| patch "tty: vt: fix memory leak in vc_allocate()" added to tty-linus | <u>[lore.kernel.org](../email/patch%20tty%20vt%20fix%20memory%20leak%20in%20vc_allocate%28%29%20added%20to%20tty-linus.txt)</u> |

