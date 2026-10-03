# 🐛 Hung Task panics via malicious I2C_TIMEOUT ioctl

## 📌 Overview
* **Location:** `drivers/i2c/busses/i2c-i801.c`
* **Current Status:**  🛡️**Crash**  ❌ **WontFix**   
* **Notes:** Userspace applications can inject an arbitrarily large timeout value via the I2C_TIMEOUT ioctl. If the hardware fails to respond, the i2c-i801 driver blocks for the entirety of this requested timeout while holding the i2c adapter lock. This starves other processes in TASK_UNINTERRUPTIBLE sleep, ultimately triggering Hung Task panics and system lockups.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/20260723025157.115897-1-25181214217@stu.xidian.edu.cn/)</u> |
| [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/20260723034331.116690-1-25181214217@stu.xidian.edu.cn/)</u> |
| AI-Result                                                    | <u>[lore.kernel.org]([Sashiko](https://sashiko.dev/#/patchset/20260723034331.116690-1-25181214217%40stu.xidian.edu.cn))</u> |
| Andi Shyti：Re: [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/amPinh8c7GwQx4Le@zenone.zhora.eu/)</u> |
| Mingyu Wang：Re: [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/0fbbc160-a599-4384-b78a-bf8e6afd963e@stu.xidian.edu.cn/)</u> |
| Andi Shyti:Re: [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/amYXXbrh3lIIXcJ0@zenone.zhora.eu/)</u> |

