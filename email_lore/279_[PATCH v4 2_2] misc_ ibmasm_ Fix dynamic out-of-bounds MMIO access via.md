# [PATCH v4 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA

| Field | Value |
| :--- | :--- |
| From | author-a-alt@example.com |
| To | arnd@arndb.de, gregkh@linuxfoundation.org |
| Cc | linux-kernel@vger.kernel.org, kees@kernel.org, stable@vger.kernel.org, Author A <author-a@example.com> |
| Date | Wed, 24 Jun 2026 11:24:25 +0800 |
| Message-ID | `20260624032425.384325-3-author-a-alt@example.com` |
| In-Reply-To | `20260624032425.384325-1-author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260624032425.384325-3-author-a-alt@example.com/` |

---

From: Author A <author-a@example.com>

The ibmasm driver reads dynamic Message Frame Addresses (MFA) from
hardware queues and uses them directly as offsets to dereference I2O
messages via get_i2o_message().

If a malformed or fuzzed device provides a malicious MFA, it can cause
the driver to access memory far beyond the mapped BAR, leading to an
out-of-bounds (OOB) access and potential kernel panic during runtime.

Fix this by validating the target offset against the actual mapped size
before dereferencing. This validation strictly accounts for both the
i2o_header and the dynamic payload size using safe subtraction to prevent
integer overflow bypasses.

If the bounds check fails, the invalid MFA is released back to the
inbound queue via set_mfa_inbound() to prevent a hardware mailbox deadlock.

Fixes: bdbeed75b288 ("pci: use pci_ioremap_bar() in drivers/misc")
Cc: stable@vger.kernel.org
Signed-off-by: Author A <author-a@example.com>
---
 drivers/misc/ibmasm/lowlevel.c | 30 ++++++++++++++++++++++++++----
 drivers/misc/ibmasm/lowlevel.h | 25 +++++++++++++++++++++++--
 2 files changed, 49 insertions(+), 6 deletions(-)

diff --git a/drivers/misc/ibmasm/lowlevel.c b/drivers/misc/ibmasm/lowlevel.c
index 5313230f36ad..68dd493a7953 100644
--- a/drivers/misc/ibmasm/lowlevel.c
+++ b/drivers/misc/ibmasm/lowlevel.c
@@ -9,7 +9,6 @@
 
 #include "ibmasm.h"
 #include "lowlevel.h"
-#include "i2o.h"
 #include "dot_command.h"
 #include "remote.h"
 
@@ -34,7 +33,13 @@ int ibmasm_send_i2o_message(struct service_processor *sp)
 		return 1;
 
 	header.message_size = outgoing_message_size((unsigned int)command_size);
-	message = get_i2o_message(sp->base_address, mfa);
+	message = get_i2o_message(sp->base_address, sp->mapped_size, mfa,
+				  sizeof(struct i2o_header) + command_size);
+	if (!message) {
+		/* Release the allocated inbound MFA to prevent mailbox deadlock */
+		set_mfa_inbound(sp->base_address, mfa);
+		return 1;
+	}
 
 	memcpy_toio(&message->header, &header, sizeof(struct i2o_header));
 	memcpy_toio(&message->data, command->buffer, command_size);
@@ -63,8 +68,25 @@ irqreturn_t ibmasm_interrupt_handler(int irq, void * dev_id)
 
 	mfa = get_mfa_outbound(base_address);
 	if (valid_mfa(mfa)) {
-		struct i2o_message *msg = get_i2o_message(base_address, mfa);
-		ibmasm_receive_message(sp, &msg->data, incoming_data_size(msg));
+		struct i2o_message *msg = get_i2o_message(base_address,
+							  sp->mapped_size, mfa,
+							  sizeof(struct i2o_header));
+		if (msg) {
+			u32 data_size = incoming_data_size(msg);
+			u32 offset = GET_MFA_ADDR(mfa);
+
+			/*
+			 * Secondary check for dynamic payload size.
+			 * Use subtraction to perfectly prevent integer overflow.
+			 */
+			if (unlikely(data_size > sp->mapped_size - offset -
+						 sizeof(struct i2o_header)))
+				dbg("received mfa payload out of bounds\n");
+			else
+				ibmasm_receive_message(sp, &msg->data, data_size);
+		} else {
+			dbg("received mfa header out of bounds\n");
+		}
 	} else
 		dbg("didn't get a valid MFA\n");
 
diff --git a/drivers/misc/ibmasm/lowlevel.h b/drivers/misc/ibmasm/lowlevel.h
index 545dfe384117..c0f08106c52c 100644
--- a/drivers/misc/ibmasm/lowlevel.h
+++ b/drivers/misc/ibmasm/lowlevel.h
@@ -13,6 +13,9 @@
 #define __IBMASM_CONDOR_H__
 
 #include <asm/io.h>
+#include <linux/compiler.h>
+#include <linux/types.h>
+#include "i2o.h"
 
 #define VENDORID_IBM	0x1014
 #define DEVICEID_RSA	0x010F
@@ -118,9 +121,27 @@ static inline void set_mfa_inbound(void __iomem *base_address, u32 mfa)
 	writel(mfa, base_address + INBOUND_QUEUE_PORT);
 }
 
-static inline struct i2o_message *get_i2o_message(void __iomem *base_address, u32 mfa)
+/**
+ * get_i2o_message - Convert MFA to i2o_message pointer with bounds check
+ * @base_address: BAR 0 virtual address
+ * @mapped_size:  actual size of BAR 0 mapping
+ * @mfa:          Message Frame Address from hardware
+ * @msg_size:     Required size of the message (header + payload)
+ *
+ * Returns NULL if the offset derived from @mfa does not fit within
+ * the mapped BAR (including the i2o_message header).
+ */
+static inline struct i2o_message *get_i2o_message(void __iomem *base_address,
+						  resource_size_t mapped_size,
+						  u32 mfa, size_t msg_size)
 {
-	return (struct i2o_message *)(GET_MFA_ADDR(mfa) + base_address);
+	u32 offset = GET_MFA_ADDR(mfa);
+
+	/* Prevent read/write beyond the ioremap region and avoid integer underflow/overflow */
+	if (unlikely(offset > mapped_size || msg_size > mapped_size - offset))
+		return NULL;
+
+	return (struct i2o_message *)(offset + base_address);
 }
 
 #endif /* __IBMASM_CONDOR_H__ */
-- 
2.34.1
