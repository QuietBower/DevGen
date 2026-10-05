# Re: Re:[PATCH 7.0] fbdev: fbcon: fix memory leak in error path of fbcon_do_set_font()

| Field | Value |
| :--- | :--- |
| From | Greg KH <greg@kroah.com> |
| To | author-a-alt <author-a-alt@example.com> |
| Cc | simona@ffwll.ch, deller@gmx.de, tzimmermann@suse.de, ville.syrjala@linux.intel.com, sam@ravnborg.org, kees@kernel.org, yanquanmin1@huawei.com, syoshida@redhat.com, linux-fbdev@vger.kernel.org, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com>, stable@vger.kernel.org |
| Date | Mon, 27 Jul 2026 09:11:23 +0200 |
| Message-ID | `2026072719-kissable-antonym-d31e@gregkh` |
| In-Reply-To | `726d129f.53bd.19fa2601661.Coremail.author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/2026072719-kissable-antonym-d31e@gregkh/` |

---

On Mon, Jul 27, 2026 at 03:00:34PM +0800, author-a-alt wrote:
> 
> From: Author A <author-a@example.com>
> 
> Hi Helge, Simona, and all,
> 
> Resending this patch as a gentle reminder, since it has been a while 
> from the original submission (May 25) and my previous ping (June 5).
> 
> As a quick note, since this memory leak was implicitly resolved in 
> mainline via a recent refactor, this fix is specifically targeted at 
> 7.0 and older stable branches using the legacy userfont logic.

Why not just take the same upstream commits for older kernels as well?

Whenever you do "one off" patches like this, it makes backporting future
changes/fixes almost impossible, and we almost always get it wrong.  So
backporting the same changes is almost always the best idea.

Also note that the 7.0.y kernel is long end-of-life.

thanks,

greg k-h
