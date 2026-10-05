# Re: [PATCH 6.18.y] drm/vkms: Fix ABBA deadlock in vblank disable and timer callback

| Field | Value |
| :--- | :--- |
| From | Maarten Lankhorst <maarten.lankhorst@linux.intel.com> |
| To | author-a-alt <author-a-alt@example.com>, Greg KH <gregkh@linuxfoundation.org> |
| Cc | louis.chauvet@bootlin.com, hamohammed.sa@gmail.com, simona@ffwll.ch, melissa.srw@gmail.com, mripard@kernel.org, tzimmermann@suse.de, airlied@gmail.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, stable@vger.kernel.org, Author A <author-a@example.com> |
| Date | Mon, 25 May 2026 10:55:43 +0200 |
| Message-ID | `9c4a68c4-43a3-4a9b-a131-9570174c8df3@linux.intel.com` |
| In-Reply-To | `397754a7.224c.19e38e42006.Coremail.author-a-alt@example.com` |
| Source | `[ANONYMIZED] local copy (original archive link removed)` |

---

Hello Author A

Den 2026-05-18 kl. 04:22, skrev author-a-alt:
> 
> 
> At 2026-05-16 17:51:59, "Greg KH" <gregkh@linuxfoundation.org> wrote:
>> There is no "minimal-risk policy for stable trees".  And if there was,
>> the least ammount of risk would be to take the reviewed and tested
>> patches that are already in Linus's tree, and NOT take anything that is
>> not already there, as 90% of the time that we do that, it comes back to
>> bite us hard.
>>
>> So please, just backport all the needed changes here.  Otherwise how are
>> we going to deal with the merge conflicts for the next 4 years in this
>> file?
>>
>> Or, get the maintainers of this file to agree and review this one-off
>> change that it is acceptable.  As they are going to be the ones getting
>> the bug reports and not having their patches applied over the years, not
>> anyone else :)
> 
> Hi Greg,
> 
> Got it. After looking deeper into the dependency chain, backporting the mainline commit (02e2681ffe1a) would indeed require pulling in the entire new DRM generic vblank timer infrastructure to 6.18.y. 
> 
> That scope is just too large and complex for this specific issue. Since a one-off patch is not the right approach either, I will just drop this patch and abandon the backport effort for 6.18.y.
> 
> Thanks for your time and the quick review.
> 
> Thanks,
> Author A
> 

As far as I can tell, if it's just a bug affecting vkms, all you need to do
is only a few commits:

74afeb812850 ("drm/vblank: Add vblank timer")
d54dbb5963bd ("drm/vblank: Add CRTC helpers for simple use cases")
02e2681ffe1a ("drm/vkms: Convert to DRM's vblank timer")
79ae8510b5b8 ("drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks()")
3946d3ba9934 ("drm/vblank: Fix kernel docs for vblank timer")

There's no need to convert all other drivers if it's only vkms that you're fixing.

But since you found this bug in one driver, it might be wise to check if others
have the same bug and ask for backports for those too.

Kind regards,
~Maarten Lankhorst
