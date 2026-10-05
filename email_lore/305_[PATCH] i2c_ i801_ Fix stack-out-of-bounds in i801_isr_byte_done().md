# [PATCH] i2c: i801: Fix stack-out-of-bounds in i801_isr_byte_done()

| Field | Value |
| :--- | :--- |
| From | Author A <author-a@example.com> |
| To | Jean Delvare <jdelvare@suse.com>, Andi Shyti <andi.shyti@kernel.org> |
| Cc | Daniel Kurtz <djkurtz@chromium.org>, linux-i2c@vger.kernel.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Sat, 27 Jun 2026 11:17:07 +0800 |
| Message-ID | `20260627031707.473833-1-author-a@example.com` |
| List-ID | <linux-i2c.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260627031707.473833-1-author-a@example.com/` |

---

Under extreme conditions (e.g., fault injection), the i801 SMBus controller
may experience transaction timeouts. The driver aborts the operation, sends
a KILL command, and returns from i801_access(), which destroys the
stack-allocated union i2c_smbus_data.

However, priv->data is not explicitly cleared. If a delayed or spurious
interrupt (e.g., BYTE_DONE) occurs later, i801_isr_byte_done() blindly
dereferences this dangling pointer, causing a KASAN stack-out-of-bounds
crash:

  BUG: KASAN: stack-out-of-bounds in i801_isr_byte_done
  Read of size 1 at addr ffff8881026dfd91 by task in:imklog/218
  Call Trace:
   <IRQ>
   ...
   kasan_report+0xca/0x100
   i801_isr_byte_done
   i801_isr
   __handle_irq_event_percpu+0x222/0x830
   ...
   </IRQ>

Fix this by clearing priv->data in the unified exit path of
i801_access(), and adding a sanity check in i801_isr_byte_done() to
ignore interrupts when priv->data is NULL.

Fixes: d3ff6ce40031 ("i2c-i801: Enable IRQ for byte_by_byte transactions")
Signed-off-by: Author A <author-a@example.com>
---
 drivers/i2c/busses/i2c-i801.c | 15 +++++++++++++++
 1 file changed, 15 insertions(+)

diff --git a/drivers/i2c/busses/i2c-i801.c b/drivers/i2c/busses/i2c-i801.c
index 32a3cef02c7b..6631b6db783b 100644
--- a/drivers/i2c/busses/i2c-i801.c
+++ b/drivers/i2c/busses/i2c-i801.c
@@ -562,6 +562,14 @@ static int i801_block_transaction_by_block(struct i801_priv *priv,
 
 static void i801_isr_byte_done(struct i801_priv *priv)
 {
+	/*
+	 * Bail out if the transaction has already timed out or completed.
+	 * This prevents a Use-After-Free/Stack-Out-Of-Bounds when a delayed
+	 * or spurious interrupt occurs after the stack memory is reclaimed.
+	 */
+	if (unlikely(!priv->data))
+		return;
+
 	if (priv->is_read) {
 		/*
 		 * At transfer start i801_smbus_block_transaction() marks
@@ -936,6 +944,13 @@ static s32 i801_access(struct i2c_adapter *adap, u16 addr,
 	 * Unlock the SMBus device for use by BIOS/ACPI,
 	 * and clear status flags if not done already.
 	 */
+
+	/*
+	 * Clear the data pointer to prevent stale pointer dereference
+	 * in the ISR if a spurious interrupt fires later.
+	 */
+	priv->data = NULL;
+
 	iowrite8(SMBHSTSTS_INUSE_STS | STATUS_FLAGS, SMBHSTSTS(priv));
 
 	pm_runtime_put_autosuspend(&priv->pci_dev->dev);
-- 
2.34.1
