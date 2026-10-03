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
| [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>lore.kernel.org</u> |
| AI-Result                                                    | <u>lore.kernel.org</u> |
| Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>lore.kernel.org</u> |
| Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>lore.kernel.org</u> |
| Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>lore.kernel.org</u> |

