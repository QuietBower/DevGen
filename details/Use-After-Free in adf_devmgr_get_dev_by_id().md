# 🐛 Use-After-Free in adf_devmgr_get_dev_by_id()

## 📌 Overview
* **Location:** `drivers/crypto/intel/qat/qat_common/adf_dev_mgr.c`
* **Current Status:** ✅ **Accepted**
* **Notes:** A missing reference count increment in `adf_devmgr_get_dev_by_id()` leads to a Use-After-Free (UAF) vulnerability during concurrent ioctl operations and device removal. Fixed by properly leveraging `atomic_inc()` and `atomic_dec()` for `ref_count`.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] intel_qat: KASAN slab-use-after-free in __mutex_lock from adf_dev_up via IOCTL_START_ACCEL_DEV (syzkaller) | <u>[lore.kernel.org](https://lore.kernel.org/all/1bc08963.a48.19c21a77cf7.Coremail.25181214217@stu.xidian.edu.cn/)</u> |
| [PATCH] crypto: qat - fix use-after-free during concurrent device start and removal | <u>[lore.kernel.org](https://lore.kernel.org/all/20260504025120.98242-1-w15303746062@163.com/)</u> |
| [PATCH] crypto: qat - fix Use-After-Free in adf_ctl_ioctl_dev_start() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260508023542.256299-1-w15303746062@163.com/)</u> |
| Re: [PATCH] crypto: qat - fix use-after-free during concurrent device start and removal | <u>[lore.kernel.org](https://lore.kernel.org/all/af2r0f%2F20watHiCX@gcabiddu-mobl.ger.corp.intel.com/)</u> |
| Giovanni Cabiddu:[PATCH 0/2] crypto: qat - remove unused ioctl interface | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/%5BPATCH%2000%5D%20crypto%20qat%20remove%20unused%20ioctl%20interface.txt)</u> |
| Giovanni Cabiddu:[PATCH 1/2] crypto: qat - remove unused character device and IOCTLs | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/%5BPATCH%2001%5D%20crypto%20qat%20remove%20unused%20character%20device%20and%20IOCTLs.txt)</u> |
| Giovanni Cabiddu:[PATCH 2/2] crypto: qat - rename adf_ctl_drv.c to adf_module.c | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/%5BPATCH%2002%5D%20crypto%20qat%20rename%20adf_ctl_drv.c%20to%20adf_module.c.txt)</u> |

