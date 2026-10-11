# 🐛 UAFs and race conditions in hci_uart lifecycle

## 📌 Overview
* **Location:** `drivers/bluetooth/hci_ldisc.c`
* **Current Status:** ✅ **Patch Accepted**  CVE-2026-46275  **HIGH 7.8**
* **Notes:** A complex series of Use-After-Free (UAF) and Null Pointer Dereference (NPD) vulnerabilities caused by flawed lifecycle management and race conditions in the HCI UART driver. The issues occurred during concurrent TTY hangup and initialization, leading to double-frees of `tx_skb` and premature freeing of the `hu` and `hdev` structs. Fixed by strictly re-ordering flag clearance (`HCI_UART_PROTO_READY`), `cancel_work_sync()`, and protocol `close()` callbacks across all initialization and teardown paths.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] bluetooth: hci_h5: kernel panic in h5_recv (general protection fault / KASAN null-ptr-deref) via TTY ioctls (syzkaller) | <u>[mail](../email/040_%5BBUG%5D%20bluetooth_%20hci_h5_%20kernel%20panic%20in%20h5_recv%20%28general%20protection%20f.txt)</u> |
| [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[mail](../email/129_%5BPATCH%5D%20Bluetooth_%20hci_uart_%20fix%20UAF%20in%20hci_uart_tty_close%28%29.txt)</u> |
| Re: [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[mail](../email/128_Re_%20%5BPATCH%5D%20Bluetooth_%20hci_uart_%20fix%20UAF%20in%20hci_uart_tty_close%28%29.txt)</u> |
| [PATCH v2] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[mail](../email/132_%5BPATCH%20v2%5D%20Bluetooth_%20hci_uart_%20fix%20UAF%20in%20hci_uart_tty_close%28%29.txt)</u> |
| Re: [PATCH v2] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[mail](../email/134_Re_%20%5BPATCH%20v2%5D%20Bluetooth_%20hci_uart_%20fix%20UAF%20in%20hci_uart_tty_close%28%29.txt)</u> |
| [PATCH v3] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[mail](../email/138_%5BPATCH%20v3%5D%20Bluetooth_%20hci_uart_%20fix%20UAF%20in%20hci_uart_tty_close%28%29.txt)</u> |
| Re: [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[mail](../email/135_Re_%20%5BPATCH%5D%20Bluetooth_%20hci_uart_%20fix%20UAF%20in%20hci_uart_tty_close%28%29.txt)</u> |
| Re:Re: [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[mail](../email/141_Re_Re_%20%5BPATCH%5D%20Bluetooth_%20hci_uart_%20fix%20UAF%20in%20hci_uart_tty_close%28%29.txt)</u> |
| [PATCH v4] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[mail](../email/142_%5BPATCH%20v4%5D%20Bluetooth_%20hci_uart_%20fix%20UAF%20in%20hci_uart_tty_close%28%29.txt)</u> |
| Re: [PATCH v4] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[mail](../email/137_Re_%20%5BPATCH%20v4%5D%20Bluetooth_%20hci_uart_%20fix%20UAF%20in%20hci_uart_tty_close%28%29.txt)</u> |
| Re:Re: [PATCH v4] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[mail](../email/143_Re_Re_%20%5BPATCH%20v4%5D%20Bluetooth_%20hci_uart_%20fix%20UAF%20in%20hci_uart_tty_close%28%29.txt)</u> |
| [PATCH v5] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[mail](../email/144_%5BPATCH%20v5%5D%20Bluetooth_%20hci_uart_%20fix%20UAF%20in%20hci_uart_tty_close%28%29.txt)</u> |
| [PATCH v6] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[mail](../email/147_%5BPATCH%20v6%5D%20Bluetooth_%20hci_uart_%20fix%20UAFs%20and%20race%20conditions%20in%20close%20.txt)</u> |
| [PATCH v7] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[mail](../email/148_%5BPATCH%20v7%5D%20Bluetooth_%20hci_uart_%20fix%20UAFs%20and%20race%20conditions%20in%20close%20.txt)</u> |
| [PATCH v8] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[mail](../email/149_%5BPATCH%20v8%5D%20Bluetooth_%20hci_uart_%20fix%20UAFs%20and%20race%20conditions%20in%20close%20.txt)</u> |
| [PATCH v9] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[mail](../email/151_%5BPATCH%20v9%5D%20Bluetooth_%20hci_uart_%20fix%20UAFs%20and%20race%20conditions%20in%20close%20.txt)</u> |
| Re: [PATCH v9] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[mail](../email/153_Re_%20%5BPATCH%20v9%5D%20Bluetooth_%20hci_uart_%20fix%20UAFs%20and%20race%20conditions%20in%20cl.txt)</u> |
| Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[mail](../email/147_%5BPATCH%20v6%5D%20Bluetooth_%20hci_uart_%20fix%20UAFs%20and%20race%20conditions%20in%20close%20.txt)</u> |
