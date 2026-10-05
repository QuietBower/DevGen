# Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup

| Field | Value |
| :--- | :--- |
| From | Guenter Roeck <linux@roeck-us.net> |
| To | author-a-alt <author-a-alt@example.com> |
| Cc | wim@linux-watchdog.org, linux-watchdog@vger.kernel.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Sun, 10 May 2026 20:00:47 -0700 |
| Message-ID | `5acea672-0550-478e-8f49-e5e43c72d7e2@roeck-us.net` |
| In-Reply-To | `1fc8bb5a.1346.19e14a548fb.Coremail.author-a-alt@example.com` |
| List-ID | <linux-watchdog.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/5acea672-0550-478e-8f49-e5e43c72d7e2@roeck-us.net/` |

---

Hi,

On 5/10/26 18:27, author-a-alt wrote:
> 
> 
> 
> From: Author A <author-a@example.com>
> 
> Hi Guenter,
> 
> Thank you for your prompt response and review.
> 
> To answer your questions directly and transparently:
> 
> 1. Is this an actual observed problem?
> Yes, it is a real, observed problem. However, it was observed in a virtualized fuzzing environment (QEMU + Syzkaller) rather than on physical legacy hardware.
> 
> 2. How was it triggered?
> In our QEMU setup, PCI IRQ lines are heavily shared. The fuzzer loaded the `wdt_pci` driver while simultaneously fuzzing other devices on the same shared IRQ line (e.g., the i2c-i801 controller). When the other device triggered a heavy interrupt load, `wdtpci_interrupt()` caught them. Since it bypassed the IRQ ownership check, it blindly claimed the interrupts and caused a massive printk storm (spamming "wdt_pci: Reset in 5ms" and "status 114").
> 
> This overwhelmed the CPU in hard IRQ context, defeated the spurious IRQ detector, and resulted in a 145-second Hung Task panic. Here is a brief snippet of the observed log:
> 
> [  375.485491] wdt_pci: Reset in 5ms
> [  375.487467] wdt_pci: status 114
> [  375.489171] wdt_pci: Reset in 5ms
> ...
> [  375.484244] systemd-journald[4771]: /dev/kmsg buffer overrun, some messages lost.
> [  519.189528] INFO: task syz.2.507 blocked for more than 145 seconds.
> 
> 3. Did I confirm the register bit?
> Yes, the assertion that `WDC_SR_IRQ` is active low relies directly on the hardware definition documented in the driver's own source code at line 66:
> `#define WDC_SR_IRQ      128 /* Active low */ /* X   X   X  */`
> 
> I completely understand your perspective. This is legacy hardware from the 1990s, and it is extremely unlikely to be used in production today. My intention was solely to report a reproducible Local DoS vector found by the fuzzer, as `IRQF_SHARED` handlers are strictly required to verify their interrupt source.
> 
> If you feel that patching this outdated driver adds unnecessary churn and consumes maintainers' time, please feel free to drop this patch. Alternatively, if the hardware is truly obsolete, perhaps the driver should be marked as BROKEN or removed entirely. I leave that entirely to your expert judgment.
> 

I don't see that watchdog supported in qemu. Is this a downstream version
of qemu, or am I missing something ?

Thanks,
Guenter
