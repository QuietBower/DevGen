# Re: [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions

| Field | Value |
| :--- | :--- |
| From | sashiko-bot@kernel.org |
| To | "Author A" <author-a@example.com> |
| Cc | dri-devel@lists.freedesktop.org |
| Date | Tue, 21 Jul 2026 07:31:09 +0000 |
| Message-ID | `20260721073109.81A121F000E9@smtp.kernel.org` |
| In-Reply-To | `20260721071522.106306-1-author-a@example.com` |
| List-ID | Direct Rendering Infrastructure - Development <dri-devel.lists.freedesktop.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/20260721073109.81A121F000E9@smtp.kernel.org/` |

---

Thank you for your contribution! Sashiko AI review found 2 potential issue(s) to consider:

Pre-existing issues:
- [High] Partial copy to user space in `fb_io_read` causes silent skipping of framebuffer data and loop state desynchronization, leading to out-of-bounds I/O reads.
- [High] Missing locking in fb_io and fb_sys operations allows concurrent mode switches to cause Use-After-Free and Out-Of-Bounds accesses.
--

--- Patch [1]: [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions ---
commit 18031b5da874491bcb50306ae03282ff2763384b
Author: Author A <author-a@example.com>

fbdev: core: Clamp total_size to smem_len in read/write functions

This patch clamps the total_size of framebuffers to info->fix.smem_len in read
and write functions when smem_len is non-zero. This aims to prevent
out-of-bounds accesses for legacy drivers that set screen_size larger than the
actual mapped framebuffer size. Virtual framebuffers remain unaffected.

> diff --git a/drivers/video/fbdev/core/fb_io_fops.c b/drivers/video/fbdev/core/fb_io_fops.c
> index 6ab60fcd00501..335f16d2cc23b 100644
> --- a/drivers/video/fbdev/core/fb_io_fops.c
> +++ b/drivers/video/fbdev/core/fb_io_fops.c
> @@ -24,6 +24,14 @@ ssize_t fb_io_read(struct fb_info *info, char __user *buf, size_t count, loff_t
>  	if (total_size == 0)
>  		total_size = info->fix.smem_len;
>  
> +	/*
> +	 * Security Hardening: Defend against buggy legacy drivers that may
> +	 * calculate a malformed screen_size. Clamp total_size to the actual
> +	 * hardware mapped memory limit (smem_len) to prevent OOB access.
> +	 */
> +	if (info->fix.smem_len && total_size > info->fix.smem_len)
> +		total_size = info->fix.smem_len;
> +

[Severity: High]
This isn't a bug introduced by this patch, but can partial copies to user space
cause out-of-bounds I/O reads later in this function?

Looking at the copy loop further down in fb_io_read():

	while (count) {
		c  = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		dst = buffer;
		fb_memcpy_fromio(dst, src, c);
		dst += c;
		src += c;

		trailing = copy_to_user(buf, buffer, c);
		if (trailing == c) {
			err = -EFAULT;
			break;
		}
		c -= trailing;

		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

The src pointer is eagerly advanced by the full chunk size before the result
of copy_to_user() is known. If a partial copy occurs, trailing is greater
than 0 but less than c. The loop adjusts c -= trailing and count -= c, but
src is not rewound.

On the next iteration, does the read resume from an over-advanced src offset?
Because count is only decremented by the successful bytes, the loop may
execute more iterations than originally bounded by total_size, potentially
causing fb_memcpy_fromio() to read beyond the end of the hardware-mapped
framebuffer.

[ ... ]

> diff --git a/drivers/video/fbdev/core/fb_sys_fops.c b/drivers/video/fbdev/core/fb_sys_fops.c
> index be96b3b3942ed..e97cf02f7c70f 100644
> --- a/drivers/video/fbdev/core/fb_sys_fops.c
> +++ b/drivers/video/fbdev/core/fb_sys_fops.c
> @@ -35,6 +35,14 @@ ssize_t fb_sys_read(struct fb_info *info, char __user *buf, size_t count,
>  	if (total_size == 0)
>  		total_size = info->fix.smem_len;
>  
> +	/*
> +	 * Security Hardening: Defend against buggy legacy drivers that may
> +	 * calculate a malformed screen_size. Clamp total_size to the actual
> +	 * hardware mapped memory limit (smem_len) to prevent OOB access.
> +	 */
> +	if (info->fix.smem_len && total_size > info->fix.smem_len)
> +		total_size = info->fix.smem_len;
> +

[Severity: High]
This is a pre-existing issue, but does this function lack locking to prevent
concurrent mode switches from causing use-after-free or out-of-bounds
accesses?

Bounds checking is performed locklessly on total_size, and later
info->screen_buffer is accessed. If a concurrent user calls
ioctl(FBIOPUT_VSCREENINFO), which takes info->lock and invokes fb_set_var(),
could it free info->screen_buffer via vfree() and allocate a new one while
this read is in progress?

A concurrent read might then either dereference the freed screen_buffer, or
use the newly allocated smaller buffer with the previously fetched, larger
total_size.

-- 
Sashiko AI review · `[ANONYMIZED] https://sashiko.dev/#/patchset/20260721071522.106306-1-author-a@example.com?part=1 (author-address patchset permalink)`
