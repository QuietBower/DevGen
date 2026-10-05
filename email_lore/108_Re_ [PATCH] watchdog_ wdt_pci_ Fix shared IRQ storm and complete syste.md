# Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup

| Field | Value |
| :--- | :--- |
| From | Guenter Roeck <linux@roeck-us.net> |
| To | author-a-alt <author-a-alt@example.com> |
| Cc | wim@linux-watchdog.org, linux-watchdog@vger.kernel.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Sun, 10 May 2026 21:39:26 -0700 |
| Message-ID | `a32f5c51-1e72-4208-92db-c5a000f3d43d@roeck-us.net` |
| In-Reply-To | `15f8c180.3519.19e1501ebbc.Coremail.author-a-alt@example.com` |
| List-ID | <linux-watchdog.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/a32f5c51-1e72-4208-92db-c5a000f3d43d@roeck-us.net/` |

---

Hi,

On 5/10/26 20:08, author-a-alt wrote:
> 
> 
> From: Author A <author-a@example.com>
> 
> Hi Guenter,
> 
> You are correct; standard QEMU does not support the WDT500/501.
> 
> We observed this issue using DevGen, an automated framework we are developing. It synthesizes QEMU virtual device models directly from Linux driver source code using LLM guidance, allowing us to fuzz legacy or obscure drivers that lack physical hardware or official emulators.
> 
> Because the synthesized device is an imperfect model, it generated unexpected interrupt loads on a heavily shared PCI IRQ line (shared with the i2c-i801 controller in our setup). However, this successfully exposed a real logic flaw: the `wdt_pci` driver registers an `IRQF_SHARED` handler but fails to verify the `WDC_SR_IRQ` bit. It erroneously claimed the interrupts, bypassed the kernel's spurious IRQ defenses, and caused a livelock.
> 
> I understand this hardware is essentially obsolete. The patch provides a purely defensive fix for a legitimate API violation (unverified shared IRQ).
> 
> If applying this adds unnecessary churn, please feel free to drop it, or perhaps consider marking the driver as BROKEN. I leave it entirely to your judgment.
> 

That is entirely not the problem. The problem is that your AI implemented
a QEMU emulation without really knowing what the hardware is doing while
guessing what status bits unused by the driver mean for the real hardware.
We (and the AI) actually don't know what the WDC_SR_IRQ does, or if it
is indeed low active. That is just a guess. I can not accept a patch
based on a guess, much less one making unconfirmed claims.

If the driver is indeed unused or obsolete is a completely different
question. Marking it as BROKEN without real evidence that it is indeed
broken, just because some AI believes that it is broken, would be
inappropriate.

Please note that I also find the patch description inappropriate and
misleading. It suggests that the problem was observed on real hardware,
while it was actually observed with a qemu implementation implemented by AI
based on the driver itself, including guesswork about status bits unused
by the driver.

Guenter
