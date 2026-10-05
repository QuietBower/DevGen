# Re: [PATCH v2] misc: ibmasm: Fix static and dynamic out-of-bounds MMIO accesses

| Field | Value |
| :--- | :--- |
| From | Greg KH <gregkh@linuxfoundation.org> |
| To | author-a-alt@example.com |
| Cc | arnd@arndb.de, linux-kernel@vger.kernel.org, kees@kernel.org, stable@vger.kernel.org, Author A <author-a@example.com> |
| Date | Tue, 23 Jun 2026 13:53:30 +0200 |
| Message-ID | `2026062354-quote-lullaby-85e3@gregkh` |
| In-Reply-To | `20260623114046.368089-1-author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/2026062354-quote-lullaby-85e3@gregkh/` |

---

On Tue, Jun 23, 2026 at 07:40:46PM +0800, author-a-alt@example.com wrote:
> From: Author A <author-a@example.com>
> 
> The ibmasm driver maps PCI BAR 0 without verifying if the hardware-provided
> resource length is sufficient.
> 
> When evaluating the driver against emulated hardware or during virtual
> device fuzzing, a malformed device may expose a significantly undersized
> BAR 0. This leads to two distinct out-of-bounds (OOB) MMIO access vectors:
> 
> 1. Static OOB: The driver hardcodes access to INTR_CONTROL_REGISTER
>    (offset 0x13A4) during probe.
> 2. Dynamic OOB: The driver reads dynamic Message Frame Addresses (MFA)
>    from hardware queues and uses them directly as offsets to dereference
>    I2O messages via get_i2o_message(). A malicious MFA can cause the
>    driver to access memory far beyond the mapped BAR.
> 
> If an OOB access triggers a #PF during module probe while holding the
> idempotent_init_module() lock, it leaves the module loading subsystem
> in a corrupted state, leading to a cascading global soft lockup.
> 
> Fix this comprehensively by:
> - Storing the mapped resource size in 'struct service_processor'.
> - Ensuring the BAR size covers the highest statically accessed register
>   (INTR_CONTROL_REGISTER) during probe.
> - Validating all dynamic MFA offsets against the mapped size before
>   dereferencing to prevent dynamic OOB accesses.
> 
> Fixes: bdbeed75b288 ("pci: use pci_ioremap_bar() in drivers/misc")
> Cc: stable@vger.kernel.org
> Signed-off-by: Author A <author-a@example.com>
> ---
> Changes in v2:
>  - Added dynamic MFA bounds checking in get_i2o_message() to prevent runtime OOB (prompted by Greg KH).
>  - Implemented hardware mailbox deadlock prevention by releasing MFA if bounds check fails.
>  - Fixed potential unsigned integer underflow in bounds check arithmetic.

That's a lot of different things all at once here.  Please split this up
into a patch series, doing only one logical thing per patch so it is
easier to review and apply.

thanks,

greg k-h
