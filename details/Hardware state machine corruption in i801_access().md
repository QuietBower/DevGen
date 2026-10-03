# 🐛 Hardware state machine corruption in i801_access()

## 📌 Overview
* **Location:** `drivers/i2c/busses/i2c-i801.c`
* **Current Status:** ✅ **Patch Accepted**  **CVE-2026-64205**
* **Notes:** An unconditional hardware register cleanup in the error path of `i801_access()` forcefuly clears the `INUSE_STS` lock without owning the controller. This interrupts BIOS/ACPI transactions, corrupts the hardware state machine, and triggers a console livelock/Hung Task panic. Fixed by introducing a safe `out_err` bypass.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] i2c: i801: fix hardware state machine corruption in error path | <u>lore.kernel.org</u> |
| Re: [PATCH] i2c: i801: fix hardware state machine corruption in error path | <u>lore.kernel.org</u> |
| [PATCH v2] i2c: i801: fix hardware state machine corruption in error path | <u>lore.kernel.org</u> |
| Ping:Re:[PATCH v2] i2c: i801: fix hardware state machine corruption in error path | <u>lore.kernel.org</u> |
| Re: [PATCH v2] i2c: i801: fix hardware state machine corruption in error path | <u>lore.kernel.org</u> |
| Re: [PATCH v2] i2c: i801: fix hardware state machine corruption in error path | <u>lore.kernel.org</u> |
| **i2c: i801: fix hardware state machine corruption in error path** | <u>lore.kernel.org</u> |

https://github.com/intel-lab-lkp/linux/commit/e67d702846114f60080991d51966d486b1615b49 

https://patchwork.ozlabs.org/project/linux-i2c/patch/20260512093534.348655-1-w15303746062@163.com/ 