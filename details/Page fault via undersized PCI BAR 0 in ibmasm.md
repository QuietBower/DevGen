# 🐛 Page fault via undersized PCI BAR 0 in ibmasm

## 📌 Overview
* **Location:** `drivers/misc/ibmasm/module.c`
* **Current Status:** ✅ **Patch Accepted**
* **Notes:** The `ibmasm` driver maps PCI BAR 0 without verifying if the resource length is sufficient to cover the statically accessed `INTR_CONTROL_REGISTER` (offset 0x13A4). A malformed or undersized BAR 0 allows the `readl()` operation to cross the page boundary into unmapped memory, triggering a page fault during module probe. Since this occurs while holding the module loading lock, it cascades into a global system soft lockup. Fixed by validating that the BAR size is at least `INTR_CONTROL_REGISTER + 4` before invoking `pci_ioremap_bar()`.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| misc: ibmasm: Fix out-of-bounds MMIO access during module load | <u>lore.kernel.org</u> |
| Re: [PATCH] misc: ibmasm: Fix out-of-bounds MMIO access during module load | <u>lore.kernel.org</u> |
| [PATCH v2] misc: ibmasm: Fix static and dynamic out-of-bounds MMIO accesses | <u>lore.kernel.org</u> |
| Re: [PATCH v2] misc: ibmasm: Fix static and dynamic out-of-bounds MMIO accesses | <u>lore.kernel.org</u> |
| [PATCH v3 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>lore.kernel.org</u> |
| [PATCH v3 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>lore.kernel.org</u> |
| [PATCH v3 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access | <u>lore.kernel.org</u> |
| sashiko: [PATCH v3 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>lore.kernel.org</u> |
| [PATCH v4 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>lore.kernel.org</u> |
| [PATCH v4 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>lore.kernel.org</u> |
| [PATCH v4 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>lore.kernel.org</u> |
| Re:[PATCH v4 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>lore.kernel.org</u> |
| Re: [PATCH v4 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>lore.kernel.org</u> |
| [PATCH v5 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>lore.kernel.org</u> |
| [PATCH v5 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>lore.kernel.org</u> |
| [PATCH v5 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>lore.kernel.org</u> |
| Re: [PATCH v4 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>lore.kernel.org</u> |
| [PATCH v6 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>lore.kernel.org</u> |
| [PATCH v6 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>lore.kernel.org</u> |
| [PATCH v6 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>lore.kernel.org</u> |
| [PATCH v7 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>lore.kernel.org</u> |
| [PATCH v7 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>lore.kernel.org</u> |
| [PATCH v7 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>lore.kernel.org</u> |
| [PATCH v8 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses | <u>lore.kernel.org</u> |
| [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>lore.kernel.org</u> |
| [PATCH v8 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>lore.kernel.org</u> |
| Result-AI Reviews                                            | <u>lore.kernel.org</u> |
| Greg KH:Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>lore.kernel.org</u> |
| Greg KH:Re: [PATCH v8 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>lore.kernel.org</u> |
| Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>lore.kernel.org</u> |
| Re: [PATCH v8 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA | <u>lore.kernel.org</u> |
| Greg KH:Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>lore.kernel.org</u> |
| Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>lore.kernel.org</u> |
| Greg KH:Re: [PATCH v8 1/2] misc: ibmasm: Fix static out-of-bounds MMIO access during probe | <u>lore.kernel.org</u> |
| [PATCH] misc: ibmasm: Remove obsolete IBM Remote Supervisor Adapter driver | <u>lore.kernel.org</u> |
| Arnd:Re: [PATCH] misc: ibmasm: Remove obsolete IBM Remote Supervisor Adapter driver | <u>lore.kernel.org</u> |
| patch "misc: ibmasm: Remove obsolete IBM Remote Supervisor Adapter driver" added to char-misc-next | <u>lore.kernel.org</u> |
| misc: ibmasm: Remove obsolete IBM Remote Supervisor Adapter driver | <u>lore.kernel.org</u> |

