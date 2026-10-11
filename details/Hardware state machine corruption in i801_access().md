# 🐛 Hardware state machine corruption in i801_access()

## 📌 Overview
* **Location:** `drivers/i2c/busses/i2c-i801.c`
* **Current Status:** ✅ **Patch Accepted**  **CVE-2026-64205**
* **Notes:** An unconditional hardware register cleanup in the error path of `i801_access()` forcefuly clears the `INUSE_STS` lock without owning the controller. This interrupts BIOS/ACPI transactions, corrupts the hardware state machine, and triggers a console livelock/Hung Task panic. Fixed by introducing a safe `out_err` bypass.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] i2c: i801: fix hardware state machine corruption in error path | <u>[mail](../email/098_%5BPATCH%5D%20i2c_%20i801_%20fix%20hardware%20state%20machine%20corruption%20in%20error%20path.txt)</u> |
| Re: [PATCH] i2c: i801: fix hardware state machine corruption in error path | <u>[mail](../email/118_Re_%20%5BPATCH%5D%20i2c_%20i801_%20fix%20hardware%20state%20machine%20corruption%20in%20error%20.txt)</u> |
| [PATCH v2] i2c: i801: fix hardware state machine corruption in error path | <u>[mail](../email/123_%5BPATCH%20v2%5D%20i2c_%20i801_%20fix%20hardware%20state%20machine%20corruption%20in%20error%20p.txt)</u> |
| Ping:Re:[PATCH v2] i2c: i801: fix hardware state machine corruption in error path | <u>[mail](../email/210_Re_%5BPATCH%20v2%5D%20i2c_%20i801_%20fix%20hardware%20state%20machine%20corruption%20in%20erro.txt)</u> |
| Re: [PATCH v2] i2c: i801: fix hardware state machine corruption in error path | <u>[mail](../email/226_Re_%20%5BPATCH%20v2%5D%20i2c_%20i801_%20fix%20hardware%20state%20machine%20corruption%20in%20err.txt)</u> |
| Re: [PATCH v2] i2c: i801: fix hardware state machine corruption in error path | <u>[mail](../email/269_Re_%20%5BPATCH%20v2%5D%20i2c_%20i801_%20fix%20hardware%20state%20machine%20corruption%20in%20err.txt)</u> |
| **i2c: i801: fix hardware state machine corruption in error path** | <u>[mail](../email/098_%5BPATCH%5D%20i2c_%20i801_%20fix%20hardware%20state%20machine%20corruption%20in%20error%20path.txt)</u> |

https://github.com/intel-lab-lkp/linux/commit/e67d702846114f60080991d51966d486b1615b49 

https://patchwork.ozlabs.org/project/linux-i2c/patch/20260512093534.348655-1-author-a-alt@example.com/ 
