# Re: [PATCH v2 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock

| Field | Value |
| :--- | :--- |
| From | Sasha Levin <sashal@kernel.org> |
| To | stable@vger.kernel.org, gregkh@linuxfoundation.org, author-a-alt@example.com |
| Cc | Sasha Levin <sashal@kernel.org>, tzimmermann@suse.de, maarten.lankhorst@linux.intel.com, mripard@kernel.org, louis.chauvet@bootlin.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Wed, 27 May 2026 15:49:08 -0400 |
| Message-ID | `20260527-agent5-item016-vkms@kernel.org` |
| In-Reply-To | `20260526133123.691465-1-author-a-alt@example.com` |
| Source | `[ANONYMIZED] local copy (original archive link removed)` |

---

> This series backports the generic vblank timer infrastructure and
> converts the vkms driver to use it, fixing an ABBA deadlock.

All five patches queued for 6.18.y per Maarten's ack. Thanks for the
v2 with the proper Signed-off-by chain.

--
Thanks,
Sasha
