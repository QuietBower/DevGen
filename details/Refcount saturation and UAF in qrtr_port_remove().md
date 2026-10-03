# 🐛 Refcount saturation and UAF in qrtr_port_remove()

## 📌 Overview
* **Location:** `net/qrtr/af_qrtr.c`
* **Current Status:**  CVE-2026-52947 ✅ **Patch Accepted**  **HIGH 7.8**
* **Notes:** A race condition in `qrtr_port_remove()` occurs because the socket reference count is decremented before the port is removed from the XArray and before the RCU grace period elapses. This allows a concurrent RCU reader to obtain the socket and attempt to increment a zeroed refcount, leading to saturation and a potential Use-After-Free. Fixed by deferring `sock_put()` until after `xa_erase()` and `synchronize_rcu()` complete.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove | <u>lore.kernel.org</u> |
| Re: [PATCH] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove | <u>lore.kernel.org</u> |
| [PATCH] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove | <u>lore.kernel.org</u> |
| [PATCH v2] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove | <u>lore.kernel.org</u> |
| AI Reviews:sashiko                                           | <u>lore.kernel.org</u> |
| Re: [PATCH v2] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove | <u>lore.kernel.org</u> |
| applied to netdev/net.git                                    | <u>lore.kernel.org</u> |

