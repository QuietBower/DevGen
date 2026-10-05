# Re: [PATCH] tty: limit TCSBRKP break duration

| Field | Value |
| :--- | :--- |
| From | Greg Kroah-Hartman <gregkh@linuxfoundation.org> |
| To | Author F <author-f@example.com> |
| Cc | Jiri Slaby <jirislaby@kernel.org>, linux-kernel@vger.kernel.org, linux-serial@vger.kernel.org |
| Date | Wed, 29 Jul 2026 13:50:43 +0200 |
| Message-ID | `2026072921-curled-moonlike-1207@gregkh` |
| List-ID | <linux-serial.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/2026072921-curled-moonlike-1207@gregkh/` |

---

On Wed, Jul 29, 2026 at 07:19:27PM +0800, Author F wrote:
> Hi Greg,
> 
> Thanks for the review.
> 
> You are right. The RCU stall I observed does not by itself justify
> clamping long break intervals in the tty core, so I will drop that
> patch.
> 
> While looking at this path, I noticed that tty_ioctl() multiplies the
> TCSBRKP argument by 100 and passes the result to send_break(), whose
> duration argument is unsigned int. Values whose product exceeds UINT_MAX
> are silently truncated to an unintended duration.
> 
> Do you think it is worth fixing that separately by rejecting TCSBRKP
> values that cannot fit in the unsigned-int millisecond duration used by
> send_break()?

Wouldn't that break userspace that is currently relying on that
truncation?

thanks,

greg k-h
