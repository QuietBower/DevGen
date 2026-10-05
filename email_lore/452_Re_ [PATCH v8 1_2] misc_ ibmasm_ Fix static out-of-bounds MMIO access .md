# Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe

| Field | Value |
| :--- | :--- |
| From | Greg KH <gregkh@linuxfoundation.org> |
| To | Author A <author-a@example.com> |
| Cc | arnd@arndb.de, kees@kernel.org, linux-kernel@vger.kernel.org, stable@vger.kernel.org |
| Date | Fri, 31 Jul 2026 16:17:48 +0200 |
| Message-ID | `2026073114-staff-turbine-9864@gregkh` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/2026073114-staff-turbine-9864@gregkh/` |

---

On Fri, Jul 31, 2026 at 09:57:15PM +0800, Author A wrote:
> Hi Greg,
> 
> Thanks for the review.
> 
> > In thinking about this some more, isn't that what the "authenticated
> > PCI" spec is for?  We trust this hardware, right?  If it gives us an
> > invalid BAR, bad things can and will happen.
> > 
> > So does this ever happen in a real device?  And where are these real
> > devices?  What types of systems are they and are they even still being
> > used?
> > 
> You are right, a physical IBM RSA device would likely never expose an
> undersized BAR. We found this crash using an automated QEMU device
> fuzzer. In a traditional bare-metal environment, the kernel's trust
> in PCI hardware is well understood.
> > Where did this value come from?
> 
> It comes from the `display_depth()` macro used during ibmasmfs
> initialization (`drivers/misc/ibmasm/ibmasmfs.c`). It calls
> `remote_display_depth(sp)`, which reads from:
> `sp->base_address + SCOUT_COM_C_BASE + 0x1fc`
> 
> Since `SCOUT_COM_C_BASE` is defined as `0xAC000` (in lowlevel.h),
> the highest fixed offset accessed statically is `0xAC1FC`.
> 
> > I feel like if we start taking this type of patch, you will need to do
> > it for EVERY PCI driver in the kernel, right?
> > 
> > Again, Linux trusts PCI devices, so is this even needed?
> > 
> You make a very fair point. We definitely do not intend to patch
> every PCI driver in the kernel.
> 
> We targeted this specific case because of the severity of the crash
> during `probe()`. Since the driver unconditionally accesses offset
> `0xAC1FC` (approx 705KB) without checking the BAR length, an
> undersized BAR provided by the fuzzer causes a page fault while
> holding the `idempotent_init_module()` lock, leading to a global
> soft lockup.
> 
> However, I completely agree with your core argument: adding defensive
> checks against untrusted/fake hardware into obsolete drivers is not
> a scalable strategy for the kernel.
> 
> If this hardware is truly dead, maintaining these edge-case patches
> is unnecessary. Would you prefer I drop this patch series and submit
> a single patch to remove the `ibmasm` driver entirely?

I do not know.  Do some research into when the hardware was made, what
it runs on, and try to determine if it really is even used anymore by
anyone.  If not, then yes, let's drop it!

thanks,

greg k-h
