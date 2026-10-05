# Re: [PATCH] fbdev: core: Clamp total_size to smem_len in fb_io_read/write

| Field | Value |
| :--- | :--- |
| From | sashiko-bot@kernel.org |
| To | "Author A" <author-a@example.com> |
| Cc | dri-devel@lists.freedesktop.org |
| Date | Mon, 20 Jul 2026 14:15:58 +0000 |
| Message-ID | `20260720141559.0912D1F00A3A@smtp.kernel.org` |
| In-Reply-To | `20260720135534.103599-1-author-a@example.com` |
| List-ID | Direct Rendering Infrastructure - Development <dri-devel.lists.freedesktop.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/20260720141559.0912D1F00A3A@smtp.kernel.org/` |

---

Thank you for your contribution! Sashiko AI review found 3 potential issue(s) to consider:

Pre-existing issues:
- [Critical] Incomplete Fix: Missing `total_size` clamp in `fb_sys_read` and `fb_sys_write` allows Out-Of-Bounds kernel memory access.
- [High] Missing Locking in `fb_io_read` and `fb_io_write` leads to Use-After-Free during concurrent mode switches.
- [High] Out-of-bounds IO Memory Read in `fb_io_read` caused by partial `copy_to_user` failure.
--

--- Patch [1]: [PATCH] fbdev: core: Clamp total_size to smem_len in fb_io_read/write ---
commit 34ebcfca778c16b156ef4c2fa6a81b75c5b5adb4
Author: Author A <author-a@example.com>

fbdev: core: Clamp total_size to smem_len in fb_io_read/write

This patch prevents out-of-bounds I/O accesses in fb_io_read() and
fb_io_write() by clamping total_size to smem_len during mode switches
when legacy drivers incorrectly set screen_size. Virtual framebuffers
remain unaffected.

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

[Severity: Critical]
This is a pre-existing issue, but I noticed that fb_sys_read() and 
fb_sys_write() in drivers/video/fbdev/core/fb_sys_fops.c have the exact 
same trust boundary issue with info->screen_size.

Because those sys functions access kernel system memory rather than I/O 
memory, an oversized screen_size without this clamp could allow userspace 
to perform out-of-bounds reads and writes directly to system RAM. Should 
this fix be applied to fb_sys_read() and fb_sys_write() as well to prevent 
kernel memory corruption?

[ ... ]
> 	if (info->fbops->fb_sync)
> 		info->fbops->fb_sync(info);
> 
> 	while (count) {
> 		c  = (count > PAGE_SIZE) ? PAGE_SIZE : count;
> 		dst = buffer;
> 		fb_memcpy_fromio(dst, src, c);

[Severity: High]
This is a pre-existing issue, but is it safe to access src (which is derived 
from info->screen_base) without holding info->lock during this loop? 

If another thread concurrently issues an ioctl(FBIOPUT_VSCREENINFO) to switch 
modes, and the driver's fb_set_par() implementation unmaps or frees 
screen_base, could this lockless memory access in fb_io_read() and 
fb_io_write() result in a use-after-free or out-of-bounds crash?

> 		dst += c;
> 		src += c;
> 
> 		trailing = copy_to_user(buf, buffer, c);
> 		if (trailing == c) {
> 			err = -EFAULT;
> 			break;
> 		}
> 		c -= trailing;

[Severity: High]
This is also a pre-existing issue, but what happens to the src pointer if 
copy_to_user() partially fails?

If copy_to_user() returns a trailing value between 0 and c, the loop does not 
break, and c is reduced by trailing. However, the src pointer was already 
unconditionally advanced by the original unreduced c. 

In the next loop iteration, src will be misaligned and advanced further than 
accounted for by count. Could this cause subsequent fb_memcpy_fromio() calls 
to read past the end of the hardware framebuffer?

-- 
Sashiko AI review · `[ANONYMIZED] https://sashiko.dev/#/patchset/20260720135534.103599-1-author-a@example.com?part=1 (author-address patchset permalink)`
