# 🐛 Stack-out-of-bounds in i801_isr_byte_done()

## 📌 Overview
* **Location:** `drivers/i2c/busses/i2c-i801.c`
* **Current Status:** 🆕 **Bug Reported** 🛠️ **Patch Sent**
* **Notes:** Under extreme conditions (e.g., transaction timeouts), the driver aborts the operation and returns from `i801_access()`, destroying the stack-allocated `union i2c_smbus_data`. However, the `priv->data` pointer is not cleared. If a delayed or spurious interrupt (like `BYTE_DONE`) fires later, `i801_isr_byte_done()` dereferences this dangling pointer, triggering a KASAN stack-out-of-bounds crash. Fixed by clearing `priv->data` in the unified exit path and adding a sanity check in the ISR to ignore interrupts when the pointer is NULL.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] i2c: i801: Fix stack-out-of-bounds in i801_isr_byte_done() | <u>lore.kernel.org</u> |
| sashiko:[PATCH] i2c: i801: Fix stack-out-of-bounds in i801_isr_byte_done() | <u>lore.kernel.org</u> |
| [PATCH v2] i2c: i801: Fix hardware state machine corruption and stack-out-of-bounds | <u>lore.kernel.org</u> |
| [PATCH v3] i2c: i801: Fix hardware state machine corruption and stack-out-of-bounds | <u>lore.kernel.org</u> |
| sashiko:[PATCH v3] i2c: i801: Fix hardware state machine corruption and stack-out-of-bounds | <u>lore.kernel.org</u> |
| Re: [PATCH v3] i2c: i801: Fix hardware state machine corruption and stack-out-of-bounds | <u>lore.kernel.org</u> |

