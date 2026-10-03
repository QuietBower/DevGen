# 🐛 Integer overflow in I2C_TIMEOUT ioctl

## 📌 Overview
* **Location:** `drivers/i2c/i2c-dev.c`
* **Current Status:** [CVE-2026-52948](https://www.cve.org/CVERecord?id=CVE-2026-52948) ✅ **Patch Accepted**   
* **Notes:** A missing bounds check before multiplying the user-provided timeout by 10 causes an integer overflow, leading to a negative timeout value and triggering a local DoS (SMBus state machine corruption).

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] i2c: dev: prevent integer overflow in I2C_TIMEOUT ioctl | <u>[lore.kernel.org](https://lore.kernel.org/all/20260427025745.1100768-1-25181214217@stu.xidian.edu.cn/)</u> |
| [PATCH AUTOSEL 7.0-5.10] i2c: dev: prevent integer overflow in I2C_TIMEOUT ioctl | <u>[lore.kernel.org](https://lore.kernel.org/stable/20260520111944.3424570-21-sashal@kernel.org/)</u> |
| [PATCH AUTOSEL 7.0-5.10] i2c: dev: prevent integer overflow in I2C_TIMEOUT ioctl | <u>[lore.kernel.org](https://lore.kernel.org/stable/20260511221931.2370053-9-sashal@kernel.org/)</u> |
| i2c: dev: prevent integer overflow in I2C_TIMEOUT ioctl      | <u>[lore.kernel.org](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=617eb7c0961a8dfcfc811844a6396e406b2923ea)</u> |
| Re: Please cherry-pick commit 617eb7c0961a                   | <u>[lore.kernel.org](../email/Re%20Please%20cherry-pick%20commit%20617eb7c0961a.txt)</u> |

