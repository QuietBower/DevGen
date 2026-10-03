# 🐛 Local DoS via printk storm in QAT ioctls

## 📌 Overview
* **Location:** `drivers/crypto/intel/qat/qat_common/adf_ctl_drv.c`
* **Current Status:** ✅ **Accepted**   **CVE-2026-64529**  **HIGH 7.8**
* **Notes:** A malicious user could trigger a printk storm by repeatedly passing invalid pointers or unknown commands to QAT ioctls. This caused RCU stalls and soft lockups (Local DoS) on environments with slow serial consoles. Fixed by removing unconditional error prints in user-copy failure paths.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] crypto: qat - remove noisy error prints in ioctl paths to prevent DoS | <u>lore.kernel.org</u> |
| Giovanni Cabiddu:[PATCH v2 00] crypto qat remove unused ioctl interface | <u>lore.kernel.org</u> |
| Giovanni Cabiddu:[PATCH v2 01] crypto qat remove unused character device and IOCTLs | <u>lore.kernel.org</u> |
| Giovanni Cabiddu:[PATCH v2 02] crypto qat rename adf_ctl_drv.c to adf_module.c | <u>lore.kernel.org</u> |
| Giovanni Cabiddu:[PATCH v3 00] crypto qat remove unused ioctl interface | <u>lore.kernel.org</u> |
| Giovanni Cabiddu:[PATCH v3 01] crypto qat remove unused character device and IOCTLs | <u>lore.kernel.org</u> |
| Giovanni Cabiddu:[PATCH v3 02] crypto qat rename adf_ctl_drv.c to adf_module.c | <u>lore.kernel.org</u> |
| Herbert Xu:Re: [PATCH v3 0/2] crypto: qat - remove unused ioctl interface | <u>lore.kernel.org</u> |

