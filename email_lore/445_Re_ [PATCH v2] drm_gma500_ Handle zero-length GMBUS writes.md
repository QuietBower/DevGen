# Re: [PATCH v2] drm/gma500: Handle zero-length GMBUS writes

| Field | Value |
| :--- | :--- |
| From | sashiko-bot@kernel.org |
| To | "Author G" <author-g@example.com> |
| Cc | dri-devel@lists.freedesktop.org |
| Date | Tue, 21 Jul 2026 13:57:26 +0000 |
| Message-ID | `20260721135726.CFD0C1F00A3D@smtp.kernel.org` |
| List-ID | Direct Rendering Infrastructure - Development <dri-devel.lists.freedesktop.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/20260721135726.CFD0C1F00A3D@smtp.kernel.org/` |

---

Thank you for your contribution! Sashiko AI review found 1 potential issue(s) to consider:

Pre-existing issues:
- [High] Incomplete fix: The patch addresses an out-of-bounds access for zero-length I2C writes but entirely misses the identical bug in the zero-length I2C read path. If a zero-length read message is processed, the unconditional `do-while` loops will underflow `len` and write 65536 bytes out-of-bounds to the `buf` pointer.
--

--- Patch [1]: [PATCH v2] drm/gma500: Handle zero-length GMBUS writes ---
commit 7fd0792ed62f6cc7ed832202067488e2f598937f
Author: Author G <author-g@example.com>

drm/gma500: Handle zero-length GMBUS writes

This commit fixes an out-of-bounds access in the GMBUS write path for
zero-length transfers. It changes a do-while loop to a while loop, preventing
the underflow of the length parameter and subsequent invalid memory access.

> diff --git a/drivers/gpu/drm/gma500/intel_gmbus.c b/drivers/gpu/drm/gma500/intel_gmbus.c
> index 5933ae04a6aa5..d004e2a1feaee 100644
> --- a/drivers/gpu/drm/gma500/intel_gmbus.c
> +++ b/drivers/gpu/drm/gma500/intel_gmbus.c
> @@ -294,9 +294,10 @@ gmbus_xfer(struct i2c_adapter *adapter,

[Severity: High]
This is a pre-existing issue, but does the read path just above this fix
have the exact same vulnerability?

Looking at gmbus_xfer(), if an I2C message with I2C_M_RD is processed and
len is 0, the read path uses an identical unconditional do-while loop:

				val = GMBUS_REG_READ(GMBUS3 + reg_offset);
				do {
					*buf++ = val & 0xff;
					val >>= 8;
				} while (--len && ++loop < 4);
			} while (len);

Would this also underflow len and write 65536 bytes out-of-bounds to the buf
pointer?

>  			u32 val, loop;
>  
>  			val = loop = 0;
> -			do {
> -				val |= *buf++ << (8 * loop);
> -			} while (--len && ++loop < 4);
> +			while (len && loop < 4) {
> +				val |= *buf++ << (8 * loop++);
> +				len -= 1;
> +			}
>  
>  			GMBUS_REG_WRITE(GMBUS3 + reg_offset, val);
>  			GMBUS_REG_WRITE(GMBUS1 + reg_offset,

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20260721134910.844678-1-author-g@example.com?part=1
