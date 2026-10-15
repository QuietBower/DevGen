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
| [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>[lore.kernel.org](https://lore.kernel.org/all/20260720053733.102186-1-25181214217@stu.xidian.edu.cn/)</u> |
| AI-Result                                                    | <u>[lore.kernel.org](https://sashiko.dev/#/patchset/20260720053733.102186-1-25181214217%40stu.xidian.edu.cn)</u> |
| Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>[lore.kernel.org](https://lore.kernel.org/all/0d58c9fe-7aed-43dd-8d00-6edfee9e2876@kernel.org/)</u> |
| Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>[lore.kernel.org](https://lore.kernel.org/all/706101a4-abf9-403d-a2cb-778a12338d99@stu.xidian.edu.cn/)</u> |
| Re: [PATCH] serial: 8250: validate iomem_base in serial8250_verify_port() | <u>[lore.kernel.org](https://lore.kernel.org/all/2026072015-ammonium-dismount-7af5@gregkh/)</u> |

