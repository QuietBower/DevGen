# 🐛 Memory leak in vc_allocate() on screen buffer allocation failure

## 📌 Overview
* **Location:** `drivers/tty/vt/vt.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** When screen buffer allocation fails in vc_allocate(), the error path frees the virtual console structure without releasing its associated unicode screen map. This leaves the unicode dictionary with an elevated reference count and leaks memory when the console is subsequently deallocated

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] tty: vt: fix memory leak in vc_allocate()            | <u>lore.kernel.org</u> |
| Greg KH:Re: [PATCH] tty: vt: fix memory leak in vc_allocate() | <u>lore.kernel.org</u> |
| Mingyu Wang:Re: [PATCH] tty: vt: fix memory leak in vc_allocate() | <u>lore.kernel.org</u> |
| Greg KH:Re: [PATCH] tty: vt: fix memory leak in vc_allocate() | <u>lore.kernel.org</u> |
| [PATCH v2] tty: vt: fix memory leak in vc_allocate()         | <u>lore.kernel.org</u> |
| Greg KH:[PATCH v2] tty: vt: fix memory leak in vc_allocate() | <u>lore.kernel.org</u> |
| Re: [PATCH v2] tty: vt: fix memory leak in vc_allocate()     | <u>lore.kernel.org</u> |
| PATCH v3] tty: vt: fix memory leak in vc_allocate()          | <u>lore.kernel.org</u> |
| patch "tty: vt: fix memory leak in vc_allocate()" added to tty-linus | <u>lore.kernel.org</u> |

