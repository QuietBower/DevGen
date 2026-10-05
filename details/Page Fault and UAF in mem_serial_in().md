# 🐛 Page Fault and UAF in mem_serial_in()

> **Resolution:** This is an architectural trade-off. The kernel prioritizes the preservation of legacy hardware management ABIs over providing comprehensive fault isolation for privileged, albeit invalid, hardware mapping requests.

## 📌 Overview

* **Location:** `drivers/tty/serial/8250/8250_port.c`
* **Current Status:** 🆕 **Bug Confirmed** ❌ **WontFix**
* **Notes:** Allowing arbitrary iomem_base injection via the TIOCSSERIAL ioctl causes a bogus memory mapping, triggering a fatal Page Fault in mem_serial_in(). This crash orphans the tty_lock, ultimately leading to a slab-use-after-free in mutex_spin_on_owner().

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>[mail](../email_lore/370_%5BPATCH%5D%20serial_%208250_%20validate%20iomem_base%20in%20serial8250_verify_port%28%29.md)</u> |
| AI-Result                                                    | <u>lore.kernel.org</u> |
| Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>[mail](../email_lore/437_Re_%20%5BPATCH%5D%20serial_%208250_%20validate%20iomem_base%20in%20serial8250_verify_por.md)</u> |
| Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>[mail](../email_lore/372_Re_%20%5BPATCH%5D%20serial_%208250_%20validate%20iomem_base%20in%20serial8250_verify_por.md)</u> |
| Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>[mail](../email_lore/441_Re_%20%5BPATCH%5D%20serial_%208250_%20validate%20iomem_base%20in%20serial8250_verify_por.md)</u> |

