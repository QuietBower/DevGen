# Re: [PATCH] misc: ibmasm: Fix out-of-bounds MMIO access during module load

| Field | Value |
| :--- | :--- |
| From | Greg KH <gregkh@linuxfoundation.org> |
| To | author-a-alt@example.com |
| Cc | arnd@arndb.de, linux-kernel@vger.kernel.org, kees@kernel.org, Author A <author-a@example.com>, stable@vger.kernel.org |
| Date | Tue, 23 Jun 2026 09:16:19 +0200 |
| Message-ID | `2026062354-crawfish-t-shirt-d45d@gregkh` |
| In-Reply-To | `20260623070909.362260-1-author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/2026062354-crawfish-t-shirt-d45d@gregkh/` |

---

On Tue, Jun 23, 2026 at 03:09:09PM +0800, author-a-alt@example.com wrote:
> From: Author A <author-a@example.com>
> 
> The ibmasm driver maps PCI BAR 0 without verifying if the hardware-provided
> resource length is sufficient. The driver statically accesses the
> INTR_CONTROL_REGISTER at offset 0x13A4.

The kernel trusts the hardware to not do foolish things like this :)

> When evaluating the driver against emulated hardware or during virtual
> device fuzzing, a malformed device may expose a significantly undersized
> BAR 0 (e.g., 4KB). In this scenario, the readl() in enable_sp_interrupts()
> crosses the mapped page boundary into unmapped memory, causing a page fault
> during probe.

Are you sure this is the only code path for this type of issue for this
device/driver?  Why just worry about this one?


thanks,

greg k-h
