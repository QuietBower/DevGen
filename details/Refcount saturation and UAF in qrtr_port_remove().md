# 🐛 Refcount saturation and UAF in qrtr_port_remove()

## 📌 Overview
* **Location:** `net/qrtr/af_qrtr.c`
* **Current Status:**  CVE-2026-52947 ✅ **Patch Accepted**  **HIGH 7.8**
* **Notes:** A race condition in `qrtr_port_remove()` occurs because the socket reference count is decremented before the port is removed from the XArray and before the RCU grace period elapses. This allows a concurrent RCU reader to obtain the socket and attempt to increment a zeroed refcount, leading to saturation and a potential Use-After-Free. Fixed by deferring `sock_put()` until after `xa_erase()` and `synchronize_rcu()` complete.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove | <u>[mail](../email_lore/199_%5BPATCH%5D%20net_%20qrtr_%20fix%20refcount%20saturation%20and%20potential%20UAF%20in%20qrtr_p.md)</u> |
| Re: [PATCH] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove | <u>[mail](../email_lore/530_Re_%20%5BPATCH%5D%20net_%20qrtr_%20fix%20refcount%20saturation%20and%20potential%20UAF%20in%20qr.md)</u> |
| [PATCH] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove | <u>[mail](../email_lore/199_%5BPATCH%5D%20net_%20qrtr_%20fix%20refcount%20saturation%20and%20potential%20UAF%20in%20qrtr_p.md)</u> |
| [PATCH v2] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove | <u>[mail](../email_lore/199_%5BPATCH%5D%20net_%20qrtr_%20fix%20refcount%20saturation%20and%20potential%20UAF%20in%20qrtr_p.md)</u> |
| AI Reviews:sashiko                                           | <u>lore.kernel.org</u> |
| Re: [PATCH v2] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove | <u>[mail](../email_lore/225_Re_%20%5BPATCH%20v2%5D%20net_%20qrtr_%20fix%20refcount%20saturation%20and%20potential%20UAF%20in.md)</u> |
| applied to netdev/net.git | <u>[mail](../email_lore/227_Re_%20%5BPATCH%20v2%5D%20net_%20qrtr_%20fix%20refcount%20saturation%20and%20potential%20UAF%20in.md)</u> |

