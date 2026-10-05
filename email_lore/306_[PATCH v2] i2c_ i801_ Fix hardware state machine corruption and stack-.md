# [PATCH v2] i2c: i801: Fix hardware state machine corruption and stack-out-of-bounds

| Field | Value |
| :--- | :--- |
| From | Author A <author-a@example.com> |
| To | jdelvare@suse.com, andi.shyti@kernel.org |
| Cc | wsa@kernel.org, hkallweit1@gmail.com, djkurtz@chromium.org, linux-i2c@vger.kernel.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com>, stable@vger.kernel.org |
| Date | Sat, 27 Jun 2026 15:58:04 +0800 |
| Message-ID | `20260627075804.478990-1-author-a@example.com` |
| List-ID | <linux-i2c.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260627075804.478990-1-author-a@example.com/` |

---

Under extreme conditions (e.g., fault injection or transaction timeouts),
the i801 SMBus controller driver exhibits two error handling issues:

1. When i801_check_pre() fails, the driver jumps to the 'out' label
   and clears the INUSE_STS and status flags without holding hardware
   ownership, corrupting the hardware state machine.

2. When a transaction aborts and returns from i801_access(), the
   stack-allocated union i2c_smbus_data is destroyed. However,
   priv->data is not cleared. A spurious interrupt can then trigger a
   stack-out-of-bounds read in i801_isr_byte_done(), caught by KASAN:

  BUG: KASAN: stack-out-of-bounds in i801_isr_byte_done drivers/i2c/busses/i2c-i801.c:592 [inline]
  BUG: KASAN: stack-out-of-bounds in i801_isr drivers/i2c/busses/i2c-i801.c:648 [inline]
  Read of size 1 at addr ffff8881026dfd91 by task in:imklog/218

  CPU: 2 UID: 0 PID: 218 Comm: in:imklog Tainted: G        W        N  7.1.0+ #1
  Call Trace:
   <IRQ>
   ...
   kasan_report+0xca/0x100 mm/kasan/report.c:595
   i801_isr_byte_done drivers/i2c/busses/i2c-i801.c:592 [inline]
   i801_isr drivers/i2c/busses/i2c-i801.c:648 [inline]
   __handle_irq_event_percpu+0x222/0x830 kernel/irq/handle.c:209
   ...
   </IRQ>

Fix these issues by:
- Bypassing hardware register cleanup if i801_check_pre() fails.
- Using WRITE_ONCE() to clear priv->data in the exit path, paired with
  READ_ONCE() in the ISR.
- Calling synchronize_irq() conditionally on the error path before
  returning to ensure any in-flight ISR finishes before stack memory
  is reclaimed.

Fixes: 1f760b87e54c ("i2c: i801: Call i801_check_pre() from i801_access()")
Fixes: d3ff6ce40031 ("i2c-i801: Enable IRQ for byte_by_byte transactions")
Cc: stable@vger.kernel.org
Signed-off-by: Author A <author-a@example.com>
---
Changes in v2:
 - Split error paths to bypass hardware register cleanup on pre-check
   failure.
 - Added READ_ONCE() check in ISR to prevent OOB access.
 - Conditionally use WRITE_ONCE() and synchronize_irq() on the error
   path to prevent race condition without performance regression.

 drivers/i2c/busses/i2c-i801.c | 20 +++++++++++++++++++-
 1 file changed, 19 insertions(+), 1 deletion(-)

diff --git a/drivers/i2c/busses/i2c-i801.c b/drivers/i2c/busses/i2c-i801.c
index 32a3cef02c7b..a14d26ee938d 100644
--- a/drivers/i2c/busses/i2c-i801.c
+++ b/drivers/i2c/busses/i2c-i801.c
@@ -562,6 +562,13 @@ static int i801_block_transaction_by_block(struct i801_priv *priv,
 
 static void i801_isr_byte_done(struct i801_priv *priv)
 {
+	/*
+	 * Use READ_ONCE to prevent compiler optimization and ensure
+	 * visibility of the cleared pointer from the process context.
+	 */
+	if (unlikely(!READ_ONCE(priv->data)))
+		return;
+
 	if (priv->is_read) {
 		/*
 		 * At transfer start i801_smbus_block_transaction() marks
@@ -905,7 +912,7 @@ static s32 i801_access(struct i2c_adapter *adap, u16 addr,
 
 	ret = i801_check_pre(priv);
 	if (ret)
-		goto out;
+		goto out_err;
 
 	hwpec = (priv->features & FEATURE_SMBUS_PEC) && (flags & I2C_CLIENT_PEC)
 		&& size != I2C_SMBUS_QUICK
@@ -938,6 +945,17 @@ static s32 i801_access(struct i2c_adapter *adap, u16 addr,
 	 */
 	iowrite8(SMBHSTSTS_INUSE_STS | STATUS_FLAGS, SMBHSTSTS(priv));
 
+out_err:
+	/*
+	 * Prevent UAF/OOB in the ISR.
+	 * For successful transactions, the ISR has completed. For aborted or
+	 * timed-out transactions, flush any in-flight interrupts before
+	 * destroying the stack-allocated data.
+	 */
+	WRITE_ONCE(priv->data, NULL);
+	if (unlikely(ret != 0 && (priv->features & FEATURE_IRQ)))
+		synchronize_irq(priv->pci_dev->irq);
+
 	pm_runtime_put_autosuspend(&priv->pci_dev->dev);
 	mutex_unlock(&priv->acpi_lock);
 	return ret;
-- 
2.34.1
