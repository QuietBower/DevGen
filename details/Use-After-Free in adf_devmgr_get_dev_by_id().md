# 🐛 Use-After-Free in adf_devmgr_get_dev_by_id()

## 📌 Overview
* **Location:** `drivers/crypto/intel/qat/qat_common/adf_dev_mgr.c`
* **Current Status:** ✅ **Accepted**
* **Notes:** A missing reference count increment in `adf_devmgr_get_dev_by_id()` leads to a Use-After-Free (UAF) vulnerability during concurrent ioctl operations and device removal. Fixed by properly leveraging `atomic_inc()` and `atomic_dec()` for `ref_count`.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] intel_qat: KASAN slab-use-after-free in __mutex_lock from adf_dev_up via IOCTL_START_ACCEL_DEV (syzkaller) | <u>lore.kernel.org</u> |
| [PATCH] crypto: qat - fix use-after-free during concurrent device start and removal | <u>lore.kernel.org</u> |
| [PATCH] crypto: qat - fix Use-After-Free in adf_ctl_ioctl_dev_start() | <u>lore.kernel.org</u> |
| Re: [PATCH] crypto: qat - fix use-after-free during concurrent device start and removal | <u>lore.kernel.org</u> |
| Giovanni Cabiddu:[PATCH 0/2] crypto: qat - remove unused ioctl interface | <u>lore.kernel.org</u> |
| Giovanni Cabiddu:[PATCH 1/2] crypto: qat - remove unused character device and IOCTLs | <u>lore.kernel.org</u> |
| Giovanni Cabiddu:[PATCH 2/2] crypto: qat - rename adf_ctl_drv.c to adf_module.c | <u>lore.kernel.org</u> |

