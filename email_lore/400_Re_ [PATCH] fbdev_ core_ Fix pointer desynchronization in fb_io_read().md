# Re: [PATCH] fbdev: core: Fix pointer desynchronization in fb_io_read()

| Field | Value |
| :--- | :--- |
| From | Helge Deller <deller@gmx.de> |
| To | Author A <author-a@example.com>, simona@ffwll.ch |
| Cc | tzimmermann@suse.de, sam@ravnborg.org, javierm@redhat.com, linux-fbdev@vger.kernel.org, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, stable@vger.kernel.org |
| Date | Sat, 25 Jul 2026 17:03:38 +0200 |
| Message-ID | `86a0387c-b704-4ea5-8117-02b30c4faeb3@gmx.de` |
| In-Reply-To | `20260721081942.107024-1-author-a@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/86a0387c-b704-4ea5-8117-02b30c4faeb3@gmx.de/` |

---

On 7/21/26 10:19, Author A wrote:
> In fb_io_read(), if copy_to_user() performs a partial copy (e.g., due to
> a faulty user buffer), the loop adjusts the chunk size 'c' and updates
> the remaining 'count'. However, the hardware 'src' pointer has already
> been eagerly advanced by the original chunk size.
> 
> If the loop is allowed to continue, the read will resume from an
> incorrect, over-advanced offset. Since the remaining 'count' was only
> decremented by the successful bytes, this desynchronization causes the
> next iterations to execute more hardware reads than originally bounded,
> eventually leading to out-of-bounds I/O reads.
> 
> Fix this by breaking out of the loop immediately upon a partial
> copy_to_user(). A partial copy indicates a faulty user buffer, making
> subsequent read attempts futile. Breaking out ensures we return the
> number of successfully read bytes without risking out-of-bounds hardware
> accesses in subsequent mismatched iterations.
> 
> Fixes: 6121cd9ef911 ("fbdev: Move I/O read and write code into helper functions")
> Cc: stable@vger.kernel.org
> Signed-off-by: Author A <author-a@example.com>
> ---
>   drivers/video/fbdev/core/fb_io_fops.c | 8 ++++++++
>   1 file changed, 8 insertions(+)

Thanks for the patch!
It seems correct, and I've added it to the fbdev git tree.

Did you actually ran into a problem, or how did you find this optimization?

Helge

  
> diff --git a/drivers/video/fbdev/core/fb_io_fops.c b/drivers/video/fbdev/core/fb_io_fops.c
> index 6ab60fcd0050..0798e88799eb 100644
> --- a/drivers/video/fbdev/core/fb_io_fops.c
> +++ b/drivers/video/fbdev/core/fb_io_fops.c
> @@ -61,6 +61,14 @@ ssize_t fb_io_read(struct fb_info *info, char __user *buf, size_t count, loff_t
>   		buf += c;
>   		cnt += c;
>   		count -= c;
> +
> +		/*
> +		 * If there was a partial copy, the user buffer is faulty.
> +		 * Break out to avoid over-advancing the src pointer and
> +		 * reading out of bounds in the next iteration.
> +		 */
> +		if (trailing)
> +			break;
>   	}
>   
>   	kfree(buffer);
