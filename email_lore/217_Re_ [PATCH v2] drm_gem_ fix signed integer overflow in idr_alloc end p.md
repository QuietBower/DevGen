# Re: [PATCH v2] drm/gem: fix signed integer overflow in idr_alloc end parameter

| Field | Value |
| :--- | :--- |
| From | sashiko-bot@kernel.org |
| To | author-a-alt@example.com |
| Cc | dri-devel@lists.freedesktop.org |
| Date | Fri, 05 Jun 2026 14:38:59 +0000 |
| Message-ID | `20260605143900.57CE51F00893@smtp.kernel.org` |
| In-Reply-To | `20260605142644.1205922-1-author-a-alt@example.com` |
| List-ID | Direct Rendering Infrastructure - Development <dri-devel.lists.freedesktop.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/20260605143900.57CE51F00893@smtp.kernel.org/` |

---

Thank you for your contribution! Sashiko AI review found 2 potential issue(s) to consider:

Pre-existing issues:
- [Critical] A race condition exists between `drm_gem_change_handle_ioctl` and `DRM_IOCTL_GEM_CLOSE` due to a lockless window, allowing a Use-After-Free, unbalanced handle counts, and IDR corruption.
- [Medium] Calling `idr_alloc` with `GFP_NOWAIT` inside a spinlock without a preceding `idr_preload` can cause spurious memory allocation failures.
--

commit b765b42ca862b5e1577465e07a11a3f29e24c848
Author: Author A <author-a@example.com>

drm/gem: fix signed integer overflow in idr_alloc end parameter

This commit fixes a signed integer overflow vulnerability in
drm_gem_change_handle_ioctl(). It tightens the limit check to >= INT_MAX to
prevent overflow and explicitly rejects 0 as a handle, as 0 is universally
treated as an invalid handle in the DRM subsystem.

> diff --git a/drivers/gpu/drm/drm_gem.c b/drivers/gpu/drm/drm_gem.c
> index ac751a42db281..b42241d6802f9 100644
> --- a/drivers/gpu/drm/drm_gem.c
> +++ b/drivers/gpu/drm/drm_gem.c
> @@ -1025,8 +1025,12 @@ int drm_gem_change_handle_ioctl(struct drm_device *dev, void *data,
>  	if (!drm_core_check_feature(dev, DRIVER_GEM))
>  		return -EOPNOTSUPP;
>  
> -	/* idr_alloc() limitation. */
> -	if (args->new_handle > INT_MAX)
> +	/*
> +	 * idr_alloc() limitation.
> +	 * Reject handle 0 (invalid in DRM) and strictly bound
> +	 * to < INT_MAX to avoid signed integer overflow in handle + 1.
> +	 */
> +	if (args->new_handle == 0 || args->new_handle >= INT_MAX)
>  		return -EINVAL;
>  	handle = args->new_handle;
>  

[Severity: Medium]
This is a pre-existing issue, but can calling idr_alloc() with GFP_NOWAIT
inside a spinlock without a preceding idr_preload() cause spurious memory
allocation failures?

drivers/gpu/drm/drm_gem.c:drm_gem_change_handle_ioctl() {
	...
	spin_lock(&file_priv->table_lock);
	...
	ret = idr_alloc(&file_priv->object_idr, obj, handle, handle + 1,
			GFP_NOWAIT);
	...
}

Because handle is user-controlled and can be up to INT_MAX - 1, the IDR radix
tree may need to allocate multiple deep tree nodes. Without preloading, is it
possible for this GFP_NOWAIT allocation to fail under memory pressure, causing
the ioctl to spuriously return -ENOMEM?

[Severity: Critical]
This is a pre-existing issue, but does a race condition exist between
drm_gem_change_handle_ioctl() and DRM_IOCTL_GEM_CLOSE due to a lockless
window?

drivers/gpu/drm/drm_gem.c:drm_gem_change_handle_ioctl() {
	...
	idr_replace(&file_priv->object_idr, NULL, args->handle);
	spin_unlock(&file_priv->table_lock);
	...
}

drm_gem_change_handle_ioctl() looks up args->handle locklessly, gaining a
temporary kref. Between this lookup and acquiring table_lock, could another
thread call DRM_IOCTL_GEM_CLOSE on args->handle, removing it from the IDR and
releasing its handle reference?

When drm_gem_change_handle_ioctl() resumes, it ignores the return value of
idr_replace(..., NULL, args->handle) and unconditionally associates
args->new_handle with obj.

Since the handle reference was dropped by the concurrent close, and this
function doesn't acquire a new one for the new handle, could obj be left in
the IDR without a corresponding handle reference?

When the temporary lookup kref is dropped via drm_gem_object_put(obj) at the
end of the function, the object is freed, but args->new_handle still points to
it in the IDR. Could subsequent access to this handle trigger an exploitable
use-after-free?

-- 
Sashiko AI review · `[ANONYMIZED] https://sashiko.dev/#/patchset/20260605142644.1205922-1-author-a-alt@example.com?part=1 (author-address patchset permalink)`
