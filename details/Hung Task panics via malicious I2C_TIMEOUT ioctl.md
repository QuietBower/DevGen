# 🐛 Hung Task panics via malicious I2C_TIMEOUT ioctl

## 📌 Overview
* **Location:** `drivers/i2c/busses/i2c-i801.c`
* **Current Status:**  🛡️**Crash**  ❌ **WontFix**   
* **Notes:** Userspace applications can inject an arbitrarily large timeout value via the I2C_TIMEOUT ioctl. If the hardware fails to respond, the i2c-i801 driver blocks for the entirety of this requested timeout while holding the i2c adapter lock. This starves other processes in TASK_UNINTERRUPTIBLE sleep, ultimately triggering Hung Task panics and system lockups.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>lore.kernel.org</u> |
| [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>lore.kernel.org</u> |
| AI-Result                                                    | <u>lore.kernel.org)</u> |
| Andi Shyti：Re: [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>lore.kernel.org</u> |
| Mingyu Wang：Re: [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>lore.kernel.org</u> |
| Andi Shyti:Re: [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>lore.kernel.org</u> |

