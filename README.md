# Automated Generation of Virtual Device Models for Kernel Drivers via Large Language Models

## Driver Information Collection

In the first phase of **DevGen**, the core source code of the target kernel will be extracted, and the main execution will be carried out in the `static_analyze` directory.

## LLM-Guided Device Modeling

The core stage of **DevGen** mainly involves using LLM to guide the generation of virtual device code. The specific details can be found in the `DevGen_Core` directory.

## Integration and Fuzzing

1. Simplify the static analysis part of the code and generate a JSON file,run `python3 generate_json.py`
2. Process the target driver code based on the generated JSON file.`python3 simple_code.py`
3. Modify the relevant path names and perform the fuzzing.`start_fuzz.py`

## Config and Result

1. We have placed all the configurations that **DevGen** needs during its execution in the `config` directory.
2. The `result` directory contains the results of different model simulations as well as coverage and crash information.

# 🐛 Kernel Bug Tracker

Welcome to my Kernel Bug Tracker repository! This repository serves as an open record of the kernel vulnerabilities discovered, analyzed, and reported to the upstream Linux Kernel community. 

> 📄 **Academic Artifact:** This repository provides supplementary data, complete timelines, and patch histories for the academic paper: **[DevGen]**.

## 📊 Status Legend

| Symbol | Status               | Description                                                  |
| :----: | :------------------- | :----------------------------------------------------------- |
|   🆕    | **Bug Reported**     | Initial bug report sent to the mailing list, awaiting first response. |
|   👀    | **Bug Confirmed**    | Developers have acknowledged the bug or are actively debugging it. |
|   💬    | **Needs Reply**      | Ongoing discussion requiring follow-up or maintainers have asked questions. |
|   🛠️    | **Patch Sent**       | A fix patch has been submitted and is pending review.        |
|   ✅    | **Patch Accepted**   | The patch is approved by maintainers (e.g., `Reviewed-by`) and merged or queued upstream. |
|   ❌    | **WontFix**          | The bug cannot be fixed (e.g., due to design choices or hardware limits). |
|   🔄    | **Indirectly Fixed** | Indirectly resolved through upstream codebase refactoring or unrelated patches. |
|   🛡️    | **Crash**            | Crash triggered by privileged API abuse. Upstream considers it intended behavior and not a security bug. |

---

## 📜 Vulnerability List

*Click on any vulnerability title to view the dedicated page with patch history and mailing list threads (lore.kernel.org).*

