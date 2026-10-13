# 🐛 RCU stall via excessively long TCSBRKP duration

## 📌 Overview
* **Location:** `drivers/tty/tty_io.c`
* **Current Status:** 👀**Bug Confirmed** ❌ **WontFix**
* **Notes:** Passing a massive argument to the TCSBRKP ioctl causes an extremely long serial break condition, which keeps the IRQ line active and triggers RCU stalls.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                          | Link                                                         |
| :--------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] tty: limit TCSBRKP break duration | <u>[mail](../email/497_%5BPATCH%5D%20tty_%20limit%20TCSBRKP%20break%20duration.txt)</u> |
| gregkh:Re: [PATCH] tty: limit TCSBRKP break duration | <u>[mail](../email/496_Re_%20%5BPATCH%5D%20tty_%20limit%20TCSBRKP%20break%20duration.txt)</u> |
| Re: [PATCH] tty: limit TCSBRKP break duration | <u>[mail](../email/498_Re_%20%5BPATCH%5D%20tty_%20limit%20TCSBRKP%20break%20duration.txt)</u> |
| gregkh:Re: [PATCH] tty: limit TCSBRKP break duration | <u>[mail](../email/499_Re_%20%5BPATCH%5D%20tty_%20limit%20TCSBRKP%20break%20duration.txt)</u> |
| Re: [PATCH] tty: limit TCSBRKP break duration | <u>[mail](../email/503_Re_%20%5BPATCH%5D%20tty_%20limit%20TCSBRKP%20break%20duration.txt)</u> |
| gregkh:Re: [PATCH] tty: limit TCSBRKP break duration | <u>[mail](../email/504_Re_%20%5BPATCH%5D%20tty_%20limit%20TCSBRKP%20break%20duration.txt)</u> |

