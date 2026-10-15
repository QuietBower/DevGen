# 🐛 RCU stall via excessively long TCSBRKP duration

## 📌 Overview
* **Location:** `drivers/tty/tty_io.c`
* **Current Status:** 👀**Bug Confirmed** ❌ **WontFix**
* **Notes:** Passing a massive argument to the TCSBRKP ioctl causes an extremely long serial break condition, which keeps the IRQ line active and triggers RCU stalls.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                          | Link                                                         |
| :--------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] tty: limit TCSBRKP break duration            | <u>[lore.kernel.org](https://lore.kernel.org/all/20260727055525.6170-1-22009200607@stu.xidian.edu.cn/)</u> |
| gregkh:Re: [PATCH] tty: limit TCSBRKP break duration | <u>[lore.kernel.org](https://lore.kernel.org/all/2026072700-causal-remarry-6edf@gregkh/)</u> |
| Re: [PATCH] tty: limit TCSBRKP break duration        | <u>[lore.kernel.org](https://lore.kernel.org/all/20260729111927.41154-1-22009200607@stu.xidian.edu.cn/)</u> |
| gregkh:Re: [PATCH] tty: limit TCSBRKP break duration | <u>[lore.kernel.org](https://lore.kernel.org/all/2026072921-curled-moonlike-1207@gregkh/)</u> |
| Re: [PATCH] tty: limit TCSBRKP break duration        | <u>[lore.kernel.org](https://lore.kernel.org/all/20260731143041.6400-1-22009200607@stu.xidian.edu.cn/)</u> |
| gregkh:Re: [PATCH] tty: limit TCSBRKP break duration | <u>[lore.kernel.org](https://lore.kernel.org/all/2026073157-expend-unzip-cad7@gregkh/)</u> |

