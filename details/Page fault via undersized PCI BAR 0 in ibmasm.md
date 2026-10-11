# 🐛 Page fault via undersized PCI BAR 0 in ibmasm

## 📌 Overview
* **Location:** `drivers/misc/ibmasm/module.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** The `ibmasm` driver maps PCI BAR 0 without verifying if the resource length is sufficient to cover the statically accessed `INTR_CONTROL_REGISTER` (offset 0x13A4). A malformed or undersized BAR 0 allows the `readl()` operation to cross the page boundary into unmapped memory, triggering a page fault during module probe. Since this occurs while holding the module loading lock, it cascades into a global system soft lockup. Fixed by validating that the BAR size is at least `INTR_CONTROL_REGISTER + 4` before invoking `pci_ioremap_bar()`.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| misc: ibmasm: Fix out-of-bounds MMIO access during module load | <u>[mail](../email/268_%5BPATCH%5D%20misc_%20ibmasm_%20Fix%20out-of-bounds%20MMIO%20access%20during%20module%20load.txt)</u> |
| Re: [PATCH] misc: ibmasm: Fix out-of-bounds MMIO access during module load | <u>[mail](../email/266_Re_%20%5BPATCH%5D%20misc_%20ibmasm_%20Fix%20out-of-bounds%20MMIO%20access%20during%20module%20.txt)</u> |
| [PATCH v2] misc: ibmasm: Fix static and dynamic out-of-bounds MMIO accesses | <u>[mail](../email/270_%5BPATCH%20v2%5D%20misc_%20ibmasm_%20Fix%20static%20and%20dynamic%20out-of-bounds%20MMIO%20acc.txt)</u> |
| Re: [PATCH v2] misc: ibmasm: Fix static and dynamic out-of-bounds MMIO accesses | <u>[mail](../email/267_Re_%20%5BPATCH%20v2%5D%20misc_%20ibmasm_%20Fix%20static%20and%20dynamic%20out-of-bounds%20MMIO.txt)</u> |
| [PATCH v3 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>[mail](../email/273_%5BPATCH%20v3%200_2%5D%20misc_%20ibmasm_%20Fix%20out-of-bounds%20MMIO%20accesses.txt)</u> |
| [PATCH v3 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>[mail](../email/274_%5BPATCH%20v3%201_2%5D%20misc_%20ibmasm_%20Fix%20static%20out-of-bounds%20MMIO%20access%20duri.txt)</u> |
| [PATCH v3 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access | <u>[mail](../email/275_%5BPATCH%20v3%202_2%5D%20misc_%20ibmasm_%20Fix%20dynamic%20out-of-bounds%20MMIO%20access.txt)</u> |
| sashiko: [PATCH v3 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>lore.kernel.org</u> |
| [PATCH v4 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>[mail](../email/277_%5BPATCH%20v4%200_2%5D%20misc_%20ibmasm_%20Fix%20out-of-bounds%20MMIO%20accesses.txt)</u> |
| [PATCH v4 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>[mail](../email/278_%5BPATCH%20v4%201_2%5D%20misc_%20ibmasm_%20Fix%20static%20out-of-bounds%20MMIO%20access%20duri.txt)</u> |
| [PATCH v4 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>[mail](../email/279_%5BPATCH%20v4%202_2%5D%20misc_%20ibmasm_%20Fix%20dynamic%20out-of-bounds%20MMIO%20access%20via.txt)</u> |
| Re:[PATCH v4 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>[mail](../email/304_Re_%5BPATCH%20v4%202_2%5D%20misc_%20ibmasm_%20Fix%20dynamic%20out-of-bounds%20MMIO%20access%20.txt)</u> |
| Re: [PATCH v4 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>[mail](../email/355_Re_%20%5BPATCH%20v4%202_2%5D%20misc_%20ibmasm_%20Fix%20dynamic%20out-of-bounds%20MMIO%20access.txt)</u> |
| [PATCH v5 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>[mail](../email/357_%5BPATCH%20v5%200_2%5D%20misc_%20ibmasm_%20Fix%20out-of-bounds%20MMIO%20accesses.txt)</u> |
| [PATCH v5 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>[mail](../email/358_%5BPATCH%20v5%201_2%5D%20misc_%20ibmasm_%20Fix%20static%20out-of-bounds%20MMIO%20access%20duri.txt)</u> |
| [PATCH v5 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>[mail](../email/359_%5BPATCH%20v5%202_2%5D%20misc_%20ibmasm_%20Fix%20dynamic%20out-of-bounds%20MMIO%20access%20via.txt)</u> |
| Re: [PATCH v4 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>[mail](../email/360_Re_%20%5BPATCH%20v4%202_2%5D%20misc_%20ibmasm_%20Fix%20dynamic%20out-of-bounds%20MMIO%20access.txt)</u> |
| [PATCH v6 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>[mail](../email/361_%5BPATCH%20v6%200_2%5D%20misc_%20ibmasm_%20Fix%20out-of-bounds%20MMIO%20accesses.txt)</u> |
| [PATCH v6 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>[mail](../email/362_%5BPATCH%20v6%201_2%5D%20misc_%20ibmasm_%20Fix%20static%20out-of-bounds%20MMIO%20access%20duri.txt)</u> |
| [PATCH v6 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>[mail](../email/363_%5BPATCH%20v6%202_2%5D%20misc_%20ibmasm_%20Fix%20dynamic%20out-of-bounds%20MMIO%20access%20via.txt)</u> |
| [PATCH v7 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>[mail](../email/364_%5BPATCH%20v7%200_2%5D%20misc_%20ibmasm_%20Fix%20out-of-bounds%20MMIO%20accesses.txt)</u> |
| [PATCH v7 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>[mail](../email/365_%5BPATCH%20v7%201_2%5D%20misc_%20ibmasm_%20Fix%20static%20out-of-bounds%20MMIO%20access%20duri.txt)</u> |
| [PATCH v7 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>[mail](../email/366_%5BPATCH%20v7%202_2%5D%20misc_%20ibmasm_%20Fix%20dynamic%20out-of-bounds%20MMIO%20access%20via.txt)</u> |
| [PATCH v8 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>[mail](../email/367_%5BPATCH%20v8%200_2%5D%20misc_%20ibmasm_%20Fix%20out-of-bounds%20MMIO%20accesses.txt)</u> |
| [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>[mail](../email/368_%5BPATCH%20v8%201_2%5D%20misc_%20ibmasm_%20Fix%20static%20out-of-bounds%20MMIO%20access%20duri.txt)</u> |
| [PATCH v8 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>[mail](../email/369_%5BPATCH%20v8%202_2%5D%20misc_%20ibmasm_%20Fix%20dynamic%20out-of-bounds%20MMIO%20access%20via.txt)</u> |
| Result-AI Reviews                                            | <u>lore.kernel.org</u> |
| Greg KH:Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>[mail](../email_lore/413_Re_%20%5BPATCH%20v8%201_2%5D%20misc_%20ibmasm_%20Fix%20static%20out-of-bounds%20MMIO%20access%20.md)</u> |
| Greg KH:Re: [PATCH v8 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>[mail](../email_lore/451_Re_%20%5BPATCH%20v8%202_2%5D%20misc_%20ibmasm_%20Fix%20dynamic%20out-of-bounds%20MMIO%20access.md)</u> |
| Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>[mail](../email/414_Re_%20%5BPATCH%20v8%201_2%5D%20misc_%20ibmasm_%20Fix%20static%20out-of-bounds%20MMIO%20access%20.txt)</u> |
| Re: [PATCH v8 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>[mail](../email/415_Re_%20%5BPATCH%20v8%202_2%5D%20misc_%20ibmasm_%20Fix%20dynamic%20out-of-bounds%20MMIO%20access.txt)</u> |
| Greg KH:Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>[mail](../email_lore/452_Re_%20%5BPATCH%20v8%201_2%5D%20misc_%20ibmasm_%20Fix%20static%20out-of-bounds%20MMIO%20access%20.md)</u> |
| Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>[mail](../email/416_Re_%20%5BPATCH%20v8%201_2%5D%20misc_%20ibmasm_%20Fix%20static%20out-of-bounds%20MMIO%20access%20.txt)</u> |
| Greg KH:Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>[mail](../email_lore/413_Re_%20%5BPATCH%20v8%201_2%5D%20misc_%20ibmasm_%20Fix%20static%20out-of-bounds%20MMIO%20access%20.md)</u> |
| [PATCH] misc: ibmasm: Remove obsolete IBM Remote Supervisor Adapter driver | <u>[mail](../email/418_%5BPATCH%5D%20misc_%20ibmasm_%20Remove%20obsolete%20IBM%20Remote%20Supervisor%20Adapter%20dr.txt)</u> |
| Arnd:Re: [PATCH] misc: ibmasm: Remove obsolete IBM Remote Supervisor Adapter driver | <u>[mail](../email/417_Re_%20%5BPATCH%5D%20misc_%20ibmasm_%20Remove%20obsolete%20IBM%20Remote%20Supervisor%20Adapte.txt)</u> |
| patch "misc: ibmasm: Remove obsolete IBM Remote Supervisor Adapter driver" added to char-misc-next | <u>[mail](../email/468_patch%20_misc_%20ibmasm_%20Remove%20obsolete%20IBM%20Remote%20Supervisor%20Adapter%20dri.txt)</u> |
| misc: ibmasm: Remove obsolete IBM Remote Supervisor Adapter driver | <u>[mail](../email/418_%5BPATCH%5D%20misc_%20ibmasm_%20Remove%20obsolete%20IBM%20Remote%20Supervisor%20Adapter%20dr.txt)</u> |

