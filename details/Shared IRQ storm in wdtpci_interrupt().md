# 🐛 Shared IRQ storm in wdtpci_interrupt()

## 📌 Overview
* **Location:** `drivers/watchdog/wdt_pci.c`
* **Current Status:** 👀 **Confirmed** ❌ **WontFix**
* **Notes:** A missing interrupt origin check in a shared IRQ handler causes `wdtpci_interrupt()` to erroneously process interrupts from other devices. Under heavy load, this defeats the kernel's spurious interrupt detector, leading to a massive printk storm, RCU/Hung Task panics, and a complete system lockup. Fixed by checking the `WDC_SR_IRQ` bit in the status register and returning `IRQ_NONE` when appropriate.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/20260509121655.275311-1-w15303746062@163.com/)</u> |
| Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/f182256d-0623-4c9b-a18a-acca8457bb08@roeck-us.net/)</u> |
| Re:Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/1fc8bb5a.1346.19e14a548fb.Coremail.w15303746062@163.com/)</u> |
| Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/5acea672-0550-478e-8f49-e5e43c72d7e2@roeck-us.net/)</u> |
| Re:Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/15f8c180.3519.19e1501ebbc.Coremail.w15303746062@163.com/)</u> |
| Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/a32f5c51-1e72-4208-92db-c5a000f3d43d@roeck-us.net/)</u> |
| Re:Re: [PATCH] watchdog: wdt_pci: Fix shared IRQ storm and complete system lockup | <u>[lore.kernel.org](https://lore.kernel.org/all/403be50f.4887.19e15613e85.Coremail.w15303746062@163.com/)</u> |

