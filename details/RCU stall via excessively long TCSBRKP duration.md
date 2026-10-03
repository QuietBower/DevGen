# 🐛 RCU stall via excessively long TCSBRKP duration

## 📌 Overview
* **Location:** `drivers/tty/tty_io.c`
* **Current Status:** 👀**Bug Confirmed** ❌ **WontFix**
* **Notes:** Passing a massive argument to the TCSBRKP ioctl causes an extremely long serial break condition, which keeps the IRQ line active and triggers RCU stalls.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                          | Link                                                         |
| :--------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] tty: limit TCSBRKP break duration            | <u>lore.kernel.org</u> |
| gregkh:Re: [PATCH] tty: limit TCSBRKP break duration | <u>lore.kernel.org</u> |
| Re: [PATCH] tty: limit TCSBRKP break duration        | <u>lore.kernel.org</u> |
| gregkh:Re: [PATCH] tty: limit TCSBRKP break duration | <u>lore.kernel.org</u> |
| Re: [PATCH] tty: limit TCSBRKP break duration        | <u>lore.kernel.org</u> |
| gregkh:Re: [PATCH] tty: limit TCSBRKP break duration | <u>lore.kernel.org</u> |

