# Re: [PATCH] char: agp: amd64 - fix null-ptr-deref in amd64_fetch_size and related functions

| Field | Value |
| :--- | :--- |
| From | sashiko-bot@kernel.org |
| To | author-a-alt@example.com |
| Cc | linux-pci@vger.kernel.org |
| Date | Mon, 04 May 2026 07:34:07 +0000 |
| Message-ID | `20260504073407.A8A35C2BCB8@smtp.kernel.org` |
| In-Reply-To | `20260504065441.99033-1-author-a-alt@example.com` |
| List-ID | <linux-pci.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/20260504073407.A8A35C2BCB8@smtp.kernel.org/` |

---

Thank you for your contribution! Sashiko AI review found 2 potential issue(s) to consider:
- [High] The patch masks a kernel crash symptom without fixing the underlying broken error propagation in `agp_amd64_probe()`, allowing the driver to erroneously proceed without hardware.
- [Low] The newly added NULL pointer safeguards across multiple initialization and cleanup functions are mathematically redundant or structurally unreachable.
--

commit 97080eec153aeef89c2ae80f04042c9b13cb0fb4
Author: Author A <author-a@example.com>

char: agp: amd64 - fix null-ptr-deref in amd64_fetch_size and related functions

This patch adds safeguards across the AMD64 AGP driver to verify that
node_to_amd_nb() and its misc pointer are not null. It attempts to fix a
kernel panic in environments without a physical AMD Northbridge.

> diff --git a/drivers/char/agp/amd64-agp.c b/drivers/char/agp/amd64-agp.c
> --- a/drivers/char/agp/amd64-agp.c
> +++ b/drivers/char/agp/amd64-agp.c
> @@ -121,14 +121,16 @@ static struct aper_size_info_32 amd64_aperture_sizes[7] =
>  static int amd64_fetch_size(void)
>  {
>  	struct pci_dev *dev;
> +	struct amd_northbridge *nb;
>  	int i;
>  	u32 temp;
>  	struct aper_size_info_32 *values;
>  
> -	dev = node_to_amd_nb(0)->misc;
> -	if (dev==NULL)
> +	nb = node_to_amd_nb(0);
> +	if (!nb || !nb->misc)
>  		return 0;

Does returning 0 here merely mask the underlying issue?

The patch prevents a crash by handling the missing hardware here, but this
still allows the driver to erroneously proceed without hardware. Should
agp_amd64_probe() be fixed to abort on cache_nbs() < 0 instead of just
checking for == -1, so the error propagates properly when hardware is
missing?

[ ... ]

> @@ -187,8 +189,12 @@ static int amd_8151_configure(void)
>  
>  	/* Configure AGP regs in each x86-64 host bridge. */
>  	for (i = 0; i < amd_nb_num(); i++) {
> -		agp_bridge->gart_bus_addr =
> -			amd64_configure(node_to_amd_nb(i)->misc, gatt_bus);
> +		struct amd_northbridge *nb = node_to_amd_nb(i);
> +
> +		if (!nb || !nb->misc)
> +			continue;

Are these safeguards necessary inside the loop? 

Since the loop is bounded by amd_nb_num(), isn't the Northbridge descriptor
and its misc pointer guaranteed to be non-NULL if amd_nb_num() is greater
than zero? This applies to amd_8151_configure(), amd64_cleanup(), and
cache_nbs().

[ ... ]

> @@ -391,6 +409,7 @@ static int uli_agp_init(struct pci_dev *pdev)
>  {
>  	u32 httfea,baseaddr,enuscr;
>  	struct pci_dev *dev1;
> +	struct amd_northbridge *nb;
>  	int i, ret;
>  	unsigned size = amd64_fetch_size();
>  
> @@ -411,9 +430,14 @@ static int uli_agp_init(struct pci_dev *pdev)
>  		goto put;
>  	}
>  
> +	nb = node_to_amd_nb(0);
> +	if (!nb || !nb->misc) {
> +		ret = -ENODEV;
> +		goto put;
> +	}

Is this check structurally reachable?

If amd64_fetch_size() returned 0 earlier in this function due to missing
hardware, the array lookup for uli_sizes will fail. This causes an early
-ENODEV exit before ever reaching this new check. The same unreachable
logic appears to apply to the changes in nforce3_agp_init().

-- 
Sashiko AI review · `[ANONYMIZED] https://sashiko.dev/#/patchset/20260504065441.99033-1-author-a-alt@example.com?part=1 (author-address patchset permalink)`
