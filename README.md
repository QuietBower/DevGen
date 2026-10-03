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

## Bug status
| Crash                                  | Device      | LLM-Qwen | LLM-GPT-5.1 | LLM-Gemini | Status                                                       |
| -------------------------------------- | ----------- | -------- | ----------- | ---------- | ------------------------------------------------------------ |
| `WARNING in drm_gem_release`           | `bochs-drm` | ✓        | ✓           | ☓          | [CVE-2026-23149](https://www.cve.org/CVERecord?id=CVE-2026-23149) |
| `WARNING in idr_alloc`                 | `intel-qep` | ☓        | ✓           | ✓          | [CVE-2026-23149](https://www.cve.org/CVERecord?id=CVE-2026-23149) |
| `KASAN: UAF read in adf_dev_up`        | `c6xxvf`    | ✓        | ☓           | ☓          | Confirmed                                                    |
| `general protection fault in h5_recv`  | `8250_lpss` | ✓        | ☓           | ✓          | Confirmed                                                    |
| `INFO: rcu detected stall in sys_mmap` | `pci-tng`   | ✓        | ☓           | ☓          | New                                                          |
| `INFO: rcu detected stall in do_idle`  | `wdt_pci`   | ☓        | ☓           | ✓          | New                                                          |
| `INFO: task hung in i2c_smbus_xfer`    | `bttv`      | ✓        | ✓           | ✓          | New                                                          |









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
| #003 | `net/ipv6/udp.c`                                 | [Memory leak in udpv6_sendmsg](https://github.com/Wmingyu/DevGen-results/blob/main/details/Memory%20leak%20in%20udpv6_sendmsg().md) | 👀**Bug Confirmed**<br/>🔄 **Indirectly Fixed**                |
| #004 | `gpu/drm/drm_gem.c`                              | [WARNING in idr_alloc](https://github.com/Wmingyu/DevGen-results/blob/main/details/Unvalidated%20negative%20handle%20in%20drm_gem_change_handle_ioctl.md) | 👀**Bug Confirmed**                                           |
| #005 | `fs/hugetlbfs/inode.c`<br>`mm/vma.c`             | [resv_map memory leak in __mmap_region()](https://github.com/Wmingyu/DevGen-results/blob/main/details/resv_map%20memory%20leak%20in%20__mmap_region().md) | ✅ **Patch Accepted**<br>**[CVE-2026-46318](https://www.cve.org/CVERecord?id=CVE-2026-46318)** |
| #006 | `i2c/i2c-dev.c`                                  | [Integer overflow in I2C_TIMEOUT ioctl](https://github.com/Wmingyu/DevGen-results/blob/main/details/Integer%20overflow%20in%20I2C_TIMEOUT%20ioctl.md) | ✅**Patch Accepted**<br>**[CVE-2026-52948](https://www.cve.org/CVERecord?id=CVE-2026-52948)** |
| #007 | `char/agp/amd64-agp.c`                           | [NULL ptr deref in amd64_fetch_size()](https://github.com/Wmingyu/DevGen-results/blob/main/details/NULL%20ptr%20deref%20in%20amd64_fetch_size().md) | ✅ **Patch Accepted**<br/>**[CVE-2026-53325](https://www.cve.org/CVERecord?id=CVE-2026-53325)** |
| #008 | `x86/kernel/smpboot.c`                           | [WARN_ON in set_cpu_sibling_map via numa=fake](https://github.com/Wmingyu/DevGen-results/blob/main/details/WARN_ON%20in%20set_cpu_sibling_map%20via%20numa%3Dfake.md) | 👀 **Bug Confirmed**                                          |
| #009 | `net/ethernet/packetengines/hamachi.c`           | [Divide by zero in hamachi_init_one](https://github.com/Wmingyu/DevGen-results/blob/main/details/Divide%20by%20zero%20in%20hamachi_init_one.md) | 👀 **Bug Confirmed**                                          |
| #010 | `crypto/intel/qat/qat_common/adf_dev_mgr.c`      | [Use-After-Free in adf_devmgr_get_dev_by_id()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Use-After-Free%20in%20adf_devmgr_get_dev_by_id().md) | ✅ **Patch Accepted**                                         |
| #011 | `drivers/crypto/qat/qat_common/adf_ctl_drv.c`    | [Local DoS via printk storm in QAT ioctls](https://github.com/Wmingyu/DevGen-results/blob/main/details/Local%20DoS%20via%20printk%20storm%20in%20QAT%20ioctls.md) | ✅ **Patch Accepted**<br/>**[CVE-2026-64529](https://www.cve.org/CVERecord?id=CVE-2026-64529)**<br/>**HIGH 7.8** |
| #012 | `i2c/busses/i2c-i801.c`                          | [Hardware state machine corruption in i801_access()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Hardware%20state%20machine%20corruption%20in%20i801_access().md) | ✅ **Patch Accepted**<br>**[CVE-2026-64205](https://www.cve.org/CVERecord?id=CVE-2026-64205)** |
| #013 | `watchdog/wdt_pci.c`                             | [Shared IRQ storm in wdtpci_interrupt()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Shared%20IRQ%20storm%20in%20wdtpci_interrupt().md) | 👀 **Bug Confirmed**<br>❌ **WontFix**                         |
| #014 | `gpu/drm/ast/ast_2500.c`                         | [Soft lockup in ast_2500_patch_ahb()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Soft%20lockup%20in%20ast_2500_patch_ahb().md) | 👀 **Bug Confirmed**<br>🛠️ **Patch Sent**                      |
| #015 | `bluetooth/hci_ldisc.c`                          | [UAFs and race conditions in hci_uart lifecycle](https://github.com/Wmingyu/DevGen-results/blob/main/details/UAFs%20and%20race%20conditions%20in%20hci_uart%20lifecycle.md) | ✅ **Patch Accepted**<br>**[CVE-2026-46275](https://www.cve.org/CVERecord?id=CVE-2026-46275)**<br/>**HIGH 7.8** |
| #016 | `gpu/drm/vkms/vkms_crtc.c`                       | [ABBA deadlock in vkms vblank timer](https://github.com/Wmingyu/DevGen-results/blob/main/details/ABBA%20deadlock%20in%20vkms%20vblank%20timer.md) | ✅ **Patch Accepted**<br>**[CVE-2025-71315](https://www.cve.org/CVERecord?id=CVE-2025-71315)** |
| #017 | `gpu/drm/vmwgfx/vmwgfx_vkms.c`                   | [Hrtimer interrupt storm in vmw_vkms_enable_vblank()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Hrtimer%20interrupt%20storm%20in%20vmw_vkms_enable_vblank().md) | 👀 **Bug Confirmed**<br>🛠️ **Patch Sent**                      |
| #018 | `fs/fcntl.c`                                     | [SOFTIRQ-unsafe lock order deadlock in fasync signaling](https://github.com/Wmingyu/DevGen-results/blob/main/details/SOFTIRQ-unsafe%20lock%20order%20deadlock%20in%20fasync%20signaling.md) | ✅ **Patch Accepted** <br>**[CVE-2026-52946](https://www.cve.org/CVERecord?id=CVE-2026-52946)** <br/>**HIGH 7.5** |
| #019 | `video/fbdev/core/fbcon.c`                       | [Memory leak in fbcon_do_set_font()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Memory%20leak%20in%20fbcon_do_set_font().md) | 🆕 **Bug Confirmed**<br/>❌ **WontFix**                        |
| #020 | `gpu/drm/vkms/vkms_crtc.c`                       | [Hrtimer livelock via unvalidated display mode](https://github.com/Wmingyu/DevGen-results/blob/main/details/Hrtimer%20livelock%20via%20unvalidated%20display%20mode.md) | 🔄 **Indirectly Fixed**                                       |
| #021 | `gpu/drm/drm_prime.c`                            | [rb_tree corruption in drm_prime_remove_buf_handle()](https://github.com/Wmingyu/DevGen-results/blob/main/details/rb_tree%20corruption%20in%20drm_prime_remove_buf_handle().md) | 👀 **Bug Confirmed**<br>❌ **WontFix**<br><sub>On Hold</sub>   |
| #022 | `net/qrtr/af_qrtr.c`                             | [Refcount saturation and UAF in qrtr_port_remove()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Refcount%20saturation%20and%20UAF%20in%20qrtr_port_remove().md) | ✅ **Patch Accepted<br>[CVE-2026-52947](https://www.cve.org/CVERecord?id=CVE-2026-52947)** <br/>**HIGH 7.8** |
| #023 | `drivers/gpu/drm/drm_gem.c`                      | [WARNING in idr_alloc via drm_gem_change_handle_ioctl](https://github.com/Wmingyu/DevGen-results/blob/main/details/WARNING%20in%20idr_alloc%20via%20drm_gem_change_handle_ioctl.md) | ✅ **Patch Accepted**<br/>**[CVE-2026-23149](https://www.cve.org/CVERecord?id=CVE-2026-23149)** |
| #024 | `drivers/crypto/intel/qat/qat_common/adf_init.c` | [Use-After-Free in adf_dev_up()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Use-After-Free%20in%20adf_dev_up().md) | 👀 **Bug Confirmed**                                          |
| #025 | `drivers/net/wireless/mac80211_hwsim.c`          | [Context-recursion deadlock in mac80211_hwsim](https://github.com/Wmingyu/DevGen-results/blob/main/details/Context-recursion%20deadlock%20in%20mac80211_hwsim.md) | 👀**Bug Confirmed**<br/>🔄 **Indirectly Fixed**                |
| #026 | `drivers/i2c/busses/i2c-i801.c`                  | [Interrupt storm in i801_isr() via invalid block read size](https://github.com/Wmingyu/DevGen-results/blob/main/details/Interrupt%20storm%20in%20i801_isr()%20via%20invalid%20block%20read%20size.md) | 🆕 **Bug Reported**<br/>                                      |
| #027 | `drivers/misc/ibmasm/module.c`                   | [misc: ibmasm: Remove obsolete IBM Remote Supervisor Adapter driver](https://github.com/Wmingyu/DevGen-results/blob/main/details/Page%20fault%20via%20undersized%20PCI%20BAR%200%20in%20ibmasm.md) | ✅ **Patch Accepted**                                         |
| #028 | `drivers/video/fbdev/core/fbcon.c`               | [Out-of-bounds read in err_out of fbcon_do_set_font()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Out-of-bounds%20read%20in%20err_out%20of%20fbcon_do_set_font().md) | ✅ **Patch Accepted**<br/>**[CVE-2026-53402](https://www.cve.org/CVERecord?id=CVE-2026-53402)**<br/>**HIGH 7.1** |
| #029 | `drivers/i2c/busses/i2c-i801.c`                  | [Stack-out-of-bounds in i801_isr_byte_done()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Stack-out-of-bounds%20in%20i801_isr_byte_done().md) | 🆕 **Bug Reported** <br/>🛠️ **Patch Sent**                     |
| #030 | `kernel/module/main.c`                           | [UAF and GPF in idempotent_init_module()](https://github.com/Wmingyu/DevGen-results/blob/main/details/UAF%20and%20GPF%20in%20idempotent_init_module().md) | 🆕 **Bug Confirmed**<br/>❌ **WontFix**                        |
| #031 | `drivers/tty/serial/8250/8250_port.c`            | [Page Fault and UAF in mem_serial_in()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Page%20Fault%20and%20UAF%20in%20mem_serial_in().md) | 🆕**Bug Confirmed** <br/>❌ **WontFix**                        |
| #032 | `drivers/video/fbdev/core/fb_io_fops.c`          | [Potential OOB access in fb_io_read/write](https://github.com/Wmingyu/DevGen-results/blob/main/details/Potential%20OOB%20access%20in%20fb_io_readwrite.md) | ✅ **Patch Accepted**                                         |
| #033 | `drivers/video/fbdev/core/fb_io_fops.c`          | [Pointer desynchronization and OOB read in fb_io_read()](https://github.com/Wmingyu/DevGen-results/blob/main/details/Pointer%20desynchronization%20and%20OOB%20read%20in%20fb_io_read().md) | ✅ **Patch Accepted****<br/>**[CVE-2026-80578](https://www.cve.org/CVERecord?id=CVE-2026-80578)**<br/>**HIGH 7.3** |
| #034 | `drivers/i2c/busses/i2c-i801.c`                  | [Hung Task panics via malicious I2C_TIMEOUT ioctl](https://github.com/Wmingyu/DevGen-results/blob/main/details/Hung%20Task%20panics%20via%20malicious%20I2C_TIMEOUT%20ioctl.md) | ❌ **WontFix** <br/>🛡️**Crash**                                |
| #035 | `drivers/tty/vt/vt.c`                            | [Memory leak in vc_allocate() on screen buffer allocation failure](https://github.com/Wmingyu/DevGen-results/blob/main/details/Memory%20leak%20in%20vc_allocate()%20on%20screen%20buffer%20allocation%20failure.md) | ✅ **Patch Accepted**                                         |
| #036 | `drivers/gpu/drm/gma500/intel_gmbus.c`           | [OOB access and integer underflow in gmbus_xfer()](https://github.com/Wmingyu/DevGen-results/blob/main/details/OOB%20access%20and%20integer%20underflow%20in%20gmbus_xfer().md) | 🆕 **Bug Reported** <br/>🛠️ **Patch Sent**                     |
| #037 | `drivers/tty/tty_io.c`                           | [RCU stall via excessively long TCSBRKP duration](https://github.com/Wmingyu/DevGen-results/blob/main/details/RCU%20stall%20via%20excessively%20long%20TCSBRKP%20duration.md) | 👀 **Bug Confirmed**<br/>❌ **WontFix**                        |

---

## 🔗 Upstream Public Records

For transparency and independent verification of our upstream engagements, you can track the complete public mailing list activity of our research team members directly on the Linux Kernel archive (`lore.kernel.org`):

* 📧 [Mingyu Wang (25181214217@stu.xidian.edu.cn) - Search Results](https://lore.kernel.org/all/?q=25181214217%40stu.xidian.edu.cn)
* 📧 [Mingyu Wang (Alternative) - Search Results](https://lore.kernel.org/all/?q=w15303746062%40163.com)
* 📧 [Zhi Wang - Search Results](https://lore.kernel.org/all/?q=%E7%8E%8B%E5%BF%97)

> **Note:** These searches aggregate our patch submissions, bug reports, and technical discussions with kernel maintainers across various subsystem mailing lists.









## Experimental Results

For detailed experimental data, please refer to [Detailed Experimental Results](https://365.kdocs.cn/l/cgIgQHBxHmhB?kmonFrom=k_Share_FileList&from=kdocs_pc_web&startTime=1772418180736&traceparent=00-00b88812dd73a65ffe1beb5533ed9027-8889954fd88680b6-01-10).

For all crash artifacts and reproduction materials, please refer to [DevGen Crash Reports](https://anonymous.4open.science/r/Crashes).



