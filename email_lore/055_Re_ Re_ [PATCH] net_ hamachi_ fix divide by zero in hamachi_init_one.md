# Re: Re: [PATCH] net: hamachi: fix divide by zero in hamachi_init_one

| Field | Value |
| :--- | :--- |
| From | Author A <author-a@example.com> |
| To | "Andrew Lunn" <andrew@lunn.ch> |
| Cc | andrew+netdev@lunn.ch, davem@davemloft.net, edumazet@google.com, kuba@kernel.org, pabeni@redhat.com, tglx@kernel.org, mingo@kernel.org, netdev@vger.kernel.org, linux-kernel@vger.kernel.org |
| Date | Mon, 20 Apr 2026 10:41:32 +0800 (GMT+08:00) |
| Message-ID | `2543c18f.5194.19da8c3b8d1.Coremail.author-a@example.com` |
| In-Reply-To | `e1d937ec-d909-4553-9f1e-c89f9b556d6a@lunn.ch` |
| List-ID | <netdev.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/2543c18f.5194.19da8c3b8d1.Coremail.author-a@example.com/` |

---

Hi Andrew,

Regarding the "halt" and exploitability:
While a clean panic on bad hardware is a safe defense, this is an unhandled exception that breaks the module error-handling path. It causes memory corruption rather than a safe halt, which is dangerous for hot-pluggable VMs. The trace shows a KASAN out-of-bounds read and a subsequent soft lockup:

==================================================================
BUG: KASAN: slab-out-of-bounds in idempotent_init_module+0x54a/0x620
Read of size 8 at addr ffff8881191e7e08 by task systemd-udevd/169
...
watchdog: BUG: soft lockup - CPU#2 stuck for 245s! [systemd-udevd:174]
RIP: 0010:queued_spin_lock_slowpath+0x243/0xb90
==================================================================

Regarding reporting the attack:
You are entirely correct that the v1 patch passively masked the math error. This v2 patch actively detects the untrusted hardware state ((i & 0x80) && !(i & 0x7f)), loudly reports it via dev_err(), and safely aborts the probe with -EIO.

Signed-off-by: Author A <author-a@example.com>
---
Changes in v2:
- Addressed feedback from Andrew Lunn regarding trusting bad hardware.
- Added explicit detection for the untrusted hardware state.
- Added dev_err() to report the attack and aborted the probe with -EIO to prevent error-path memory corruption. 

 drivers/net/ethernet/packetengines/hamachi.c | 8 ++++++++
 1 file changed, 8 insertions(+)

diff --git a/drivers/net/ethernet/packetengines/hamachi.c b/drivers/net/ethernet/packetengines/hamachi.c
index b0de7e9f12a5..2f487db2fb33 100644
--- a/drivers/net/ethernet/packetengines/hamachi.c
+++ b/drivers/net/ethernet/packetengines/hamachi.c
@@ -745,6 +745,14 @@ static int hamachi_init_one(struct pci_dev *pdev,
 		   dev->name, chip_tbl[chip_id].name, readl(ioaddr + ChipRev),
 		   ioaddr, dev->dev_addr, irq);
 	i = readb(ioaddr + PCIClkMeas);
+
+	if ((i & 0x80) && !(i & 0x7f)) {
+		dev_err(&pdev->dev, "Invalid PCIClkMeas value (0x%02x), hardware untrusted.\n", i);
+		unregister_netdev(dev);
+		ret = -EIO;
+		goto err_out_unmap_rx;
+	}
+
 	printk(KERN_INFO "%s:  %d-bit %d Mhz PCI bus (%d), Virtual Jumpers "
 		   "%2.2x, LPA %4.4x.\n",
 		   dev->name, readw(ioaddr + MiscStatus) & 1 ? 64 : 32,
-- 
2.34.1

2026-04-18 23:34:26 "Andrew Lunn" <andrew@lunn.ch> 写道：
> On Sat, Apr 18, 2026 at 08:18:04PM +0800, Author A wrote:
> > During the hardware initialization phase in hamachi_init_one(), the driver
> > reads the PCIClkMeas register to calculate the PCI bus frequency.
> > 
> > The current code attempts to prevent a divide-by-zero error using a ternary
> > operator: `i ? 2000/(i&0x7f) : 0`. However, this check is flawed. The highest
> > bit of `i` (0x80) acts as a ready flag. If unreliable hardware or a malicious
> > virtual device returns a value where the ready bit is set but the lower 7 bits
> > are zero (e.g., 0x80), the condition `i` evaluates to true, but `(i & 0x7f)`
> > evaluates to 0. This results in a fatal divide-by-zero exception.
> > 
> > This bug was discovered during an automated virtual device fuzzing campaign
> > testing the hardware-software trust boundary. When the hardware returns 0x80,
> > it bypassed the readiness while-loop but triggered the divide error. In our
> > tests, this panic interrupted the module loading process, further triggering
> > a KASAN slab-out-of-bounds in the module error path, and ultimately leading
> > to a multi-core soft lockup and RCU stall.
> 
> Isn't that a good result of somebody trying to use emulated hardware
> with bad behaviour? The machine grinds to a halt? So it is not
> exploitable.
> 
> What happens with your patch in place? How are you reporting the
> hardware is attacking the machine, and the hardware should not be
> trusted?
> 
> 	Andrew
