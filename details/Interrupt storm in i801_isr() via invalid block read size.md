# 🐛 Interrupt storm in i801_isr() via invalid block read size

## 📌 Overview
* **Location:** `drivers/i2c/busses/i2c-i801.c`
* **Current Status:** 🆕 **Bug Reported**
* **Notes:** When the hardware (or emulator) returns an invalid block read size (e.g., 0 or >32), the driver's ISR (`i801_isr_byte_done`) improperly forces the length to 32 instead of aborting the transaction. Because the transaction is never cleanly terminated, `complete()` is not called (causing a Hung Task for the caller holding the I2C adapter lock), and the hardware remains in `HOST_BUSY`, continuously asserting the IRQ line and trapping the CPU in an endless interrupt storm (Kernel DoS).

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] i2c: i801: interrupt storm and hung task with invalid block read size | <u>[lore.kernel.org](https://lore.kernel.org/all/20260617031646.13295-1-w15303746062@163.com/)</u> |
|                                                              | <u>[lore.kernel.org](#)</u>                                  |
|                                                              | <u>[lore.kernel.org](#)</u>                                  |
|                                                              | <u>[lore.kernel.org](#)</u>                                  |

