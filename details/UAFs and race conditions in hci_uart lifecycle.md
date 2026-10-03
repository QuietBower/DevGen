# 🐛 UAFs and race conditions in hci_uart lifecycle

## 📌 Overview
* **Location:** `drivers/bluetooth/hci_ldisc.c`
* **Current Status:** ✅ **Patch Accepted**  [CVE-2026-46275](https://www.cve.org/CVERecord?id=CVE-2026-46275)  **HIGH 7.8**
* **Notes:** A complex series of Use-After-Free (UAF) and Null Pointer Dereference (NPD) vulnerabilities caused by flawed lifecycle management and race conditions in the HCI UART driver. The issues occurred during concurrent TTY hangup and initialization, leading to double-frees of `tx_skb` and premature freeing of the `hu` and `hdev` structs. Fixed by strictly re-ordering flag clearance (`HCI_UART_PROTO_READY`), `cancel_work_sync()`, and protocol `close()` callbacks across all initialization and teardown paths.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] bluetooth: hci_h5: kernel panic in h5_recv (general protection fault / KASAN null-ptr-deref) via TTY ioctls (syzkaller) | <u>[lore.kernel.org](https://lore.kernel.org/all/1a0753ef.1084.19c22e4fde0.Coremail.25181214217@stu.xidian.edu.cn/)</u> |
| [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260513064547.352601-1-w15303746062@163.com/)</u> |
| Re: [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[lore.kernel.org](https://lore.kernel.org/all/505b56bd-e5fd-4feb-a6e3-1d8269609277@molgen.mpg.de/)</u> |
| [PATCH v2] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260514151722.382161-1-w15303746062@163.com/)</u> |
| Re: [PATCH v2] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[lore.kernel.org](https://lore.kernel.org/all/2026051510-elliptic-avatar-d819@gregkh/)</u> |
| [PATCH v3] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260515065009.383265-1-w15303746062@163.com/)</u> |
| Re: [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[lore.kernel.org](https://lore.kernel.org/all/CABBYNZLjreYY_BczAQr2G6L=iJjBYKksFp53CairG-6V0Cb0EA@mail.gmail.com/)</u> |
| Re:Re: [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[lore.kernel.org](https://lore.kernel.org/all/370fa2b5.a147.19e2bdcb7e0.Coremail.w15303746062@163.com/)</u> |
| [PATCH v4] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260515140548.393865-1-w15303746062@163.com/)</u> |
| Re: [PATCH v4] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[lore.kernel.org](https://lore.kernel.org/all/CABBYNZ+r3gm37FW5WqE79bRp+x9UZsaCtyvfz_FdixqEucAxGw@mail.gmail.com/)</u> |
| Re:Re: [PATCH v4] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[lore.kernel.org](https://lore.kernel.org/all/485d0dd5.660.19e2e71ed1e.Coremail.w15303746062@163.com/)</u> |
| [PATCH v5] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260516022200.396369-1-w15303746062@163.com/)</u> |
| [PATCH v6] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[lore.kernel.org](https://lore.kernel.org/all/20260516053036.399005-1-w15303746062@163.com/)</u> |
| [PATCH v7] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[lore.kernel.org](https://lore.kernel.org/all/20260516084727.420032-1-w15303746062@163.com/)</u> |
| [PATCH v8] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[lore.kernel.org](https://lore.kernel.org/all/20260518013602.438835-1-w15303746062@163.com/)</u> |
| [PATCH v9] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[lore.kernel.org](https://lore.kernel.org/all/20260518024949.439299-1-w15303746062@163.com/)</u> |
| Re: [PATCH v9] Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[lore.kernel.org](https://lore.kernel.org/all/177920280488.2756414.8251481561878776667.git-patchwork-notify@kernel.org/)</u> |
| Bluetooth: hci_uart: fix UAFs and race conditions in close and init paths | <u>[lore.kernel.org](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=c1bb9336ae6b54a5f6a353c4bd4ed9a4307e429b)</u> |
