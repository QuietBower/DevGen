# [PATCH] net: hamachi: fix divide by zero in hamachi_init_one

| Field | Value |
| :--- | :--- |
| From | Author A <author-a@example.com> |
| To | andrew+netdev@lunn.ch, davem@davemloft.net, edumazet@google.com, kuba@kernel.org, pabeni@redhat.com |
| Cc | tglx@kernel.org, mingo@kernel.org, netdev@vger.kernel.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Sat, 18 Apr 2026 20:18:04 +0800 |
| Message-ID | `20260418121804.149171-1-author-a@example.com` |
| List-ID | <netdev.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260418121804.149171-1-author-a@example.com/` |

---

During the hardware initialization phase in hamachi_init_one(), the driver
reads the PCIClkMeas register to calculate the PCI bus frequency.

The current code attempts to prevent a divide-by-zero error using a ternary
operator: `i ? 2000/(i&0x7f) : 0`. However, this check is flawed. The highest
bit of `i` (0x80) acts as a ready flag. If unreliable hardware or a malicious
virtual device returns a value where the ready bit is set but the lower 7 bits
are zero (e.g., 0x80), the condition `i` evaluates to true, but `(i & 0x7f)`
evaluates to 0. This results in a fatal divide-by-zero exception.

This bug was discovered during an automated virtual device fuzzing campaign
testing the hardware-software trust boundary. When the hardware returns 0x80,
it bypassed the readiness while-loop but triggered the divide error. In our
tests, this panic interrupted the module loading process, further triggering
a KASAN slab-out-of-bounds in the module error path, and ultimately leading
to a multi-core soft lockup and RCU stall.

This patch fixes the issue by explicitly checking the divisor `(i & 0x7f)`
instead of the entire register value `i` before performing the division.

Signed-off-by: Author A <author-a@example.com>
---
 drivers/net/ethernet/packetengines/hamachi.c | 2 +-
 1 file changed, 1 insertion(+), 1 deletion(-)

diff --git a/drivers/net/ethernet/packetengines/hamachi.c b/drivers/net/ethernet/packetengines/hamachi.c
index b0de7e9f12a5..1d7206dd18fd 100644
--- a/drivers/net/ethernet/packetengines/hamachi.c
+++ b/drivers/net/ethernet/packetengines/hamachi.c
@@ -748,7 +748,7 @@ static int hamachi_init_one(struct pci_dev *pdev,
 	printk(KERN_INFO "%s:  %d-bit %d Mhz PCI bus (%d), Virtual Jumpers "
 		   "%2.2x, LPA %4.4x.\n",
 		   dev->name, readw(ioaddr + MiscStatus) & 1 ? 64 : 32,
-		   i ? 2000/(i&0x7f) : 0, i&0x7f, (int)readb(ioaddr + VirtualJumpers),
+		   (i & 0x7f) ? 2000 / (i & 0x7f) : 0, i & 0x7f, (int)readb(ioaddr + VirtualJumpers),
 		   readw(ioaddr + ANLinkPartnerAbility));
 
 	if (chip_tbl[hmp->chip_id].flags & CanHaveMII) {
-- 
2.34.1
