# 🐛 Use-After-Free in adf_devmgr_get_dev_by_id()

## 📌 Overview
* **Location:** `drivers/crypto/intel/qat/qat_common/adf_dev_mgr.c`
* **Current Status:** ✅ **Accepted**
* **Notes:** A missing reference count increment in `adf_devmgr_get_dev_by_id()` leads to a Use-After-Free (UAF) vulnerability during concurrent ioctl operations and device removal. Fixed by properly leveraging `atomic_inc()` and `atomic_dec()` for `ref_count`.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] intel_qat: KASAN slab-use-after-free in __mutex_lock from adf_dev_up via IOCTL_START_ACCEL_DEV (syzkaller) | <u>[mail](../email_lore/039_%5BBUG%5D%20intel_qat_%20KASAN%20slab-use-after-free%20in%20__mutex_lock%20from%20adf_de.md)</u> |
| [PATCH] crypto: qat - fix use-after-free during concurrent device start and removal | <u>[mail](../email_lore/094_%5BPATCH%5D%20crypto_%20qat%20-%20fix%20use-after-free%20during%20concurrent%20device%20star.md)</u> |
| [PATCH] crypto: qat - fix Use-After-Free in adf_ctl_ioctl_dev_start() | <u>[mail](../email_lore/101_%5BPATCH%5D%20crypto_%20qat%20-%20fix%20Use-After-Free%20in%20adf_ctl_ioctl_dev_start%28%29.md)</u> |
| Re: [PATCH] crypto: qat - fix use-after-free during concurrent device start and removal | <u>[mail](../email_lore/100_Re_%20%5BPATCH%5D%20crypto_%20qat%20-%20fix%20use-after-free%20during%20concurrent%20device%20.md)</u> |
| Giovanni Cabiddu:[PATCH 0/2] crypto: qat - remove unused ioctl interface | <u>[mail](../email_lore/507_%5BPATCH%200_2%5D%20crypto_%20qat%20-%20remove%20unused%20ioctl%20interface.md)</u> |
| Giovanni Cabiddu:[PATCH 1/2] crypto: qat - remove unused character device and IOCTLs | <u>[mail](../email_lore/508_%5BPATCH%201_2%5D%20crypto_%20qat%20-%20remove%20unused%20character%20device%20and%20IOCTLs.md)</u> |
| Giovanni Cabiddu:[PATCH 2/2] crypto: qat - rename adf_ctl_drv.c to adf_module.c | <u>[mail](../email_lore/509_%5BPATCH%202_2%5D%20crypto_%20qat%20-%20rename%20adf_ctl_drv.c%20to%20adf_module.c.md)</u> |

