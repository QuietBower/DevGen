# 🐛 Memory leak in vc_allocate() on screen buffer allocation failure

## 📌 Overview
* **Location:** `drivers/tty/vt/vt.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** When screen buffer allocation fails in vc_allocate(), the error path frees the virtual console structure without releasing its associated unicode screen map. This leaves the unicode dictionary with an elevated reference count and leaks memory when the console is subsequently deallocated

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] tty: vt: fix memory leak in vc_allocate() | <u>[mail](../email/396_%5BPATCH%5D%20tty_%20vt_%20fix%20memory%20leak%20in%20vc_allocate%28%29.txt)</u> |
| Greg KH:Re: [PATCH] tty: vt: fix memory leak in vc_allocate() | <u>[mail](../email_lore/393_Re_%20%5BPATCH%5D%20tty_%20vt_%20fix%20memory%20leak%20in%20vc_allocate%28%29.md)</u> |
| Author A:Re: [PATCH] tty: vt: fix memory leak in vc_allocate() | <u>[mail](../email/393_Re_%20%5BPATCH%5D%20tty_%20vt_%20fix%20memory%20leak%20in%20vc_allocate%28%29.txt)</u> |
| Greg KH:Re: [PATCH] tty: vt: fix memory leak in vc_allocate() | <u>[mail](../email_lore/393_Re_%20%5BPATCH%5D%20tty_%20vt_%20fix%20memory%20leak%20in%20vc_allocate%28%29.md)</u> |
| [PATCH v2] tty: vt: fix memory leak in vc_allocate() | <u>[mail](../email/412_%5BPATCH%20v2%5D%20tty_%20vt_%20fix%20memory%20leak%20in%20vc_allocate%28%29.txt)</u> |
| Greg KH:[PATCH v2] tty: vt: fix memory leak in vc_allocate() | <u>[mail](../email/419_Re_%20%5BPATCH%20v2%5D%20tty_%20vt_%20fix%20memory%20leak%20in%20vc_allocate%28%29.txt)</u> |
| Re: [PATCH v2] tty: vt: fix memory leak in vc_allocate() | <u>[mail](../email/420_Re_%20%5BPATCH%20v2%5D%20tty_%20vt_%20fix%20memory%20leak%20in%20vc_allocate%28%29.txt)</u> |
| PATCH v3] tty: vt: fix memory leak in vc_allocate()          | <u>lore.kernel.org</u> |
| patch "tty: vt: fix memory leak in vc_allocate()" added to tty-linus | <u>[mail](../email/421_%5BPATCH%20v3%5D%20tty_%20vt_%20fix%20memory%20leak%20in%20vc_allocate%28%29.txt)</u> |

