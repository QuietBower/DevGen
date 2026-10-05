# Re: [PATCH] tty: limit TCSBRKP break duration

| Field | Value |
| :--- | :--- |
| From | Greg Kroah-Hartman <gregkh@linuxfoundation.org> |
| To | Author F <author-f@example.com> |
| Cc | Jiri Slaby <jirislaby@kernel.org>, linux-kernel@vger.kernel.org, linux-serial@vger.kernel.org |
| Date | Fri, 31 Jul 2026 17:04:40 +0200 |
| Message-ID | `2026073157-expend-unzip-cad7@gregkh` |
| List-ID | <linux-serial.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/2026073157-expend-unzip-cad7@gregkh/` |

---

On Fri, Jul 31, 2026 at 10:30:41PM +0800, Author F wrote:
> Hi Greg,
> 
> Yes, it would still be user-visible.
> 
> My concern is that tty_ioctl() first multiplies the TCSBRKP argument by
> 100 before passing it to send_break(), whose duration argument is
> unsigned int. For values above UINT_MAX / 100, the current behavior uses
> the low 32 bits of the multiplied product, which can be a duration very
> different from the requested interval.
> 
> But I agree that rejecting or saturating that value would also change
> current behavior. Would you be open to a fix for this, or would you
> rather leave the path as-is?

I lost all context here :(

ANyway, keeping things as-is is always good, unless you know of a way
the current stuff is broken and you can fix it without changing any
existing users.  user apis are hard...

thanks,

greg k-h
