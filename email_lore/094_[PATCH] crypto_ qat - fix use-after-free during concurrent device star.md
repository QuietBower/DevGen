# [PATCH] crypto: qat - fix use-after-free during concurrent device start and removal

| Field | Value |
| :--- | :--- |
| From | author-a-alt@example.com |
| To | giovanni.cabiddu@intel.com, herbert@gondor.apana.org.au, davem@davemloft.net |
| Cc | qat-linux@intel.com, linux-crypto@vger.kernel.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Mon, 4 May 2026 10:51:20 +0800 |
| Message-ID | `20260504025120.98242-1-author-a-alt@example.com` |
| List-ID | <linux-crypto.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260504025120.98242-1-author-a-alt@example.com/` |

---

From: Author A <author-a@example.com>

A Use-After-Free (UAF) vulnerability was identified in the QAT driver's ioctl path. When handling commands like IOCTL_START_ACCEL_DEV, `adf_ctl_ioctl_dev_start()` retrieves the acceleration device using `adf_devmgr_get_dev_by_id()`.

Previously, this lookup function iterated over the `accel_table` under the `table_lock`. However, once the target device was found, the lock was dropped and a bare pointer was returned without incrementing the device's reference count.

This creates a critical race condition. If a concurrent thread removes the device (e.g., via device stop operations or PCIe hotplug) by calling `adf_devmgr_rm_dev()`, the device is removed from the list and its memory is subsequently freed. When the original ioctl thread resumes and attempts to acquire `accel_dev->state_lock` inside `adf_dev_up()`, it triggers a KASAN slab-out-of-bounds panic.

Fix this by properly leveraging the existing `ref_count`. Increment the device's `ref_count` via `atomic_inc()` inside `adf_devmgr_get_dev_by_id()` while the `table_lock` is still held. All callers of `adf_devmgr_get_dev_by_id()` are then updated to safely release this reference using `atomic_dec(&accel_dev->ref_count)` once they are done interacting with the device.

Signed-off-by: Author A <author-a@example.com>
---
 drivers/crypto/intel/qat/qat_common/adf_ctl_drv.c | 10 ++++++++++
 drivers/crypto/intel/qat/qat_common/adf_dev_mgr.c | 12 ++++++++++--
 2 files changed, 20 insertions(+), 2 deletions(-)

diff --git a/drivers/crypto/intel/qat/qat_common/adf_ctl_drv.c b/drivers/crypto/intel/qat/qat_common/adf_ctl_drv.c
index c2e6f0cb7480..4924b2bbb412 100644
--- a/drivers/crypto/intel/qat/qat_common/adf_ctl_drv.c
+++ b/drivers/crypto/intel/qat/qat_common/adf_ctl_drv.c
@@ -201,6 +201,9 @@ static int adf_ctl_ioctl_dev_config(struct file *fp, unsigned int cmd,
 	}
 	set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);
 out:
+	/* Release the reference acquired by adf_devmgr_get_dev_by_id() */
+	if (accel_dev)
+		atomic_dec(&accel_dev->ref_count);
 	kfree(ctl_data);
 	return ret;
 }
@@ -310,6 +313,9 @@ static int adf_ctl_ioctl_dev_start(struct file *fp, unsigned int cmd,
 		adf_dev_down(accel_dev);
 	}
 out:
+	/* Release the reference acquired by adf_devmgr_get_dev_by_id() */
+	if (accel_dev)
+		atomic_dec(&accel_dev->ref_count);
 	kfree(ctl_data);
 	return ret;
 }
@@ -360,8 +366,12 @@ static int adf_ctl_ioctl_get_status(struct file *fp, unsigned int cmd,
 	if (copy_to_user((void __user *)arg, &dev_info,
 			 sizeof(struct adf_dev_status_info))) {
 		dev_err(&GET_DEV(accel_dev), "failed to copy status.\n");
+		atomic_dec(&accel_dev->ref_count);
 		return -EFAULT;
 	}
+	
+	/* Release the reference acquired by adf_devmgr_get_dev_by_id() */
+	atomic_dec(&accel_dev->ref_count);
 	return 0;
 }
 
diff --git a/drivers/crypto/intel/qat/qat_common/adf_dev_mgr.c b/drivers/crypto/intel/qat/qat_common/adf_dev_mgr.c
index e050de16ab5d..321bea3cefce 100644
--- a/drivers/crypto/intel/qat/qat_common/adf_dev_mgr.c
+++ b/drivers/crypto/intel/qat/qat_common/adf_dev_mgr.c
@@ -320,6 +320,8 @@ struct adf_accel_dev *adf_devmgr_get_dev_by_id(u32 id)
 		struct adf_accel_dev *ptr =
 				list_entry(itr, struct adf_accel_dev, list);
 		if (ptr->accel_id == id) {
+			/* Increment ref_count to prevent UAF during concurrent removal */
+			atomic_inc(&ptr->ref_count);
 			mutex_unlock(&table_lock);
 			return ptr;
 		}
@@ -331,11 +333,17 @@ struct adf_accel_dev *adf_devmgr_get_dev_by_id(u32 id)
 
 int adf_devmgr_verify_id(u32 id)
 {
+	struct adf_accel_dev *accel_dev;
+	
 	if (id == ADF_CFG_ALL_DEVICES)
 		return 0;
 
-	if (adf_devmgr_get_dev_by_id(id))
-		return 0;
+	accel_dev = adf_devmgr_get_dev_by_id(id);
+	if (accel_dev) {
+		/* Release the reference immediately as we only verify existence */
+		atomic_dec(&accel_dev->ref_count);
+ 		return 0;
+	}
 
 	return -ENODEV;
 }
-- 
2.34.1
