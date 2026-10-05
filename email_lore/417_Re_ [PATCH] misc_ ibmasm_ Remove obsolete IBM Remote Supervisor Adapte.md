# Re: [PATCH] misc: ibmasm: Remove obsolete IBM Remote Supervisor Adapter driver

| Field | Value |
| :--- | :--- |
| From | "Arnd Bergmann" <arnd@arndb.de> |
| To | "Author A" <author-a@example.com>, "Greg Kroah-Hartman" <gregkh@linuxfoundation.org> |
| Cc | "Kees Cook" <kees@kernel.org>, linux-kernel@vger.kernel.org, stable@vger.kernel.org |
| Date | Sat, 01 Aug 2026 11:23:32 +0200 |
| Message-ID | `f22ccf39-7d07-4b96-be44-89d86153402c@app.fastmail.com` |
| In-Reply-To | `20260801070756.161698-1-author-a@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/f22ccf39-7d07-4b96-be44-89d86153402c@app.fastmail.com/` |

---

On Sat, Aug 1, 2026, at 09:07, Author A wrote:
> The IBM Remote Supervisor Adapter (RSA) and RSA II were out-of-band
> management PCI/ISA cards introduced in the early 2000s for ancient
> IBM eServer xSeries machines. IBM deprecated the RSA family and
> replaced it with the Integrated Management Module (IMM) around 2008.
> IBM subsequently sold its x86 server business to Lenovo in 2014, and
> modern systems use entirely different BMC architectures.
>
> This hardware has been completely obsolete and out of production for
> over 15 years. Surviving physical servers using this specific hardware
> would be running ancient 32-bit processors vastly unsuited for modern
> kernels.

Technically I think there were three generations of 64-bit system
that shipped with this, not just 32-bit. I see that end of support
life for the x3650 M1 server was in 2018, which is still a long time
ago, but not quite as old as you make it sound. As far as I can tell,
SLES12-LTS still supports those servers until next year and runs
the userspace for ibmasm, but obviously won't update their kernels.

> Remove the driver entirely.
>
> Signed-off-by: Author A <author-a@example.com>

Acked-by: Arnd Bergmann <arnd@arndb.de>
