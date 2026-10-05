# Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe

| Field | Value |
| :--- | :--- |
| From | Greg KH <gregkh@linuxfoundation.org> |
| To | Author A <author-a@example.com> |
| Cc | arnd@arndb.de, kees@kernel.org, linux-kernel@vger.kernel.org, stable@vger.kernel.org |
| Date | Fri, 31 Jul 2026 13:18:05 +0200 |
| Message-ID | `2026073120-deplete-bronzing-481c@gregkh` |
| In-Reply-To | `20260718073253.96741-2-author-a@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/2026073120-deplete-bronzing-481c@gregkh/` |

---

On Sat, Jul 18, 2026 at 03:32:52PM +0800, Author A wrote:
> The ibmasm driver maps PCI BAR 0 without verifying if the hardware-provided
> resource length is sufficient to cover statically accessed registers.
> 
> When evaluating the driver against emulated hardware or during virtual
> device fuzzing, a malformed device may expose a significantly undersized
> BAR 0. This leads to an out-of-bounds (OOB) access when reading or writing
> to registers such as INTR_CONTROL_REGISTER (offset 0x13A4) or mouse
> interrupt controls (offset 0xAC000) during probe. A page fault here
> while holding the idempotent_init_module() lock causes a cascading global
> soft lockup.

In thinking about this some more, isn't that what the "authenticated
PCI" spec is for?  We trust this hardware, right?  If it gives us an
invalid BAR, bad things can and will happen.

So does this ever happen in a real device?  And where are these real
devices?  What types of systems are they and are they even still being
used?



> 
> Fix this by storing the mapped size in 'struct service_processor' and
> ensuring the BAR is at least large enough to cover the highest statically
> accessed hardware register before calling pci_ioremap_bar(). This highest
> static access (offset 0xAC1FC) occurs via the display_depth() macro
> during ibmasmfs filesystem initialization.
> 
> Fixes: bdbeed75b288 ("pci: use pci_ioremap_bar() in drivers/misc")
> Cc: stable@vger.kernel.org
> Signed-off-by: Author A <author-a@example.com>
> ---
>  drivers/misc/ibmasm/ibmasm.h   |  1 +
>  drivers/misc/ibmasm/lowlevel.h |  3 +++
>  drivers/misc/ibmasm/module.c   | 13 +++++++++++++
>  3 files changed, 17 insertions(+)
> 
> diff --git a/drivers/misc/ibmasm/ibmasm.h b/drivers/misc/ibmasm/ibmasm.h
> index a5ced88ca923..8d69198bf10f 100644
> --- a/drivers/misc/ibmasm/ibmasm.h
> +++ b/drivers/misc/ibmasm/ibmasm.h
> @@ -140,6 +140,7 @@ struct service_processor {
>  	struct list_head	node;
>  	spinlock_t		lock;
>  	void __iomem		*base_address;
> +	resource_size_t		mapped_size;
>  	unsigned int		irq;
>  	struct command		*current_command;
>  	struct command		*heartbeat;
> diff --git a/drivers/misc/ibmasm/lowlevel.h b/drivers/misc/ibmasm/lowlevel.h
> index 25f1ed07c3c5..970d30478c7b 100644
> --- a/drivers/misc/ibmasm/lowlevel.h
> +++ b/drivers/misc/ibmasm/lowlevel.h
> @@ -33,6 +33,9 @@
>  #define INTR_STATUS_REGISTER   0x13A0
>  #define INTR_CONTROL_REGISTER  0x13A4
>  
> +/* Highest static MMIO offset accessed during probe (display_depth during fs init) */
> +#define IBMASM_MAX_REG_OFFSET  0xAC1FC

Where did this value come from?


> +
>  #define SCOUT_COM_A_BASE         0x0000
>  #define SCOUT_COM_B_BASE         0x0100
>  #define SCOUT_COM_C_BASE         0x0200
> diff --git a/drivers/misc/ibmasm/module.c b/drivers/misc/ibmasm/module.c
> index 4509c15a76a8..87d4d698a5ff 100644
> --- a/drivers/misc/ibmasm/module.c
> +++ b/drivers/misc/ibmasm/module.c
> @@ -93,6 +93,19 @@ static int ibmasm_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
>  	}
>  
>  	sp->irq = pdev->irq;
> +	sp->mapped_size = pci_resource_len(pdev, 0);
> +
> +	/*
> +	 * Ensure BAR 0 is large enough to cover the highest statically
> +	 * accessed hardware register (IBMASM_MAX_REG_OFFSET).
> +	 */
> +	if (sp->mapped_size < IBMASM_MAX_REG_OFFSET + 4) {
> +		dev_err(sp->dev, "PCI BAR0 too small, need at least %zu bytes\n",
> +			(size_t)(IBMASM_MAX_REG_OFFSET + 4));

I feel like if we start taking this type of patch, you will need to do
it for EVERY PCI driver in the kernel, right?

Again, Linux trusts PCI devices, so is this even needed?

thanks,

greg k-h
