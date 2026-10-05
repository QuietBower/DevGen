# [PATCH 6.18.y 0/5] drm/vkms: Backport generic vblank timer to fix ABBA deadlock

| Field | Value |
| :--- | :--- |
| From | author-a-alt@example.com |
| To | stable@vger.kernel.org, gregkh@linuxfoundation.org |
| Cc | tzimmermann@suse.de, maarten.lankhorst@linux.intel.com, mripard@kernel.org, louis.chauvet@bootlin.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Mon, 25 May 2026 21:16:05 +0800 |
| Message-ID | `20260525131610.608273-1-author-a-alt@example.com` |
| In-Reply-To | `9c4a68c4-43a3-4a9b-a131-9570174c8df3@linux.intel.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260525131610.608273-1-author-a-alt@example.com/` |

---

From: Author A <author-a@example.com>

Hi Greg and all,

This patch series backports the generic DRM vblank timer infrastructure
and converts the vkms driver to use it, specifically targeting the 
6.18.y stable branch.

During local fuzzing with Syzkaller, an RCU preempt stall (ABBA deadlock)
was consistently observed in the 6.18.y vkms driver. This deadlock occurs
between the legacy drm_vblank_disable_and_save() function and the
vkms_vblank_simulate() hrtimer callback. 

A previous localized patch was submitted to address this in 6.18.y using
hrtimer_try_to_cancel. However, as discussed with Greg KH and Maarten
Lankhorst on the mailing list, the correct and most maintainable approach
is to backport the mainline commits that inherently resolve this by
removing the custom vkms hrtimer entirely.

Following Maarten's roadmap, this series cherry-picks the exact
dependency chain from mainline to introduce the drm_vblank_helper
infrastructure and migrate vkms to it. 

The series applies smoothly to 6.18.y and completely resolves the soft
lockup in the fuzzing environment.

Thanks,
Author A

Thomas Zimmermann (5):
  drm/vblank: Add vblank timer
  drm/vblank: Add CRTC helpers for simple use cases
  drm/vkms: Convert to DRM's vblank timer
  drm/atomic: Increase timeout in drm_atomic_helper_wait_for_vblanks()
  drm/vblank: Fix kernel docs for vblank timer

 Documentation/gpu/drm-kms-helpers.rst    |  12 ++
 drivers/gpu/drm/Makefile                 |   3 +-
 drivers/gpu/drm/drm_atomic_helper.c      |   2 +-
 drivers/gpu/drm/drm_vblank.c             | 172 +++++++++++++++++++++-
 drivers/gpu/drm/drm_vblank_helper.c      | 176 +++++++++++++++++++++++
 drivers/gpu/drm/vkms/vkms_crtc.c         |  83 +----------
 drivers/gpu/drm/vkms/vkms_drv.h          |   2 -
 include/drm/drm_modeset_helper_vtables.h |  12 ++
 include/drm/drm_vblank.h                 |  32 +++++
 include/drm/drm_vblank_helper.h          |  56 ++++++++
 10 files changed, 468 insertions(+), 82 deletions(-)
 create mode 100644 drivers/gpu/drm/drm_vblank_helper.c
 create mode 100644 include/drm/drm_vblank_helper.h

-- 
2.34.1
