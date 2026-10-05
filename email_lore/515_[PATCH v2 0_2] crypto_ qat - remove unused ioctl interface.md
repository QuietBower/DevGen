# [PATCH v2 0/2] crypto: qat - remove unused ioctl interface

| Field | Value |
| :--- | :--- |
| From | Giovanni Cabiddu <giovanni.cabiddu@intel.com> |
| To | herbert@gondor.apana.org.au |
| Cc | linux-crypto@vger.kernel.org, qat-linux@intel.com, author-b@example.com, author-c@example.com, author-a-alt@example.com, vdronov@redhat.com, Giovanni Cabiddu <giovanni.cabiddu@intel.com> |
| Date | Sat, 9 May 2026 08:00:11 +0100 |
| Message-ID | `20260509070340.12201-1-giovanni.cabiddu@intel.com` |
| Source | `[ANONYMIZED] local copy (original archive link removed)` |

---

The QAT driver exposes a character device (qat_adf_ctl) with IOCTLs for
device configuration, start, stop, status query and enumeration. These
IOCTLs are not part of any public uAPI header and have no known in-tree
or out-of-tree users.

This ioctl interface increases the attack surface and is the subject of a
number of bug reports. Remove it entirely.

Patch 1 removes the character device, the IOCTL definitions, the related
data structures and headers. It strips adf_ctl_drv.c down to the
minimal module_init/module_exit hooks. This is marked for stable.

Patch 2 renames the now-minimal adf_ctl_drv.c to adf_module.c and
adjusts function names to match the new file name. This is not marked
for stable as it is a pure rename.

Changes since v1:
- Addressed comments from Sashiko: cleaned up leftover dead code
  https://sashiko.dev/#/patchset/20260508091912.206913-1-giovanni.cabiddu%40intel.com

Giovanni Cabiddu (2):
  crypto: qat - remove unused character device and IOCTLs
  crypto: qat - rename adf_ctl_drv.c to adf_module.c

 .../userspace-api/ioctl/ioctl-number.rst      |   1 -
 drivers/crypto/intel/qat/qat_common/Makefile  |   2 +-
 drivers/crypto/intel/qat/qat_common/adf_cfg.c |  10 -
 drivers/crypto/intel/qat/qat_common/adf_cfg.h |   1 -
 .../intel/qat/qat_common/adf_cfg_common.h     |  29 --
 .../intel/qat/qat_common/adf_cfg_user.h       |  38 --
 .../intel/qat/qat_common/adf_common_drv.h     |   2 -
 .../crypto/intel/qat/qat_common/adf_ctl_drv.c | 466 ------------------
 .../crypto/intel/qat/qat_common/adf_dev_mgr.c |  32 --
 .../crypto/intel/qat/qat_common/adf_module.c  |  64 +++
 10 files changed, 65 insertions(+), 580 deletions(-)
 delete mode 100644 drivers/crypto/intel/qat/qat_common/adf_cfg_user.h
 delete mode 100644 drivers/crypto/intel/qat/qat_common/adf_ctl_drv.c
 create mode 100644 drivers/crypto/intel/qat/qat_common/adf_module.c


base-commit: f7dd32c5179d7755de18e21d5674b08f9e5cb180
-- 
2.54.0
