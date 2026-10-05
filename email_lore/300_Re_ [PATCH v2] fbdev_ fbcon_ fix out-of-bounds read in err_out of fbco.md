# Re: [PATCH v2] fbdev: fbcon: fix out-of-bounds read in err_out of fbcon_do_set_font()

| Field | Value |
| :--- | :--- |
| From | Helge Deller <deller@gmx.de> |
| To | Author A <author-a@example.com>, tzimmermann@suse.de, simona@ffwll.ch |
| Cc | syoshida@redhat.com, dri-devel@lists.freedesktop.org, linux-fbdev@vger.kernel.org, linux-kernel@vger.kernel.org, author-a-alt@example.com, stable@vger.kernel.org |
| Date | Thu, 25 Jun 2026 18:18:09 +0200 |
| Message-ID | `79ba53b6-97d6-41dc-aeaf-69181262e8b5@gmx.de` |
| In-Reply-To | `20260625160306.438847-1-author-a@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/79ba53b6-97d6-41dc-aeaf-69181262e8b5@gmx.de/` |

---

On 6/25/26 18:03, Author A wrote:
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
> Reviewed-by: Thomas Zimmermann <tzimmermann@suse.de>

applied.

Thanks!
Helge
