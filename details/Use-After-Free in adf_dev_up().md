# 🐛 Use-After-Free in adf_dev_up()

## 📌 Overview
* **Location:** `drivers/crypto/intel/qat/qat_common/adf_init.c`
* **Current Status:** 👀**Confirmed**
* **Notes:** A KASAN slab-use-after-free occurs in the Intel QAT driver during `adf_dev_up()`. The vulnerability is triggered by opening `/dev/qat_adf_ctl` and issuing an `ioctl` to start the accelerator device with a minimal/invalid configuration. This causes the driver to read a previously freed object (`names_cache`) during mutex acquisition (`__mutex_lock_common`), leading to the UAF.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                             | Link                                                         |
| :------------------------------------------------------ | :----------------------------------------------------------- |
| [BUG] KASAN: slab-use-after-free Read in adf_dev_up | <u>[mail](../email/034_%5BBUG%5D%20KASAN_%20slab-use-after-free%20Read%20in%20adf_dev_up.txt)</u> |
| RE: [BUG] KASAN: slab-use-after-free Read in adf_dev_up | <u>[mail](../email/032_RE_%20%5BBUG%5D%20KASAN_%20slab-use-after-free%20Read%20in%20adf_dev_up.txt)</u> |

