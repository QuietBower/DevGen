# Re: [PATCH] tty: vt: fix memory leak in vc_allocate()

| Field | Value |
| :--- | :--- |
| From | Greg KH <gregkh@linuxfoundation.org> |
| To | Author A <author-a@example.com> |
| Cc | jirislaby@kernel.org, linux-kernel@vger.kernel.org, linux-serial@vger.kernel.org, stable@vger.kernel.org |
| Date | Thu, 23 Jul 2026 09:15:13 +0200 |
| Message-ID | `2026072345-copartner-shrink-9596@gregkh` |
| In-Reply-To | `20260723070530.117552-1-author-a@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/2026072345-copartner-shrink-9596@gregkh/` |

---

On Thu, Jul 23, 2026 at 03:05:30PM +0800, Author A wrote:
> If the screen buffer allocation fails in vc_allocate(), the error handling
> path jumps to `err_free`. However, this path fails to release the unicode
> screen map attached to `vc->uni_pagedict_loc`.
> 
> During the early stages of vc_allocate(), the unicode screen map is either
> newly allocated via con_set_default_unimap() or shares the default unicode
> map from a previously initialized console (which increments its refcount).
> If the subsequent kzalloc() for the screen buffer fails, the err_free path
> frees the vc structure but leaves the attached uni_pagedict with an
> elevated refcount. This results in a memory leak if that console is later
> deallocated.
> 
> Fix this by calling con_free_unimap(vc) in the err_free path before
> kfree(vc). This safely decrements the refcount and releases the dictionary
> memory if this was the last reference.
> 
> Fixes: 34902b7f2754 ("tty: vt, get rid of weird source code flow")
> Cc: stable@vger.kernel.org
> Signed-off-by: Author A <author-a@example.com>

How was this tested?  And how can this ever fail given this happens at
early boot time, right?

thanks,

greg k-h
