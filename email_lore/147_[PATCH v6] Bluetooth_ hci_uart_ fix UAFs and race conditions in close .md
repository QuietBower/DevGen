# [PATCH v6] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths

| Field | Value |
| :--- | :--- |
| From | <author-a-alt@example.com> |
| To | luiz.dentz@gmail.com, pmenzel@molgen.mpg.de, marcel@holtmann.org, linux-bluetooth@vger.kernel.org |
| Cc | linux-serial@vger.kernel.org, linux-kernel@vger.kernel.org, greg@kroah.com, stable@vger.kernel.org, Author A <author-a@example.com> |
| Date | Sat, 16 May 2026 13:30:36 +0800 |
| Message-ID | `20260516053036.399005-1-author-a-alt@example.com` |
| In-Reply-To | `CABBYNZ+r3gm37FW5WqE79bRp+x9UZsaCtyvfz_FdixqEucAxGw@mail.gmail.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260516053036.399005-1-author-a-alt@example.com/` |

---

From: Author A <author-a@example.com>

Vulnerabilities leading to Use-After-Free (UAF) and Null Pointer
Dereference (NPD) conditions were observed in the lifecycle management
of hci_uart.

The primary issue arises because the workqueues (init_ready and write_work)
are only flushed/cancelled if the HCI_UART_PROTO_READY flag is set during
TTY close. If a hangup occurs before setup completes, hci_uart_tty_close()
skips the teardown of these workqueues and proceeds to free the `hu` struct.
When the scheduled work executes later, it blindly dereferences the freed
`hu` struct. This issue was triggered by our custom device emulation and
fuzzing framework (DevGen) on the v6.18 kernel.

The crash trace is as follows:
  ODEBUG: free active (active state 0) object: ffff88804024e870 object type: work_struct hint: hci_uart_write_work+0x0/0x940
  WARNING: CPU: 0 PID: 338273 at lib/debugobjects.c:612 debug_print_object+0x1a2/0x2b0
  ...
  Call Trace:
   <TASK>
   debug_check_no_obj_freed+0x3ec/0x520
   kfree+0x3f0/0x6c0
   hci_uart_tty_close+0x127/0x2a0
   k_ldisc_close+0x113/0x1a0
   tty_ldisc_kill+0x8e/0x150
   tty_ldisc_hangup+0x3c1/0x730
   __tty_hangup.part.0+0x3fd/0x8a0
   tty_ioctl+0x120f/0x1690
   __x64_sys_ioctl+0x18f/0x210
   do_syscall_64+0xcb/0xfa0
   entry_SYSCALL_64_after_hwframe+0x77/0x7f
   </TASK>

Additionally, sister bugs exist in the device registration error paths
within hci_uart_init_work() and hci_uart_register_dev(). If
hci_register_dev() fails, the code frees hdev and sets hu->hdev to NULL,
but leaves hu->write_work active if it was already scheduled during early
protocol setup phase. When the work executes later, it blindly fetches and
dereferences the NULL hu->hdev pointer, triggering an unconditioned kernel
panic.

Furthermore, the error path in hci_uart_init_work() clears the
HCI_UART_PROTO_READY flag and invokes hu->proto->close(hu) without
acquiring the hu->proto_lock write lock. Since concurrent callbacks like
hci_uart_tty_receive() execute under the read lock, this missing
synchronization allows the protocol state to be destroyed while active
readers are accessing it.

Fix these synchronization and lifecycle issues by:
1. Moving the workqueue teardown to the very beginning of
   hci_uart_tty_close() and using disable_work_sync() to unconditionally
   prevent new work submissions.
2. Wrapping the clearance of HCI_UART_PROTO_READY in hci_uart_init_work()
   with percpu_down_write/percpu_up_write of hu->proto_lock.
3. Explicitly invoking disable_work_sync(&hu->write_work) before resetting
   hu->hdev to NULL in both initialization error paths to safely prevent
   any concurrent re-queuing issues.

