# 🐛 UAFs and race conditions in hci_uart lifecycle

## 📌 Overview
* **Location:** `drivers/bluetooth/hci_ldisc.c`
* **Current Status:** ✅ **Patch Accepted**  CVE-2026-46275  **HIGH 7.8**
* **Notes:** A complex series of Use-After-Free (UAF) and Null Pointer Dereference (NPD) vulnerabilities caused by flawed lifecycle management and race conditions in the HCI UART driver. The issues occurred during concurrent TTY hangup and initialization, leading to double-frees of `tx_skb` and premature freeing of the `hu` and `hdev` structs. Fixed by strictly re-ordering flag clearance (`HCI_UART_PROTO_READY`), `cancel_work_sync()`, and protocol `close()` callbacks across all initialization and teardown paths.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] bluetooth: hci_h5: kernel panic in h5_recv (general protection fault / KASAN null-ptr-deref) via TTY ioctls (syzkaller) | <u>lore.kernel.org</u> |
| [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>lore.kernel.org</u> |
| Re: [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>lore.kernel.org</u> |
| [PATCH v2] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>lore.kernel.org</u> |
| Re: [PATCH v2] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>lore.kernel.org</u> |
| [PATCH v3] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>lore.kernel.org</u> |
| Re: [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>lore.kernel.org</u> |
| Re:Re: [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>lore.kernel.org</u> |
| [PATCH v4] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>lore.kernel.org</u> |
| Re: [PATCH v4] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>lore.kernel.org</u> |
| Re:Re: [PATCH v4] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>lore.kernel.org</u> |
| [PATCH v5] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>lore.kernel.org</u> |
| [PATCH v6] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>lore.kernel.org</u> |
| [PATCH v7] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>lore.kernel.org</u> |
| [PATCH v8] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>lore.kernel.org</u> |
| [PATCH v9] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>lore.kernel.org</u> |
| Re: [PATCH v9] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>lore.kernel.org</u> |
| Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>lore.kernel.org</u> |
