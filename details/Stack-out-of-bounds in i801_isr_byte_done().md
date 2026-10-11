# 🐛 Stack-out-of-bounds in i801_isr_byte_done()

## 📌 Overview
* **Location:** `drivers/i2c/busses/i2c-i801.c`
* **Current Status:** 🆕 **Bug Reported** 🛠️ **Patch Sent**
* **Notes:** Under extreme conditions (e.g., transaction timeouts), the driver aborts the operation and returns from `i801_access()`, destroying the stack-allocated `union i2c_smbus_data`. However, the `priv->data` pointer is not cleared. If a delayed or spurious interrupt (like `BYTE_DONE`) fires later, `i801_isr_byte_done()` dereferences this dangling pointer, triggering a KASAN stack-out-of-bounds crash. Fixed by clearing `priv->data` in the unified exit path and adding a sanity check in the ISR to ignore interrupts when the pointer is NULL.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] i2c: i801: Fix stack-out-of-bounds in i801_isr_byte_done() | <u>[mail](../email/305_%5BPATCH%5D%20i2c_%20i801_%20Fix%20stack-out-of-bounds%20in%20i801_isr_byte_done%28%29.txt)</u> |
| sashiko:[PATCH] i2c: i801: Fix stack-out-of-bounds in i801_isr_byte_done() | <u>[mail](../email/305_%5BPATCH%5D%20i2c_%20i801_%20Fix%20stack-out-of-bounds%20in%20i801_isr_byte_done%28%29.txt)</u> |
| [PATCH v2] i2c: i801: Fix hardware state machine corruption and stack-out-of-bounds | <u>[mail](../email/306_%5BPATCH%20v2%5D%20i2c_%20i801_%20Fix%20hardware%20state%20machine%20corruption%20and%20stack-.txt)</u> |
| [PATCH v3] i2c: i801: Fix hardware state machine corruption and stack-out-of-bounds | <u>[mail](../email/307_%5BPATCH%20v3%5D%20i2c_%20i801_%20Fix%20hardware%20state%20machine%20corruption%20and%20stack-.txt)</u> |
| sashiko:[PATCH v3] i2c: i801: Fix hardware state machine corruption and stack-out-of-bounds | <u>[mail](../email/307_%5BPATCH%20v3%5D%20i2c_%20i801_%20Fix%20hardware%20state%20machine%20corruption%20and%20stack-.txt)</u> |
| Re: [PATCH v3] i2c: i801: Fix hardware state machine corruption and stack-out-of-bounds | <u>[mail](../email/424_Re_%20%5BPATCH%20v3%5D%20i2c_%20i801_%20Fix%20hardware%20state%20machine%20corruption%20and%20st.txt)</u> |

