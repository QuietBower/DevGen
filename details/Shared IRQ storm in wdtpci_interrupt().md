# 🐛 Shared IRQ storm in wdtpci_interrupt()

## 📌 Overview
* **Location:** `drivers/watchdog/wdt_pci.c`
* **Current Status:** 👀 **Confirmed** ❌ **WontFix**
* **Notes:** A missing interrupt origin check in a shared IRQ handler causes `wdtpci_interrupt()` to erroneously process interrupts from other devices. Under heavy load, this defeats the kernel's spurious interrupt detector, leading to a massive printk storm, RCU/Hung Task panics, and a complete system lockup. Fixed by checking the `WDC_SR_IRQ` bit in the status register and returning `IRQ_NONE` when appropriate.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>lore.kernel.org</u> |
| Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>lore.kernel.org</u> |
| Re:Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>lore.kernel.org</u> |
| Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>lore.kernel.org</u> |
| Re:Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>lore.kernel.org</u> |
| Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>lore.kernel.org</u> |
| Re:Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>lore.kernel.org</u> |