| ID   | Location                                         | Vulnerability & Details                                      | Status & Notes                                               |
| :--- | :----------------------------------------------- | :----------------------------------------------------------- | :----------------------------------------------------------- |
| #001 | `net/ethernet/packetengines/hamachi.c`           | [net: packetengines: remove obsolete hamachi driver](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=4cf42f9c3e3624fedf4f6c38c3d81d80c8b3cbd6) | ✅ **Patch Accepted**                                         |
| #002 | `net/ethernet/packetengines/yellowfin.c`         | [net: packetengines: remove obsolete yellowfin driver](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=aec3202247b4ab41c5bf3b9f704a2d9a323a051b) | ✅ **Patch Accepted**                                         |
| #003 | `net/ipv6/udp.c`                                 | [Memory leak in udpv6_sendmsg](./details/Memory%20leak%20in%20udpv6_sendmsg%28%29.md) | 👀**Bug Confirmed**<br/>🔄 **Indirectly Fixed**                |
| #004 | `gpu/drm/drm_gem.c`                              | [WARNING in idr_alloc](./details/Unvalidated%20negative%20handle%20in%20drm_gem_change_handle_ioctl.md) | 👀**Bug Confirmed**                                           |
| #005 | `fs/hugetlbfs/inode.c`<br>`mm/vma.c`             | [resv_map memory leak in __mmap_region()](./details/resv_map%20memory%20leak%20in%20__mmap_region%28%29.md) | ✅ **Patch Accepted**<br>**[CVE-2026-46318](https://www.cve.org/CVERecord?id=CVE-2026-46318)** |
| #006 | `i2c/i2c-dev.c`                                  | [Integer overflow in I2C_TIMEOUT ioctl](./details/Integer%20overflow%20in%20I2C_TIMEOUT%20ioctl.md) | ✅**Patch Accepted**<br>**[CVE-2026-52948](https://www.cve.org/CVERecord?id=CVE-2026-52948)** |
| #007 | `char/agp/amd64-agp.c`                           | [NULL ptr deref in amd64_fetch_size()](./details/NULL%20ptr%20deref%20in%20amd64_fetch_size%28%29.md) | ✅ **Patch Accepted**<br/>**[CVE-2026-53325](https://www.cve.org/CVERecord?id=CVE-2026-53325)** |
| #008 | `x86/kernel/smpboot.c`                           | [WARN_ON in set_cpu_sibling_map via numa=fake](./details/WARN_ON%20in%20set_cpu_sibling_map%20via%20numa%3Dfake.md) | 👀 **Bug Confirmed**                                          |
| #009 | `net/ethernet/packetengines/hamachi.c`           | [Divide by zero in hamachi_init_one](./details/Divide%20by%20zero%20in%20hamachi_init_one.md) | 👀 **Bug Confirmed**                                          |
| #010 | `crypto/intel/qat/qat_common/adf_dev_mgr.c`      | [Use-After-Free in adf_devmgr_get_dev_by_id()](./details/Use-After-Free%20in%20adf_devmgr_get_dev_by_id%28%29.md) | ✅ **Patch Accepted**                                         |
| #011 | `drivers/crypto/qat/qat_common/adf_ctl_drv.c`    | [Local DoS via printk storm in QAT ioctls](./details/Local%20DoS%20via%20printk%20storm%20in%20QAT%20ioctls.md) | ✅ **Patch Accepted**<br/>**[CVE-2026-64529](https://www.cve.org/CVERecord?id=CVE-2026-64529)**<br/>**HIGH 7.8** |
| #012 | `i2c/busses/i2c-i801.c`                          | [Hardware state machine corruption in i801_access()](./details/Hardware%20state%20machine%20corruption%20in%20i801_access%28%29.md) | ✅ **Patch Accepted**<br>**[CVE-2026-64205](https://www.cve.org/CVERecord?id=CVE-2026-64205)** |
| #013 | `watchdog/wdt_pci.c`                             | [Shared IRQ storm in wdtpci_interrupt()](./details/Shared%20IRQ%20storm%20in%20wdtpci_interrupt%28%29.md) | 👀 **Bug Confirmed**<br>❌ **WontFix**                         |
| #014 | `gpu/drm/ast/ast_2500.c`                         | [Soft lockup in ast_2500_patch_ahb()](./details/Soft%20lockup%20in%20ast_2500_patch_ahb%28%29.md) | 👀 **Bug Confirmed**<br>🛠️ **Patch Sent**                      |
| #015 | `bluetooth/hci_ldisc.c`                          | [UAFs and race conditions in hci_uart lifecycle](./details/UAFs%20and%20race%20conditions%20in%20hci_uart%20lifecycle.md) | ✅ **Patch Accepted**<br>**[CVE-2026-46275](https://www.cve.org/CVERecord?id=CVE-2026-46275)**<br/>**HIGH 7.8** |
| #016 | `gpu/drm/vkms/vkms_crtc.c`                       | [ABBA deadlock in vkms vblank timer](./details/ABBA%20deadlock%20in%20vkms%20vblank%20timer.md) | ✅ **Patch Accepted**<br>**[CVE-2025-71315](https://www.cve.org/CVERecord?id=CVE-2025-71315)** |
| #017 | `gpu/drm/vmwgfx/vmwgfx_vkms.c`                   | [Hrtimer interrupt storm in vmw_vkms_enable_vblank()](./details/Hrtimer%20interrupt%20storm%20in%20vmw_vkms_enable_vblank%28%29.md) | 👀 **Bug Confirmed**<br>🛠️ **Patch Sent**                      |
| #018 | `fs/fcntl.c`                                     | [SOFTIRQ-unsafe lock order deadlock in fasync signaling](./details/SOFTIRQ-unsafe%20lock%20order%20deadlock%20in%20fasync%20signaling.md) | ✅ **Patch Accepted** <br>**[CVE-2026-52946](https://www.cve.org/CVERecord?id=CVE-2026-52946)** <br/>**HIGH 7.5** |
| #019 | `video/fbdev/core/fbcon.c`                       | [Memory leak in fbcon_do_set_font()](./details/Memory%20leak%20in%20fbcon_do_set_font%28%29.md) | 🆕 **Bug Confirmed**<br/>❌ **WontFix**                        |
| #020 | `gpu/drm/vkms/vkms_crtc.c`                       | [Hrtimer livelock via unvalidated display mode](./details/Hrtimer%20livelock%20via%20unvalidated%20display%20mode.md) | 🔄 **Indirectly Fixed**                                       |
| #021 | `gpu/drm/drm_prime.c`                            | [rb_tree corruption in drm_prime_remove_buf_handle()](./details/rb_tree%20corruption%20in%20drm_prime_remove_buf_handle%28%29.md) | 👀 **Bug Confirmed**<br>❌ **WontFix**<br><sub>On Hold</sub>   |
| #022 | `net/qrtr/af_qrtr.c`                             | [Refcount saturation and UAF in qrtr_port_remove()](./details/Refcount%20saturation%20and%20UAF%20in%20qrtr_port_remove%28%29.md) | ✅ **Patch Accepted<br>[CVE-2026-52947](https://www.cve.org/CVERecord?id=CVE-2026-52947)** <br/>**HIGH 7.8** |
| #023 | `drivers/gpu/drm/drm_gem.c`                      | [WARNING in idr_alloc via drm_gem_change_handle_ioctl](./details/WARNING%20in%20idr_alloc%20via%20drm_gem_change_handle_ioctl.md) | ✅ **Patch Accepted**<br/>**[CVE-2026-23149](https://www.cve.org/CVERecord?id=CVE-2026-23149)** |
| #024 | `drivers/crypto/intel/qat/qat_common/adf_init.c` | [Use-After-Free in adf_dev_up()](./details/Use-After-Free%20in%20adf_dev_up%28%29.md) | 👀 **Bug Confirmed**                                          |
| #025 | `drivers/net/wireless/mac80211_hwsim.c`          | [Context-recursion deadlock in mac80211_hwsim](./details/Context-recursion%20deadlock%20in%20mac80211_hwsim.md) | 👀**Bug Confirmed**<br/>🔄 **Indirectly Fixed**                |
| #026 | `drivers/i2c/busses/i2c-i801.c`                  | [Interrupt storm in i801_isr() via invalid block read size](./details/Interrupt%20storm%20in%20i801_isr%28%29%20via%20invalid%20block%20read%20size.md) | 🆕 **Bug Reported**<br/>                                      |
| #027 | `drivers/misc/ibmasm/module.c`                   | [misc: ibmasm: Remove obsolete IBM Remote Supervisor Adapter driver](./details/Page%20fault%20via%20undersized%20PCI%20BAR%200%20in%20ibmasm.md) | ✅ **Patch Accepted**                                         |
| #028 | `drivers/video/fbdev/core/fbcon.c`               | [Out-of-bounds read in err_out of fbcon_do_set_font()](./details/Out-of-bounds%20read%20in%20err_out%20of%20fbcon_do_set_font%28%29.md) | ✅ **Patch Accepted**<br/>**[CVE-2026-53402](https://www.cve.org/CVERecord?id=CVE-2026-53402)**<br/>**HIGH 7.1** |
| #029 | `drivers/i2c/busses/i2c-i801.c`                  | [Stack-out-of-bounds in i801_isr_byte_done()](./details/Stack-out-of-bounds%20in%20i801_isr_byte_done%28%29.md) | 🆕 **Bug Reported** <br/>🛠️ **Patch Sent**                     |
| #030 | `kernel/module/main.c`                           | [UAF and GPF in idempotent_init_module()](./details/UAF%20and%20GPF%20in%20idempotent_init_module%28%29.md) | 🆕 **Bug Confirmed**<br/>❌ **WontFix**                        |
| #031 | `drivers/tty/serial/8250/8250_port.c`            | [Page Fault and UAF in mem_serial_in()](./details/Page%20Fault%20and%20UAF%20in%20mem_serial_in%28%29.md) | 🆕**Bug Confirmed** <br/>❌ **WontFix**                        |
| #032 | `drivers/video/fbdev/core/fb_io_fops.c`          | [Potential OOB access in fb_io_read/write](./details/Potential%20OOB%20access%20in%20fb_io_readwrite.md) | ✅ **Patch Accepted**                                         |
| #033 | `drivers/video/fbdev/core/fb_io_fops.c`          | [Pointer desynchronization and OOB read in fb_io_read()](./details/Pointer%20desynchronization%20and%20OOB%20read%20in%20fb_io_read%28%29.md) | ✅ **Patch Accepted****<br/>**[CVE-2026-80578](https://www.cve.org/CVERecord?id=CVE-2026-80578)**<br/>**HIGH 7.3** |
| #034 | `drivers/i2c/busses/i2c-i801.c`                  | [Hung Task panics via malicious I2C_TIMEOUT ioctl](./details/Hung%20Task%20panics%20via%20malicious%20I2C_TIMEOUT%20ioctl.md) | ❌ **WontFix** <br/>🛡️**Crash**                                |
| #035 | `drivers/tty/vt/vt.c`                            | [Memory leak in vc_allocate() on screen buffer allocation failure](./details/Memory%20leak%20in%20vc_allocate%28%29%20on%20screen%20buffer%20allocation%20failure.md) | ✅ **Patch Accepted**                                         |
| #036 | `drivers/gpu/drm/gma500/intel_gmbus.c`           | [OOB access and integer underflow in gmbus_xfer()](./details/OOB%20access%20and%20integer%20underflow%20in%20gmbus_xfer%28%29.md) | 🆕 **Bug Reported** <br/>🛠️ **Patch Sent**                     |
| #037 | `drivers/tty/tty_io.c`                           | [RCU stall via excessively long TCSBRKP duration](./details/RCU%20stall%20via%20excessively%20long%20TCSBRKP%20duration.md) | 👀 **Bug Confirmed**<br/>❌ **WontFix**                        |



## Experimental Results

For detailed experimental data, please refer to [Detailed Experimental Results](https://365.kdocs.cn/l/cgIgQHBxHmhB?kmonFrom=k_Share_FileList&from=kdocs_pc_web&startTime=1772418180736&traceparent=00-00b88812dd73a65ffe1beb5533ed9027-8889954fd88680b6-01-10).

For all crash artifacts and reproduction materials, please refer to [DevGen Crash Reports](https://anonymous.4open.science/r/Crashes).



