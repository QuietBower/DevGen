# Re: [PATCH] drm/gma500: Handle zero-length GMBUS writes

| Field | Value |
| :--- | :--- |
| From | sashiko-bot@kernel.org |
| To | "Author G" <author-g@example.com> |
| Cc | dri-devel@lists.freedesktop.org |
| Date | Tue, 21 Jul 2026 13:40:50 +0000 |
| Message-ID | `20260721134050.89BD51F00A3D@smtp.kernel.org` |
| List-ID | Direct Rendering Infrastructure - Development <dri-devel.lists.freedesktop.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/20260721134050.89BD51F00A3D@smtp.kernel.org/` |

---

Thank you for your contribution! Sashiko AI review found 2 potential issue(s) to consider:

New issues:
- [High] The new while loop condition uses a bitwise AND (`&`) instead of a logical AND (`&&`), causing it to evaluate incorrectly and terminate prematurely, completely breaking I2C writes for even lengths.

Pre-existing issues:
- [High] The patch fixes an out-of-bounds access for zero-length I2C writes but leaves the exact same vulnerability unpatched in the I2C read path, causing a massive heap buffer overflow.
--

--- Patch [1]: [PATCH] drm/gma500: Handle zero-length GMBUS writes ---
commit 95b6f28264b9df08009a4c63ada1cc3e62a730c8
Author: Author G <author-g@example.com>

drm/gma500: Handle zero-length GMBUS writes

This commit modifies the GMBUS write path in gmbus_xfer() to handle
zero-length I2C writes. It replaces a do-while loop with a while loop to
ensure the buffer is not accessed if the length is zero.

> diff --git a/drivers/gpu/drm/gma500/intel_gmbus.c b/drivers/gpu/drm/gma500/intel_gmbus.c
> index 5933ae04a6aa5..dce3408a666f2 100644
> --- a/drivers/gpu/drm/gma500/intel_gmbus.c
> +++ b/drivers/gpu/drm/gma500/intel_gmbus.c
> @@ -294,9 +294,10 @@ gmbus_xfer(struct i2c_adapter *adapter,
>  			u32 val, loop;
>  
>  			val = loop = 0;
> -			do {
> -				val |= *buf++ << (8 * loop);
> -			} while (--len && ++loop < 4);
> +			while (len & loop < 4) {

[Severity: High]
Does this condition evaluate correctly? In C, the relational operator <
has higher precedence than the bitwise AND &, so this evaluates as
(len & (loop < 4)).

For an even length, len & 1 is 0, which would cause the loop to exit
immediately without preloading any bytes into the GMBUS data register. Could
this break I2C writes for all even lengths?

[Severity: High]
This is a pre-existing issue, but the I2C read path in gmbus_xfer() appears
to have the exact same vulnerability that this patch fixes in the write path.

Looking at the I2C read path earlier in gmbus_xfer():

drivers/gpu/drm/gma500/intel_gmbus.c:gmbus_xfer() {
    ...
		if (msgs[i].flags & I2C_M_RD) {
			/* ... */
			do {
				/* ... */
				val = GMBUS_REG_READ(GMBUS3 + reg_offset);
				do {
					*buf++ = val & 0xff;
					val >>= 8;
				} while (--len && ++loop < 4);
			} while (len);
    ...
}

Will a zero-length I2C read request also unconditionally dereference buf
and underflow the 16-bit unsigned len to 65535, causing a massive
out-of-bounds write into the buffer memory?

> +				val |= *buf++ << (8 * loop++);
> +				len -= 1;
> +			}
>  
>  			GMBUS_REG_WRITE(GMBUS3 + reg_offset, val);
>  			GMBUS_REG_WRITE(GMBUS1 + reg_offset,

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20260721133200.844629-1-author-g@example.com?part=1