Fixes: 3b799254cf6f ("Bluetooth: hci_uart: Cancel init work before unregistering")
Cc: stable@vger.kernel.org
Signed-off-by: Author A <author-a@example.com>
---
Changes in v6:
- Fixed missing `hu->proto_lock` write lock in hci_uart_init_work() error path to prevent race with readers (reported by Sashiko).
- Added disable_work_sync() instead of cancel_work_sync() for `hu->write_work` in hci_uart_init_work() and hci_uart_register_dev() error paths to completely block any concurrent re-queuing window before hdev is freed (reported by Sashiko).

Changes in v5:
- Relocated disable_work_sync() to the very top of hci_uart_tty_close(), 
  before hci_uart_close(), to ensure no new work is submitted during device teardown.

Changes in v4:
- Adopted Luiz's suggestion to use disable_work_sync() instead of 
  cancel_work_sync() in close path to prevent new work submissions.

Changes in v3:
- Added 'Cc: stable' tag as requested by the stable bot.

Changes in v2:
- Added KASAN/ODEBUG crash trace.

 drivers/bluetooth/hci_ldisc.c | 35 ++++++++++++++++++++++++++++++++---
 1 file changed, 32 insertions(+), 3 deletions(-)

diff --git a/drivers/bluetooth/hci_ldisc.c b/drivers/bluetooth/hci_ldisc.c
index 275ea865bc29..612fa2890cec 100644
--- a/drivers/bluetooth/hci_ldisc.c
+++ b/drivers/bluetooth/hci_ldisc.c
@@ -194,7 +194,22 @@ void hci_uart_init_work(struct work_struct *work)
 	err = hci_register_dev(hu->hdev);
 	if (err < 0) {
 		BT_ERR("Can't register HCI device");
+
+		/*
+		 * Acquire proto_lock before clearing PROTO_READY and closing
+		 * the protocol to synchronize with active readers.
+		 */
+		percpu_down_write(&hu->proto_lock);
 		clear_bit(HCI_UART_PROTO_READY, &hu->flags);
+		percpu_up_write(&hu->proto_lock);
+
+		/*
+		 * Disable any pending write_work that may have been queued
+		 * during the PROTO_INIT phase to prevent UAF/NPD when hdev
+		 * is freed.
+		 */
+		disable_work_sync(&hu->write_work);
+
 		hu->proto->close(hu);
 		hdev = hu->hdev;
 		hu->hdev = NULL;
@@ -540,6 +555,15 @@ static void hci_uart_tty_close(struct tty_struct *tty)
 	if (!hu)
 		return;
 
+	/*
+	 * Disable workqueues unconditionally before tearing down the
+	 * connection, as they might be active during the PROTO_INIT phase.
+	 * Using disable_work_sync() ensures no new submissions can occur
+	 * during or after hci_uart_close().
+	 */
+	disable_work_sync(&hu->init_ready);
+	disable_work_sync(&hu->write_work);
+
 	hdev = hu->hdev;
 	if (hdev)
 		hci_uart_close(hdev);
@@ -549,9 +573,6 @@ static void hci_uart_tty_close(struct tty_struct *tty)
 		clear_bit(HCI_UART_PROTO_READY, &hu->flags);
 		percpu_up_write(&hu->proto_lock);
 
-		cancel_work_sync(&hu->init_ready);
-		cancel_work_sync(&hu->write_work);
-
 		if (hdev) {
 			if (test_bit(HCI_UART_REGISTERED, &hu->flags))
 				hci_unregister_dev(hdev);
@@ -692,6 +713,14 @@ static int hci_uart_register_dev(struct hci_uart *hu)
 
 	if (hci_register_dev(hdev) < 0) {
 		BT_ERR("Can't register HCI device");
+
+		/*
+		 * Disable any pending write_work that may have been queued
+		 * during the proto->open() phase to prevent UAF/NPD when
+		 * hdev is freed.
+		 */
+		disable_work_sync(&hu->write_work);
+
 		percpu_down_write(&hu->proto_lock);
 		clear_bit(HCI_UART_PROTO_INIT, &hu->flags);
 		percpu_up_write(&hu->proto_lock);
-- 
2.34.1
