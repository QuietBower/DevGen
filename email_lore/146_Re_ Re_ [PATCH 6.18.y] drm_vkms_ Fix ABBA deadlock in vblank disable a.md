# Re: Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback

| Field | Value |
| :--- | :--- |
| From | Greg KH <gregkh@linuxfoundation.org> |
| To | author-a-alt <author-a-alt@example.com> |
| Cc | louis.chauvet@bootlin.com, hamohammed.sa@gmail.com, simona@ffwll.ch, melissa.srw@gmail.com, maarten.lankhorst@linux.intel.com, mripard@kernel.org, tzimmermann@suse.de, airlied@gmail.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, stable@vger.kernel.org, Author A <author-a@example.com> |
| Date | Sat, 16 May 2026 11:51:59 +0200 |
| Message-ID | `2026051633-skyward-parrot-cdd3@gregkh` |
| In-Reply-To | `581657f0.ba8.19e2eaaf003.Coremail.author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/2026051633-skyward-parrot-cdd3@gregkh/` |

---

On Sat, May 16, 2026 at 10:43:35AM +0800, author-a-alt wrote:
> 
> Hi Greg,
> 
> Thanks for the quick response and review.
> 
> 
> At 2026-05-15 23:09:46, "Greg KH" <greg@kroah.com> wrote:
> >On Fri, May 15, 2026 at 09:18:26PM +0800, author-a-alt@example.com wrote:
> >> From: Author A <author-a@example.com>
> >> 
> >> [Note: This patch addresses a legacy VKMS implementation deadlock specific
> >> to older stable trees (e.g., 6.18.y). Mainline has removed this code during
> >> the generic DRM_CRTC_VBLANK_TIMER_FUNCS refactoring.]
> >
> >Why not apply those upstream commits here as well?  No need to diverge
> >from Linus's tree, otherwise we will end up having a mess that nothing
> >can ever be backported to.
> >
> >How many commits need to be backported?  Have you tried?
> 
> I have looked into the upstream commits. The commit that removed this 
> vulnerable legacy code in mainline is:
> 02e2681ffe1a ("drm/vkms: Convert to DRM's vblank timer")
> 
> I tried to apply it to 6.18.y, but it does not apply cleanly. The reason 
> is that this upstream commit is not a simple bug fix, but a massive 
> refactoring. It completely rips out the custom VKMS hrtimer and ports 
> the driver to a newly introduced DRM core infrastructure 
> (DRM_CRTC_VBLANK_TIMER_FUNCS and drm_vblank_helper.h).
> 
> To backport commit 02e2681ffe1a, we would first need to backport the 
> entire DRM generic vblank timer infrastructure to 6.18.y. This seems 
> too intrusive and violates the minimal-risk policy for stable trees.

There is no "minimal-risk policy for stable trees".  And if there was,
the least ammount of risk would be to take the reviewed and tested
patches that are already in Linus's tree, and NOT take anything that is
not already there, as 90% of the time that we do that, it comes back to
bite us hard.

So please, just backport all the needed changes here.  Otherwise how are
we going to deal with the merge conflicts for the next 4 years in this
file?

Or, get the maintainers of this file to agree and review this one-off
change that it is acceptable.  As they are going to be the ones getting
the bug reports and not having their patches applied over the years, not
anyone else :)

thanks,

greg k-h
