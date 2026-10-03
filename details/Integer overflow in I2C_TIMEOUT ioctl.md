# 🐛 Integer overflow in I2C_TIMEOUT ioctl

## 📌 Overview
* **Location:** `drivers/i2c/i2c-dev.c`
* **Current Status:** CVE-2026-52948 ✅ **Patch Accepted**   
* **Notes:** A missing bounds check before multiplying the user-provided timeout by 10 causes an integer overflow, leading to a negative timeout value and triggering a local DoS (SMBus state machine corruption).

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] i2c: dev: prevent integer overflow in I2C_TIMEOUT ioctl | <u>lore.kernel.org</u> |
| [PATCH AUTOSEL 7.0-5.10] i2c: dev: prevent integer overflow in I2C_TIMEOUT ioctl | <u>lore.kernel.org</u> |
| [PATCH AUTOSEL 7.0-5.10] i2c: dev: prevent integer overflow in I2C_TIMEOUT ioctl | <u>lore.kernel.org</u> |
| i2c: dev: prevent integer overflow in I2C_TIMEOUT ioctl      | <u>lore.kernel.org</u> |
| Re: Please cherry-pick commit 617eb7c0961a                   | <u>lore.kernel.org</u> |

