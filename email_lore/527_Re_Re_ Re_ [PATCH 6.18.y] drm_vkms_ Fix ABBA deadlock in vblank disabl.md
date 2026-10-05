# Re:Re: Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback 20260403(27802f6d) Copyright (c) 2002-2026 www.mailtech.cn 163com

| Field | Value |
| :--- | :--- |
| From | author-a-alt <author-a-alt@example.com> |
| To | "Greg KH" <gregkh@linuxfoundation.org> |
| Cc | louis.chauvet@bootlin.com, hamohammed.sa@gmail.com, simona@ffwll.ch, melissa.srw@gmail.com, maarten.lankhorst@linux.intel.com, mripard@kernel.org, tzimmermann@suse.de, airlied@gmail.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, stable@vger.kernel.org, "Author A" <author-a@example.com> |
| Date | Mon, 18 May 2026 10:22:15 +0800 (CST) |
| Message-ID | `397754a7.224c.19e38e42006.Coremail.author-a-alt@example.com` |
| In-Reply-To | `2026051633-skyward-parrot-cdd3@gregkh` |
| Source | `[ANONYMIZED] local copy (original archive link removed)` |

---

At 2026-05-16 17:51:59, "Greg KH" <gregkh@linuxfoundation.org> wrote:
>There is no "minimal-risk policy for stable trees".  And if there was,
>the least ammount of risk would be to take the reviewed and tested
>patches that are already in Linus's tree, and NOT take anything that is
>not already there, as 90% of the time that we do that, it comes back to
>bite us hard.
>
>So please, just backport all the needed changes here.  Otherwise how are
>we going to deal with the merge conflicts for the next 4 years in this
>file?
>
>Or, get the maintainers of this file to agree and review this one-off
>change that it is acceptable.  As they are going to be the ones getting
>the bug reports and not having their patches applied over the years, not
>anyone else :)

Hi Greg,

Got it. After looking deeper into the dependency chain, backporting the mainline commit (02e2681ffe1a) would indeed require pulling in the entire new DRM generic vblank timer infrastructure to 6.18.y. 

That scope is just too large and complex for this specific issue. Since a one-off patch is not the right approach either, I will just drop this patch and abandon the backport effort for 6.18.y.

Thanks for your time and the quick review.

Thanks,
Author A
