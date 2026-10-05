# Re: [PATCH v2] drm/prime: fix dangling dmabuf entries after handle release

| Field | Value |
| :--- | :--- |
| From | =?UTF-8?Q?Christian_K=C3=B6nig?= <christian.koenig@amd.com> |
| To | author-a-alt <author-a-alt@example.com> |
| Cc | maarten.lankhorst@linux.intel.com, mripard@kernel.org, tzimmermann@suse.de, airlied@gmail.com, simona@ffwll.ch, sumit.semwal@linaro.org, jeffy.chen@rock-chips.com, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, linux-media@vger.kernel.org, linaro-mm-sig@lists.linaro.org, Author A <author-a@example.com> |
| Date | Fri, 29 May 2026 08:45:01 +0200 |
| Message-ID | `bb4d3330-822e-45ae-94f4-65ba0e351f57@amd.com` |
| In-Reply-To | `7bbb8946.9d19.19e6ed958df.Coremail.author-a-alt@example.com` |
| Source | `[ANONYMIZED] local copy (original archive link removed)` |

---

Hi Author A,

On 5/28/26 15:49, author-a-alt wrote:
> Hi Christian,
> 
> Thank you for insisting on this. I've now gone through all callers
> of drm_prime_add_buf_handle() in drm_prime.c.
> 
> You are absolutely right: both drm_gem_prime_fd_to_handle() and
> drm_gem_prime_handle_to_dmabuf() perform the lookup under
> prime_fpriv->lock before adding, so a duplicate handle should indeed
> never be inserted through those paths.
> 
> That said, the syzkaller report clearly shows that the dmabufs tree
> is not empty when drm_prime_destroy_file_private() runs, which means
> some entry wasn't removed. Given that the normal add/remove paths
> appear correct, the trigger might be something more subtle — perhaps
> a driver-specific callback that bypasses the generic helpers, or an
> error path that leaves an orphan in the dmabufs tree. I haven't been
> able to identify the exact trigger yet.
> 
> The proposed change to drm_prime_remove_buf_handle() (restart search
> instead of break) is intended as a small robustness improvement, not
> a fix for a confirmed race. In the normal case it will still execute
> only once, but if the trees ever become inconsistent for any reason,
> it will clean up all entries for the given handle and prevent the
> WARNING.
> 
> Would you be okay with such a defensive approach, or would you prefer
> that we first track down the precise trigger (e.g. with additional
> WARNs or tracing)?

I don't think so. As far as I can see this is not a robustness improvement but just papering over an issue.

Leaking memory is usually only a very minor problem, things like use after free or random memory corruption is much more worse.

And such things is exactly what starts to happens when you start papering over issues.

So I would say find the root cause of what is going on here, you have certainly stumbled over something, and then we can look into how to fix that.

But just sending out random patches where a bit of simple code reading can prove them incorrect is not really helpful.

Regards,
Christian.

> 
> Thanks,
> Author A
