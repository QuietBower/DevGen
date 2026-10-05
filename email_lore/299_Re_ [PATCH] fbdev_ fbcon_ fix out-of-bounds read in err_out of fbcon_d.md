# Re: [PATCH] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font()

| Field | Value |
| :--- | :--- |
| From | Thomas Zimmermann <tzimmermann@suse.de> |
| To | author-a-alt@example.com, deller@gmx.de, simona@ffwll.ch |
| Cc | syoshida@redhat.com, dri-devel@lists.freedesktop.org, linux-fbdev@vger.kernel.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com>, stable@vger.kernel.org |
| Date | Thu, 25 Jun 2026 17:04:50 +0200 |
| Message-ID | `7dd87e55-5170-4317-8c9e-38a7868f68fc@suse.de` |
| In-Reply-To | `20260624083316.389677-1-author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/7dd87e55-5170-4317-8c9e-38a7868f68fc@suse.de/` |

---

Hi

Am 24.06.26 um 10:33 schrieb author-a-alt@example.com:
> From: Author A <author-a@example.com>
>
> When fbcon_do_set_font() fails (e.g., due to a memory allocation failure
> inside vc_resize() under heavy memory pressure), it jumps to the `err_out`
> label to roll back the console state. However, the current rollback logic
> forgets to restore the `hi_font` state, leading to a severe state machine
> corruption.
>
> Earlier in the function, `set_vc_hi_font()` might be called to change
> `vc->vc_hi_font_mask` and mutate the screen buffer. If `vc_resize()`
> subsequently fails, the `err_out` path restores `vc_font.charcount`
> but entirely skips rolling back the `vc_hi_font_mask` and the screen
> buffer.
>
> This mismatch leaves the terminal in a desynchronized state. Because
> `vc_hi_font_mask` remains set, the VT subsystem will still accept
> character indices greater than 255 from userspace and write them to the
> screen buffer. Subsequent rendering calls (e.g., `fbcon_putcs()`) will
> then use these inflated indices to access the reverted, 256-character
> font array, leading to a deterministic out-of-bounds read and potential
> kernel memory disclosure.
>
> Fix this by adding the missing rollback logic for the `hi_font` mask
> and screen buffer in the error path.
>
> Fixes: a5a923038d70 ("fbdev: fbcon: Properly revert changes when vc_resize() failed")
> Cc: stable@vger.kernel.org
> Signed-off-by: Author A <author-a@example.com>

The email in your S-o-b tag differs from the one in the mail's From: 
line. I think this is not accepted in the kernel. Can you please 
resubmit with the email addresses synchronized.

Apart from that:

Reviewed-by: Thomas Zimmermann <tzimmermann@suse.de>

Thanks for fixing this problem.

Best regards
Thomas

> ---
>   drivers/video/fbdev/core/fbcon.c | 7 +++++++
>   1 file changed, 7 insertions(+)
>
> diff --git a/drivers/video/fbdev/core/fbcon.c b/drivers/video/fbdev/core/fbcon.c
> index 9077d3b99357..5880ab9f3cde 100644
> --- a/drivers/video/fbdev/core/fbcon.c
> +++ b/drivers/video/fbdev/core/fbcon.c
> @@ -2405,6 +2405,7 @@ static int fbcon_do_set_font(struct vc_data *vc, int w, int h, int charcount,
>   	int resize, ret, old_width, old_height, old_charcount;
>   	font_data_t *old_fontdata = p->fontdata;
>   	const u8 *old_data = vc->vc_font.data;
> +	int old_hi_font_mask = vc->vc_hi_font_mask;
>   
>   	font_data_get(data);
>   
> @@ -2451,6 +2452,12 @@ static int fbcon_do_set_font(struct vc_data *vc, int w, int h, int charcount,
>   	vc->vc_font.height = old_height;
>   	vc->vc_font.charcount = old_charcount;
>   
> +	/* Restore the hi_font state and screen buffer */
> +	if (old_hi_font_mask && !vc->vc_hi_font_mask)
> +		set_vc_hi_font(vc, true);
> +	else if (!old_hi_font_mask && vc->vc_hi_font_mask)
> +		set_vc_hi_font(vc, false);
> +
>   	font_data_put(data);
>   
>   	return ret;

-- 
--
Thomas Zimmermann
Graphics Driver Developer
SUSE Software Solutions Germany GmbH
Frankenstr. 146, 90461 N�rnberg, Germany, www.suse.com
GF: Jochen Jaser, Andrew McDonald, Werner Knoblich, (HRB 36809, AG N�rnberg)
