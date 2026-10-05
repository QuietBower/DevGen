# Re: [PATCH v3 0/2] crypto: qat - remove unused ioctl interface

| Field | Value |
| :--- | :--- |
| From | Herbert Xu <herbert@gondor.apana.org.au> |
| To | Giovanni Cabiddu <giovanni.cabiddu@intel.com> |
| Cc | linux-crypto@vger.kernel.org, qat-linux@intel.com, author-b@example.com, author-c@example.com, author-a-alt@example.com, vdronov@redhat.com |
| Date | Fri, 22 May 2026 20:27:10 +0800 |
| Message-ID | `ahBLnsokkkw9Futh@gondor.apana.org.au` |
| List-ID | <linux-crypto.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/ahBLnsokkkw9Futh@gondor.apana.org.au/` |

---

On Mon, May 11, 2026 at 11:04:07AM +0100, Giovanni Cabiddu wrote:
> The QAT driver exposes a character device (qat_adf_ctl) with IOCTLs for
> device configuration, start, stop, status query and enumeration. These
> IOCTLs are not part of any public uAPI header and have no known in-tree
> or out-of-tree users.
> 
> This ioctl interface increases the attack surface and is the subject of a
> number of bug reports. Remove it entirely.
> 
> Patch 1 removes the character device, the IOCTL definitions, the related
> data structures and headers. It strips adf_ctl_drv.c down to the
> minimal module_init/module_exit hooks. This is marked for stable.
> 
> Patch 2 renames the now-minimal adf_ctl_drv.c to adf_module.c and
> adjusts function names to match the new file name. This is not marked
> for stable as it is a pure rename.
> 
> Changes since v1:
> - Addressed comments from Sashiko: cleaned up leftover dead code
>   https://sashiko.dev/#/patchset/20260508091912.206913-1-giovanni.cabiddu%40intel.com
> 
> Changes since v2:
> - Removed additional dead code: adf_devmgr_get_dev_by_id(),
>   adf_get_vf_real_id() and a few ADF_CFG unused macros
> 
> Giovanni Cabiddu (2):
>   crypto: qat - remove unused character device and IOCTLs
>   crypto: qat - rename adf_ctl_drv.c to adf_module.c
> 
>  .../userspace-api/ioctl/ioctl-number.rst      |   1 -
>  drivers/crypto/intel/qat/qat_common/Makefile  |   2 +-
>  drivers/crypto/intel/qat/qat_common/adf_cfg.c |  10 -
>  drivers/crypto/intel/qat/qat_common/adf_cfg.h |   1 -
>  .../intel/qat/qat_common/adf_cfg_common.h     |  32 --
>  .../intel/qat/qat_common/adf_cfg_user.h       |  38 --
>  .../intel/qat/qat_common/adf_common_drv.h     |   3 -
>  .../crypto/intel/qat/qat_common/adf_ctl_drv.c | 466 ------------------
>  .../crypto/intel/qat/qat_common/adf_dev_mgr.c |  70 ---
>  .../crypto/intel/qat/qat_common/adf_module.c  |  64 +++
>  10 files changed, 65 insertions(+), 622 deletions(-)
>  delete mode 100644 drivers/crypto/intel/qat/qat_common/adf_cfg_user.h
>  delete mode 100644 drivers/crypto/intel/qat/qat_common/adf_ctl_drv.c
>  create mode 100644 drivers/crypto/intel/qat/qat_common/adf_module.c
> 
> 
> base-commit: f7dd32c5179d7755de18e21d5674b08f9e5cb180
> -- 
> 2.54.0

All applied.  Thanks.
-- 
Email: Herbert Xu <herbert@gondor.apana.org.au>
Home Page: http://gondor.apana.org.au/~herbert/
PGP Key: http://gondor.apana.org.au/~herbert/pubkey.txt
