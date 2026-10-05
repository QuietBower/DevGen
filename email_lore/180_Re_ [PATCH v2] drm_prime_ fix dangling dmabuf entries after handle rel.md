# Re: [PATCH v2] drm/prime: fix dangling dmabuf entries after handle release

| Field | Value |
| :--- | :--- |
| From | Christian König <christian.koenig@amd.com> |
| To | author-a-alt@example.com, maarten.lankhorst@linux.intel.com, mripard@kernel.org, tzimmermann@suse.de, airlied@gmail.com, simona@ffwll.ch, sumit.semwal@linaro.org |
| Cc | jeffy.chen@rock-chips.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, linux-media@vger.kernel.org, linaro-mm-sig@lists.linaro.org, Author A <author-a@example.com> |
| Date | Thu, 28 May 2026 15:32:28 +0200 |
| Message-ID | `62c256eb-1df4-4633-8040-222895b54f97@amd.com` |
| In-Reply-To | `20260528132932.1078483-1-author-a-alt@example.com` |
| List-ID | <linux-media.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/62c256eb-1df4-4633-8040-222895b54f97@amd.com/` |

---

On 5/28/26 15:29, author-a-alt@example.com wrote:
> From: Author A <author-a@example.com>
> 
> When a GEM handle already exists in the drm_prime_file_private, repeated
> calls to DRM_IOCTL_PRIME_HANDLE_TO_FD can cause drm_prime_add_buf_handle()
> to insert multiple entries with the same handle into the handles rb_tree.
> Because the insertion walk moves left on equality, duplicate keys are
> structurally accepted by the tree.

That should never happen and would be a major bug.

All callers should check if a handler exists before calling drm_prime_add_buf_handle().

How do you see that a handle is added twice?

Regards,
Christian.

> 
> Later, when the handle is released via drm_gem_release() ->
> drm_gem_object_release_handle() -> drm_prime_remove_buf_handle(), the
> latter iterates the handles tree, removes the first matching node, and
> breaks out of the loop. Any remaining duplicate nodes that share the
> same handle are left orphaned in the dmabufs tree - they are no longer
> reachable through the handles tree and are never freed.
> 
> When the drm file is finally closed, drm_prime_destroy_file_private()
> triggers:
> 
>         WARN_ON(!RB_EMPTY_ROOT(&prime_fpriv->dmabufs));
> 
> because the dmabufs tree is still non-empty. With CONFIG_PANIC_ON_WARN
> this becomes a kernel panic:
> 
>         ------------[ cut here ]------------
>         WARNING: CPU: 0 PID: 19739 at drivers/gpu/drm/drm_prime.c:223 drm_prime_destroy_file_private+0x43/0x60
>         ...
>         Kernel panic - not syncing: kernel: panic_on_warn set ...
> 
> Fix this by restarting the lookup from the root of the handles tree
> after each successful removal, so that all duplicate nodes for the given
> handle are erased. The caller (drm_gem_object_release_handle) already
> holds prime_fpriv->lock, so this does not change the locking strategy.
> 
> Signed-off-by: Author A <author-a@example.com>
> ---
> Changes in v2:
>  - Drop the unnecessary mutex_lock addition, as the caller (drm_gem_object_release_handle) already holds the lock.
>  - Rewrite the commit message to accurately reflect the root cause (duplicate handle insertions) rather than an assumed lack of synchronization.
>  - Restart the rb_tree lookup from the root instead of breaking the loop to ensure all orphaned duplicate nodes are thoroughly removed.
> 
>  drivers/gpu/drm/drm_prime.c | 5 ++++-
>  1 file changed, 4 insertions(+), 1 deletion(-)
> 
> diff --git a/drivers/gpu/drm/drm_prime.c b/drivers/gpu/drm/drm_prime.c
> index 9b44c78cd77f..dc28df1c6698 100644
> --- a/drivers/gpu/drm/drm_prime.c
> +++ b/drivers/gpu/drm/drm_prime.c
> @@ -202,7 +202,10 @@ void drm_prime_remove_buf_handle(struct drm_prime_file_private *prime_fpriv,
> 
>                         dma_buf_put(member->dma_buf);
>                         kfree(member);
> -                       break;
> +                       /* Duplicate handles may exist; restart search from root
> +                        * to guarantee removal of all matching entries.
> +                        */
> +                       rb = prime_fpriv->handles.rb_node;
>                 } else if (member->handle < handle) {
>                         rb = rb->rb_right;
>                 } else {
> --
> 2.34.1
>
