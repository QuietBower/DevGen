# 🐛 Local DoS via printk storm in QAT ioctls

## 📌 Overview
* **Location:** `drivers/crypto/intel/qat/qat_common/adf_ctl_drv.c`
* **Current Status:** ✅ **Accepted**   **[CVE-2026-64529](https://www.cve.org/CVERecord?id=CVE-2026-64529)**  **HIGH 7.8**
* **Notes:** A malicious user could trigger a printk storm by repeatedly passing invalid pointers or unknown commands to QAT ioctls. This caused RCU stalls and soft lockups (Local DoS) on environments with slow serial consoles. Fixed by removing unconditional error prints in user-copy failure paths.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] crypto: qat - remove noisy error prints in ioctl paths to prevent DoS | <u>[lore.kernel.org](https://lore.kernel.org/all/20260508034841.256794-1-w15303746062@163.com/)</u> |
| Giovanni Cabiddu:[PATCH v2 00] crypto qat remove unused ioctl interface | <u>[lore.kernel.org](../email/%5BPATCH%20v2%2000%5D%20crypto%20qat%20remove%20unused%20ioctl%20interface.txt)</u> |
| Giovanni Cabiddu:[PATCH v2 01] crypto qat remove unused character device and IOCTLs | <u>[lore.kernel.org](../email/%5BPATCH%20v2%2001%5D%20crypto%20qat%20remove%20unused%20character%20device%20and%20IOCTLs.txt)</u> |
| Giovanni Cabiddu:[PATCH v2 02] crypto qat rename adf_ctl_drv.c to adf_module.c | <u>[lore.kernel.org](../email/%5BPATCH%20v2%2002%5D%20crypto%20qat%20rename%20adf_ctl_drv.c%20to%20adf_module.c.txt)</u> |
| Giovanni Cabiddu:[PATCH v3 00] crypto qat remove unused ioctl interface | <u>[lore.kernel.org](https://lore.kernel.org/all/20260511100854.29474-1-giovanni.cabiddu@intel.com/)</u> |
| Giovanni Cabiddu:[PATCH v3 01] crypto qat remove unused character device and IOCTLs | <u>[lore.kernel.org](https://lore.kernel.org/all/20260511100854.29474-2-giovanni.cabiddu@intel.com/)</u> |
| Giovanni Cabiddu:[PATCH v3 02] crypto qat rename adf_ctl_drv.c to adf_module.c | <u>[lore.kernel.org](https://lore.kernel.org/all/20260511100854.29474-3-giovanni.cabiddu@intel.com/)</u> |
| Herbert Xu:Re: [PATCH v3 0/2] crypto: qat - remove unused ioctl interface | <u>[lore.kernel.org](https://lore.kernel.org/all/ahBLnsokkkw9Futh@gondor.apana.org.au/)</u> |

