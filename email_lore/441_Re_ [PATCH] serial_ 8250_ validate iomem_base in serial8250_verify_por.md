# Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port()

| Field | Value |
| :--- | :--- |
| From | Greg KH <gregkh@linuxfoundation.org> |
| To | Author A <author-a@example.com> |
| Cc | Jiri Slaby <jirislaby@kernel.org>, ilpo.jarvinen@linux.intel.com, john.ogness@linutronix.de, andriy.shevchenko@linux.intel.com, linux-serial@vger.kernel.org, linux-kernel@vger.kernel.org, syzkaller@googlegroups.com, stable@vger.kernel.org |
| Date | Mon, 20 Jul 2026 08:47:24 +0200 |
| Message-ID | `2026072015-ammonium-dismount-7af5@gregkh` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/2026072015-ammonium-dismount-7af5@gregkh/` |

---

On Mon, Jul 20, 2026 at 02:44:19PM +0800, Author A wrote:
> 
> > 
> > Pardon my ignorance, but isn't TIOCSSERIAL intended exactly to change
> > the type or mapbase when needed for ports without UPF_FIXED_PORT (and
> > under root)?
> > 
> Hi Jiri,
> 
> You're right — TIOCSSERIAL is meant to allow root to change io_type
> and iomem_base for non-fixed ports. My patch was indeed too restrictive
> and would have broken that ABI. That was an oversight on my part.
> 
> The issue was identified via local syzkaller testing. It reveals two
> related behaviors:
> 1. Root injects a bogus iomem_base via TIOCSSERIAL; opening the port
> � �faults in mem_serial_in() because the mapped address is invalid.
> 2. Because the fault happens while holding tty_lock, the mutex remains
> � �locked by the dying task. This leads to a cascading use-after-free
> � �when subsequent threads try to acquire the same lock.
> 
> While (1) is effectively "root shooting itself in the foot," (2) results
> in cascading failures beyond the initial fault.
> 
> How would you prefer to handle this? Should we treat this as wontfix
> (since it requires CAP_SYS_ADMIN), or would you prefer a more targeted
> check to avoid the cascading UAF (e.g., ensuring basic validity of the
> mapbase before ioremap)?

I think we have been through this before, look in the archives.  syzbot
shouldn't be doing things as root that are known to cause problems with
the system.

It's like it calling 'rm -rf /' from a syzbot script, just don't do
that :)

> I'm happy to drop this patch if you feel the risk does not justify
> additional checks.

We can't break existing apis, sorry.

thanks,

greg k-h
