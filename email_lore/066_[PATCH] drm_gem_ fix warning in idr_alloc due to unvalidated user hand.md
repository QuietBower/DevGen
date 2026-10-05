# [PATCH] drm/gem: fix warning in idr_alloc due to unvalidated user handle

| Field | Value |
| :--- | :--- |
| From | Author A <author-a@example.com> |
| To | maarten.lankhorst@linux.intel.com, mripard@kernel.org, tzimmermann@suse.de, airlied@gmail.com, simona@ffwll.ch |
| Cc | dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Wed, 22 Apr 2026 19:42:47 +0800 |
| Message-ID | `20260422114247.486581-1-author-a@example.com` |
| List-ID | <linux-kernel.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260422114247.486581-1-author-a@example.com/` |

---

During fuzzing, a warning was triggered in idr_alloc() when handling
the DRM_IOCTL_GEM_CHANGE_HANDLE (or similar) ioctl.

The function drm_gem_change_handle_ioctl() currently only checks if
args->new_handle is strictly greater than INT_MAX. However, it fails
to check for negative values. If a userpace application passes a
negative handle, it bypasses the upper-bound check and is passed
directly to idr_alloc() as the 'start' parameter, triggering the
WARN_ON_ONCE(start < 0) inside idr_alloc().

Fix this by explicitly validating that the user-provided handle is
strictly positive and within the valid IDR range.

Signed-off-by: Author A <author-a@example.com>
---
 drivers/gpu/drm/drm_gem.c | 2 +-
 1 file changed, 1 insertion(+), 1 deletion(-)

diff --git a/drivers/gpu/drm/drm_gem.c b/drivers/gpu/drm/drm_gem.c
index d6424267260b..3d84d4f1c3e0 100644
--- a/drivers/gpu/drm/drm_gem.c
+++ b/drivers/gpu/drm/drm_gem.c
@@ -1026,7 +1026,7 @@ int drm_gem_change_handle_ioctl(struct drm_device *dev, void *data,
 		return -EOPNOTSUPP;
 
 	/* idr_alloc() limitation. */
-	if (args->new_handle > INT_MAX)
+	if (args->new_handle <= 0 || args->new_handle > INT_MAX)
 		return -EINVAL;
 	handle = args->new_handle;
 
-- 
2.34.1
