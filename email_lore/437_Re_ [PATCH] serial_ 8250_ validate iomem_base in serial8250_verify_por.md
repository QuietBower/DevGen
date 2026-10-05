# Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port()

| Field | Value |
| :--- | :--- |
| From | Jiri Slaby <jirislaby@kernel.org> |
| To | Author A <author-a@example.com>, gregkh@linuxfoundation.org |
| Cc | ilpo.jarvinen@linux.intel.com, john.ogness@linutronix.de, andriy.shevchenko@linux.intel.com, linux-serial@vger.kernel.org, linux-kernel@vger.kernel.org, syzkaller@googlegroups.com, stable@vger.kernel.org |
| Date | Mon, 20 Jul 2026 08:08:48 +0200 |
| Message-ID | `0d58c9fe-7aed-43dd-8d00-6edfee9e2876@kernel.org` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/0d58c9fe-7aed-43dd-8d00-6edfee9e2876@kernel.org/` |

---

On 20. 07. 26, 7:37, Author A wrote:
> Fix this by tightening the validation in serial8250_verify_port().
> Reject any TIOCSSERIAL requests that attempt to modify the io_type
> to/from memory-mapped types, or change the iomem_base of an existing
> memory-mapped port. Userspace can still legitimately modify IRQs or
> baud rates of MMIO ports as long as the io_type and mapbase remain
> unchanged.

Pardon my ignorance, but isn't TIOCSSERIAL intended exactly to change 
the type or mapbase when needed for ports without UPF_FIXED_PORT (and 
under root)?

thanks,
-- 
js
suse labs
