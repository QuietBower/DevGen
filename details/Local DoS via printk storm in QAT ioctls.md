# 🐛 Local DoS via printk storm in QAT ioctls

## 📌 Overview
* **Location:** `drivers/crypto/intel/qat/qat_common/adf_ctl_drv.c`
* **Current Status:** ✅ **Accepted**   **CVE-2026-64529**  **HIGH 7.8**
* **Notes:** A malicious user could trigger a printk storm by repeatedly passing invalid pointers or unknown commands to QAT ioctls. This caused RCU stalls and soft lockups (Local DoS) on environments with slow serial consoles. Fixed by removing unconditional error prints in user-copy failure paths.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] crypto: qat - remove noisy error prints in ioctl paths to prevent DoS | <u>[mail](../email/102_%5BPATCH%5D%20crypto_%20qat%20-%20remove%20noisy%20error%20prints%20in%20ioctl%20paths%20to%20prev.txt)</u> |
| Giovanni Cabiddu:[PATCH v2 00] crypto qat remove unused ioctl interface | <u>[mail](../email/457_%5BPATCH%20v2%200_2%5D%20crypto_%20qat%20-%20remove%20unused%20ioctl%20interface.txt)</u> |
| Giovanni Cabiddu:[PATCH v2 01] crypto qat remove unused character device and IOCTLs | <u>[mail](../email/104_%5BPATCH%20v2%201_2%5D%20crypto_%20qat%20-%20remove%20unused%20character%20device%20and%20IOCTLs.txt)</u> |
| Giovanni Cabiddu:[PATCH v2 02] crypto qat rename adf_ctl_drv.c to adf_module.c | <u>[mail](../email/456_%5BPATCH%20v2%202_2%5D%20crypto_%20qat%20-%20rename%20adf_ctl_drv.c%20to%20adf_module.c.txt)</u> |
| Giovanni Cabiddu:[PATCH v3 00] crypto qat remove unused ioctl interface | <u>[mail](../email/453_%5BPATCH%20v3%200_2%5D%20crypto_%20qat%20-%20remove%20unused%20ioctl%20interface.txt)</u> |
| Giovanni Cabiddu:[PATCH v3 01] crypto qat remove unused character device and IOCTLs | <u>[mail](../email/111_%5BPATCH%20v3%201_2%5D%20crypto_%20qat%20-%20remove%20unused%20character%20device%20and%20IOCTLs.txt)</u> |
| Giovanni Cabiddu:[PATCH v3 02] crypto qat rename adf_ctl_drv.c to adf_module.c | <u>[mail](../email/454_%5BPATCH%20v3%202_2%5D%20crypto_%20qat%20-%20rename%20adf_ctl_drv.c%20to%20adf_module.c.txt)</u> |
| Herbert Xu:Re: [PATCH v3 0/2] crypto: qat - remove unused ioctl interface | <u>[mail](../email/506_Re_%20%5BPATCH%20v3%200_2%5D%20crypto_%20qat%20-%20remove%20unused%20ioctl%20interface.txt)</u> |

