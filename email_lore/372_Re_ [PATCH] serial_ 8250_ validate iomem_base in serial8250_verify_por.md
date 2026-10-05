# Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port()

| Field | Value |
| :--- | :--- |
| From | Author A <author-a@example.com> |
| To | Jiri Slaby <jirislaby@kernel.org>, gregkh@linuxfoundation.org |
| Cc | ilpo.jarvinen@linux.intel.com, john.ogness@linutronix.de, andriy.shevchenko@linux.intel.com, linux-serial@vger.kernel.org, linux-kernel@vger.kernel.org, syzkaller@googlegroups.com, stable@vger.kernel.org |
| Date | Mon, 20 Jul 2026 14:44:19 +0800 |
| Message-ID | `706101a4-abf9-403d-a2cb-778a12338d99@anonymous.edu` |
| In-Reply-To | `0d58c9fe-7aed-43dd-8d00-6edfee9e2876@kernel.org` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/706101a4-abf9-403d-a2cb-778a12338d99@anonymous.edu/` |

---


>
> Pardon my ignorance, but isn't TIOCSSERIAL intended exactly to change 
> the type or mapbase when needed for ports without UPF_FIXED_PORT (and 
> under root)?
>
Hi Jiri,

You're right — TIOCSSERIAL is meant to allow root to change io_type
and iomem_base for non-fixed ports. My patch was indeed too restrictive
and would have broken that ABI. That was an oversight on my part.

The issue was identified via local syzkaller testing. It reveals two
related behaviors:
1. Root injects a bogus iomem_base via TIOCSSERIAL; opening the port
 � �faults in mem_serial_in() because the mapped address is invalid.
2. Because the fault happens while holding tty_lock, the mutex remains
 � �locked by the dying task. This leads to a cascading use-after-free
 � �when subsequent threads try to acquire the same lock.

While (1) is effectively "root shooting itself in the foot," (2) results
in cascading failures beyond the initial fault.

How would you prefer to handle this? Should we treat this as wontfix
(since it requires CAP_SYS_ADMIN), or would you prefer a more targeted
check to avoid the cascading UAF (e.g., ensuring basic validity of the
mapbase before ioremap)?

I'm happy to drop this patch if you feel the risk does not justify
additional checks.

thanks,
Author A
