# Re: [PATCH] drm/prime: Fix unsupervised rb_tree corruption in drm_prime_remove_buf_handle

| Field | Value |
| :--- | :--- |
| From | Christian König <christian.koenig@amd.com> |
| To | author-a-alt@example.com, maarten.lankhorst@linux.intel.com, mripard@kernel.org, tzimmermann@suse.de, airlied@gmail.com, simona@ffwll.ch, sumit.semwal@linaro.org |
| Cc | jeffy.chen@rock-chips.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, linux-media@vger.kernel.org, linaro-mm-sig@lists.linaro.org, Author A <author-a@example.com>, stable@vger.kernel.org |
| Date | Thu, 28 May 2026 11:14:32 +0200 |
| Message-ID | `0e12ce28-f5b7-4ffa-849c-df9ad1796e22@amd.com` |
| In-Reply-To | `20260528082912.1051262-1-author-a-alt@example.com` |
| List-ID | <linux-media.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/0e12ce28-f5b7-4ffa-849c-df9ad1796e22@amd.com/` |

---

On 5/28/26 10:29, author-a-alt@example.com wrote:
> From: Author A <author-a@example.com>
> 
> Syzkaller fuzzer triggered a kernel panic via a WARNING in
> drm_prime_destroy_file_private() due to a non-empty prime rb_tree.
> 
> The root cause is a complete lack of synchronization in the teardown
> path. While the import path (drm_gem_prime_fd_to_handle) holds the
> &file_priv->prime.lock during lookup and insertion, the deletion path
> (drm_prime_remove_buf_handle) traverses and mutates both the 'handles'
> and 'dmabufs' rb_trees without acquiring any mutex.

That's just simply incorrect, drm_prime_remove_buf_handle() is called with the lock already held.

See drm_gem_object_release_handle():

        mutex_lock(&file_priv->prime.lock);

        drm_prime_remove_buf_handle(&file_priv->prime, id);

        mutex_unlock(&file_priv->prime.lock);

So the patch you propose here is just nonsense.

What tree are you working on? Could it be that this is something specific to a certain version.

Regards,
Christian.

> 
> When multiple threads concurrently close GEM handles or interleave import
> and close operations, the pointers and balance states of the rb_tree
> nodes get corrupted. As a result, certain members are erased from one
> tree but remain orphaned in the other. Upon process exit, the final
> sanity check triggers the WARNING.
> 
> [    448.919314][T19739] ------------[ cut here ]------------
> [    448.945387][T19739] WARNING: CPU: 0 PID: 19739 at drivers/gpu/drm/drm_prime.c:223 drm_prime_destroy_file_private+0x43/0x60
> ...
> [    449.056535][T19739] Call Trace:
> [    449.056544][T19739]  <TASK>
> [    449.056553][T19739]  drm_file_free.part.0+0x805/0xcf0
> [    449.056652][T19739]  drm_close_helper.isra.0+0x183/0x1f0
> [    449.056677][T19739]  drm_release+0x1ab/0x360
> [    449.056719][T19739]  __fput+0x402/0xb50
> [    449.056783][T19739]  task_work_run+0x16b/0x260
> [    449.056883][T19739]  exit_to_user_mode_loop+0xf9/0x130
> [    449.056931][T19739]  do_syscall_64+0x424/0xfa0
> [    449.056977][T19739]  entry_SYSCALL_64_after_hwframe+0x77/0x7f
> [    449.057268][T19739]  </TASK>
> [    449.057295][T19739] Kernel panic - not syncing: kernel: panic_on_warn set ...
> 
> Fix this by acquiring the prime_fpriv->lock mutex around the rb_tree
> lookup and erasure logic. To respect the locking rules and avoid potential
> deadlocks with driver-specific memory cleanups, assign the target node to
> a temporary pointer and defer the dma_buf_put() and kfree() operations
> until after the mutex is safely dropped.
> 
> Fixes: ea2aa97ca37a ("drm/gem: Fix GEM handle release errors")
> Cc: stable@vger.kernel.org
> Signed-off-by: Author A <author-a@example.com>
> ---
>  drivers/gpu/drm/drm_prime.c | 13 +++++++++++--
>  1 file changed, 11 insertions(+), 2 deletions(-)
> 
> diff --git a/drivers/gpu/drm/drm_prime.c b/drivers/gpu/drm/drm_prime.c
> index 9b44c78cd77f..26319c638e0f 100644
> --- a/drivers/gpu/drm/drm_prime.c
> +++ b/drivers/gpu/drm/drm_prime.c
> @@ -190,6 +190,9 @@ void drm_prime_remove_buf_handle(struct drm_prime_file_private *prime_fpriv,
>                                  uint32_t handle)
>  {
>         struct rb_node *rb;
> +       struct drm_prime_member *found = NULL;
> +
> +       mutex_lock(&prime_fpriv->lock);
> 
>         rb = prime_fpriv->handles.rb_node;
>         while (rb) {
> @@ -200,8 +203,7 @@ void drm_prime_remove_buf_handle(struct drm_prime_file_private *prime_fpriv,
>                         rb_erase(&member->handle_rb, &prime_fpriv->handles);
>                         rb_erase(&member->dmabuf_rb, &prime_fpriv->dmabufs);
> 
> -                       dma_buf_put(member->dma_buf);
> -                       kfree(member);
> +                       found = member;
>                         break;
>                 } else if (member->handle < handle) {
>                         rb = rb->rb_right;
> @@ -209,6 +211,13 @@ void drm_prime_remove_buf_handle(struct drm_prime_file_private *prime_fpriv,
>                         rb = rb->rb_left;
>                 }
>         }
> +       mutex_unlock(&prime_fpriv->lock);
> +
> +       /* Defer resource release outside the mutex to prevent deadlocks */
> +       if (found) {
> +               dma_buf_put(found->dma_buf);
> +               kfree(found);
> +       }
>  }
> 
>  void drm_prime_init_file_private(struct drm_prime_file_private *prime_fpriv)
> --
> 2.34.1
>
