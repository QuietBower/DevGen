# 🐛 Hung Task panics via malicious I2C_TIMEOUT ioctl

## 📌 Overview
* **Location:** `drivers/i2c/busses/i2c-i801.c`
* **Current Status:**  🛡️**Crash**  ❌ **WontFix**   
* **Notes:** Userspace applications can inject an arbitrarily large timeout value via the I2C_TIMEOUT ioctl. If the hardware fails to respond, the i2c-i801 driver blocks for the entirety of this requested timeout while holding the i2c adapter lock. This starves other processes in TASK_UNINTERRUPTIBLE sleep, ultimately triggering Hung Task panics and system lockups.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>[mail](../email/394_%5BPATCH%5D%20i2c_%20i801_%20Clamp%20adapter%20timeout%20to%20prevent%20system%20lockup.txt)</u> |
| [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>[mail](../email/395_%5BPATCH%20v2%5D%20i2c_%20i801_%20Clamp%20adapter%20timeout%20to%20prevent%20system%20lockup.txt)</u> |
| AI-Result                                                    | <u>[mail](../email/481_%5BPATCH%20v2%5D%20i2c_%20i801_%20Clamp%20adapter%20timeout%20to%20prevent%20system%20lockup.txt)</u> |
| Andi Shyti：Re: [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>[mail](../email/398_Re_%20%5BPATCH%20v2%5D%20i2c_%20i801_%20Clamp%20adapter%20timeout%20to%20prevent%20system%20lock.txt)</u> |
| Author A：Re: [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>[mail](../email/399_Re_%20%5BPATCH%20v2%5D%20i2c_%20i801_%20Clamp%20adapter%20timeout%20to%20prevent%20system%20lock.txt)</u> |
| Andi Shyti:Re: [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>[mail](../email/507_Re_%20%5BPATCH%20v2%5D%20i2c_%20i801_%20Clamp%20adapter%20timeout%20to%20prevent%20system%20lock.txt)</u> |

