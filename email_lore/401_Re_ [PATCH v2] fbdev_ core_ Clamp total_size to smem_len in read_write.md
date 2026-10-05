# Re: [PATCH v2] fbdev: core: Clamp total_size to smem_len in read/write functions

| Field | Value |
| :--- | :--- |
| From | Helge Deller <deller@gmx.de> |
| To | Author A <author-a@example.com>, simona@ffwll.ch |
| Cc | linux-fbdev@vger.kernel.org, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org |
| Date | Sat, 25 Jul 2026 17:29:58 +0200 |
| Message-ID | `bb8a8d13-07fb-4ee9-aed2-172c86cf3ac0@gmx.de` |
| In-Reply-To | `20260721071522.106306-1-author-a@example.com` |
| List-ID | <linux-kernel.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/bb8a8d13-07fb-4ee9-aed2-172c86cf3ac0@gmx.de/` |

---

On 7/21/26 09:15, Author A wrote:
> Some legacy fbdev drivers may incorrectly set info->screen_size to a
> value larger than the actual mapped framebuffer size (info->fix.smem_len)
> during mode switches. This could allow out-of-bounds I/O and system
> memory accesses in fb_io_read(), fb_io_write(), fb_sys_read(), and
> fb_sys_write().
> 
> Prevent this by clamping total_size to smem_len when smem_len is non-zero.
> Virtual framebuffers (smem_len == 0) are unaffected.
> 
> This is a hardening measure; no specific crash is fixed by this patch.
> 
> Signed-off-by: Author A <author-a@example.com>
> ---
> v2:
>   - Apply the clamp to fb_sys_read() and fb_sys_write() as well to prevent
>     similar OOB accesses in system memory framebuffers (Reported by Sashiko).
>   - Update commit message to clearly state this is a hardening measure.
> ---
>   drivers/video/fbdev/core/fb_io_fops.c  | 16 ++++++++++++++++
>   drivers/video/fbdev/core/fb_sys_fops.c | 16 ++++++++++++++++
>   2 files changed, 32 insertions(+)

Sounds reasonable.
I've added it to fbdev git tree for wider testing.

Thanks!
Helge


  
> diff --git a/drivers/video/fbdev/core/fb_io_fops.c b/drivers/video/fbdev/core/fb_io_fops.c
> index 6ab60fcd0050..335f16d2cc23 100644
> --- a/drivers/video/fbdev/core/fb_io_fops.c
> +++ b/drivers/video/fbdev/core/fb_io_fops.c
> @@ -24,6 +24,14 @@ ssize_t fb_io_read(struct fb_info *info, char __user *buf, size_t count, loff_t
>   	if (total_size == 0)
>   		total_size = info->fix.smem_len;
>   
> +	/*
> +	 * Security Hardening: Defend against buggy legacy drivers that may
> +	 * calculate a malformed screen_size. Clamp total_size to the actual
> +	 * hardware mapped memory limit (smem_len) to prevent OOB access.
> +	 */
> +	if (info->fix.smem_len && total_size > info->fix.smem_len)
> +		total_size = info->fix.smem_len;
> +
>   	if (p >= total_size)
>   		return 0;
>   
> @@ -88,6 +96,14 @@ ssize_t fb_io_write(struct fb_info *info, const char __user *buf, size_t count,
>   	if (total_size == 0)
>   		total_size = info->fix.smem_len;
>   
> +	/*
> +	 * Security Hardening: Defend against buggy legacy drivers that may
> +	 * calculate a malformed screen_size. Clamp total_size to the actual
> +	 * hardware mapped memory limit (smem_len) to prevent OOB access.
> +	 */
> +	if (info->fix.smem_len && total_size > info->fix.smem_len)
> +		total_size = info->fix.smem_len;
> +
>   	if (p > total_size)
>   		return -EFBIG;
>   
> diff --git a/drivers/video/fbdev/core/fb_sys_fops.c b/drivers/video/fbdev/core/fb_sys_fops.c
> index be96b3b3942e..e97cf02f7c70 100644
> --- a/drivers/video/fbdev/core/fb_sys_fops.c
> +++ b/drivers/video/fbdev/core/fb_sys_fops.c
> @@ -35,6 +35,14 @@ ssize_t fb_sys_read(struct fb_info *info, char __user *buf, size_t count,
>   	if (total_size == 0)
>   		total_size = info->fix.smem_len;
>   
> +	/*
> +	 * Security Hardening: Defend against buggy legacy drivers that may
> +	 * calculate a malformed screen_size. Clamp total_size to the actual
> +	 * hardware mapped memory limit (smem_len) to prevent OOB access.
> +	 */
> +	if (info->fix.smem_len && total_size > info->fix.smem_len)
> +		total_size = info->fix.smem_len;
> +
>   	if (p >= total_size)
>   		return 0;
>   
> @@ -80,6 +88,14 @@ ssize_t fb_sys_write(struct fb_info *info, const char __user *buf,
>   	if (total_size == 0)
>   		total_size = info->fix.smem_len;
>   
> +	/*
> +	 * Security Hardening: Defend against buggy legacy drivers that may
> +	 * calculate a malformed screen_size. Clamp total_size to the actual
> +	 * hardware mapped memory limit (smem_len) to prevent OOB access.
> +	 */
> +	if (info->fix.smem_len && total_size > info->fix.smem_len)
> +		total_size = info->fix.smem_len;
> +
>   	if (p > total_size)
>   		return -EFBIG;
>
