# DevGen

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20QEMU-informational)

![Language](https://img.shields.io/badge/language-Python%203%20%7C%20C-brightgreen)



![Fuzzer](https://img.shields.io/badge/fuzzer-Syzkaller-orange)

<div align="justify">

DevGen generates executable QEMU device models from Linux PCI/PCIe driver source code, so that  
drivers whose physical devices are unavailable can still be fuzzed.

</div>

<div align="justify">

Given a target driver, DevGen recovers the device constraints encoded in the driver source, guides  
an LLM to construct a virtual device model, validates the candidate through code checks and QEMU  
runtime feedback, and then completes the Syzlang descriptions of the userspace interfaces that  
appear once the driver binds — which lets Syzkaller exercise the driver.

</div>

<div align="justify">

Run over 855 PCI/PCIe drivers with three LLM backends, DevGen builds at least one working model for  
470 drivers, makes 84 previously untestable drivers testable, and generates or completes 438  
Syzlang call descriptions. It has uncovered 37 previously unknown Linux kernel bugs — 18 patches  
merged so far, including 3 driver removals, and 12 bugs with CVE identifiers. They are listed in the  
Kernel Bug Tracker section below.

</div>

## Components

| Component                  | Directory                                      | What it does                                                                                 |
| -------------------------- | ---------------------------------------------- | -------------------------------------------------------------------------------------------- |
| Static Context Extraction  | [`static_analyze/`](static_analyze/)           | recovers the device constraints encoded in the driver source                                 |
| LLM-Guided Device Modeling | [`DevGen_Core/`](DevGen_Core/)                 | builds a QEMU device model from that context and validates it end to end                     |
| Driver Fuzzing Integration | [`fuzzing_integration/`](fuzzing_integration/) | identifies the exposed interfaces, completes their Syzlang descriptions and drives Syzkaller |

## Repository Layout

```text
DevGen/
├── static_analyze/                 # static context extraction
├── DevGen_Core/                    # LLM-guided device modeling
├── Case_Repair_Library/            # 9 modeling-error categories used for iterative repair
├── fuzzing_integration/            # interface identification, Syzlang generation/validation, fuzzing
├── start_fuzz.py                   # targeted Syzkaller fuzzing driver
├── config/                         # 7.1.config, enable_syscalls.json, create-image.sh
├── device_result/                  # generated device models, grouped by LLM backend
│   ├── GPT/  Gemini/  DeepSeek/
├── new_sys_linux/                  # generated Syzlang descriptions + extracted .const files
├── result/                         # simulation results: Fuzzing/{bug,coverage,crashes}, Qwen3-Max
├── details/                        # 35 per-bug write-ups (report, patch history, mailing-list threads)
├── email/                          # mailing-list thread archives for the reported bugs
└── DevGen_Experimental_Data.xlsx   # collected results and coverage data
```

## Getting Started

1. Build the guest image with `fuzzing_integration/image/create-image.sh`, and pick a guest kernel  
   config from `fuzzing_integration/kernel-configs/`.
2. Extract the device context for your target driver — see  
   [`static_analyze/README.md`](static_analyze/README.md).
3. Generate and validate a device model — see [`DevGen_Core/README.md`](DevGen_Core/README.md).
4. Identify the interfaces the model exposes, generate and validate the Syzlang descriptions, and  
   start the fuzzing campaign — see  
   [`fuzzing_integration/README.md`](fuzzing_integration/README.md).

## Static Context Extraction

<div align="justify">

Device constraints are distributed across registration objects, callbacks, headers and call chains  
that span multiple files: a local code fragment may omit resource layout constraints or state  
dependencies, whereas supplying the whole kernel source introduces substantial irrelevant context.  
Given a target driver `D`, static analysis uses the driver's source directory as the initial scope  
and follows symbol references and function calls from its `struct pci_driver` object, the associated  
`pci_device_id` table and the callbacks. Device identifiers and declarations provide explicit  
configuration information, while resource setup code and register definitions describe the  
resources the driver accesses. The analysis also collects the code around device accesses, polling  
conditions and error checks in `probe` and its callees, and the interface registration and handling  
code that provides the context for operations used after binding. The result is organized as

</div>

```
E(D) = < I(D), R(D), C_init(D), C_func(D) >
```

<div align="justify">

where `I(D)` contains the device identification information, `R(D)` the resource layout,  
`C_init(D)` the source context for initialization requirements, and `C_func(D)` the device  
interactions and interface logic relevant after binding.

</div>

<div align="justify">

Instead of placing all collected code in every LLM prompt, the extracted context is organized into  
a **structured summary** and a **searchable source index**. The summary provides device identity,  
resource layout and initialization constraints for initial modeling, while the index retains the  
supporting source code and definitions for further retrieval: when additional definitions or  
interaction details are needed, the LLM issues a `need_resource` request and the relevant source  
fragments plus calling context are added to the current prompt before modeling resumes. Retrieval  
stops when `need_resource` is empty or the retrieval iteration limit is reached.

</div>

See [`static_analyze/README.md`](static_analyze/README.md) for build and run instructions.

## LLM-Guided Device Modeling

<div align="justify">

A candidate model is initialized from a predefined QEMU template using the information obtained  
through static context extraction, and is then completed in two stages:

</div>

- **Structural modeling** establishes the device skeleton — the target device identity (the first  
  valid entry of the driver's `pci_device_id` table), the BARs and their associated address spaces,  
  and the fields for register values and internal state derived from the driver's accesses and state  
  checks.
- **Behavioral modeling** completes the skeleton with the response logic and state transitions  
  required by the driver: MMIO/PIO responses, DMA transfers, interrupt delivery and reset logic are  
  generated where the corresponding accesses and checks exist, and register responses are related to  
  the model's internal state so that dependencies between successive operations are preserved (for  
  example, a start command followed by polling of a ready bit).

<div align="justify">

The candidate then undergoes **code validation** (clangd diagnostics and QEMU compilation) and  
**runtime semantic validation** (QEMU startup, kernel logs and `lspci -k` binding status). Failures  
in either stage guide iterative repair using the corresponding diagnostics together with a repair  
case library covering 9 common modeling error categories; runtime semantic validation is limited to  
three attempts (`MAX = 3`), with code validation repeated after each semantic repair. The first  
candidate that satisfies

</div>

```
Valid(M, D) = Code(M) ∧ Boot(M) ∧ Bind(M, D)
```

<div align="justify">

is returned as the model `M` for driver `D`; binding is verified by the target driver's name in the  
`Kernel driver in use` field reported by `lspci -k` for that device.

</div>

<div align="justify">

The implementation is in [`DevGen_Core/`](DevGen_Core/) (see  
[`DevGen_Core/README.md`](DevGen_Core/README.md)) and the repair case library in  
[`Case_Repair_Library/`](Case_Repair_Library/).

</div>

## Driver Fuzzing Integration

<div align="justify">

The validated device model enables the target driver to complete its `probe` routine and bind to  
the generated device. After binding, testing the driver requires exercising the software interfaces  
it exposes to user space. Syzkaller relies on Syzlang descriptions to generate inputs for these  
interfaces — device opening operations, private `ioctl` commands, parameter constraints and  
resource dependencies — so the exposed interfaces are identified and matched against the existing  
Syzlang corpus: existing descriptions are reused when available, while the LLM generates the missing  
ones and completes the incomplete ones. All of this is implemented in  
[`fuzzing_integration/`](fuzzing_integration/README.md), where the concrete run order, per-script  
usage and directory layout are documented.

</div>

### Interface Identification

<div align="justify">

To locate the candidate device nodes, two QEMU runs are compared under otherwise identical  
conditions, with and without the validated model `M`. Let `N+(D)` and `N-(D)` denote the `/dev`  
node sets with and without `M`; their difference defines the candidate node set:

</div>

```
ΔN(D) = N+(D) \ N-(D)
```

<div align="justify">

Because startup can introduce unrelated nodes, each candidate in `ΔN(D)` is traced through  
registration and subsystem dispatch to establish its association with the target driver. The  
collected code forms the *Interface Source Context* and covers registration sites, command dispatch  
and userspace argument handling. For character devices the analysis follows `unlocked_ioctl` /  
`compat_ioctl` in `file_operations` to the relevant handlers and enumerates their supported command  
branches, examining the associated `_IO`, `_IOR`, `_IOW` and `_IOWR` macros together with  
`copy_from_user` / `copy_to_user`; other interface types are traced through their subsystem  
registration and dispatch paths. Interfaces that do not correspond to newly created `/dev` nodes  
(debugfs interfaces, or existing shared nodes such as `/dev/ptmx`) are identified through source  
registration analysis and predefined mappings between drivers and Syzlang descriptions.

</div>

<div align="justify">

The identified operations and command branches are then matched against the existing Syzlang  
corpus. Variants covered by existing descriptions are grouped as `ExistingCalls(D)` and reused;  
those lacking descriptions, or whose descriptions omit essential constraints, are grouped as  
`MissingCalls(D)` and passed to the next stage. When all identified variants are adequately  
described, targeted fuzzing is configured with the existing descriptions alone and description  
completion is skipped.

</div>


### Syzlang Description Completion

<div align="justify">

For each syscall variant in `MissingCalls(D)`, the relevant code and definitions are selected from
the Interface Source Context, and three analyses recover the semantics needed for description
completion:

</div>

- **Identifier deduction** — the device path, the opening method and the operation identifier.
- **Type recovery** — argument direction, primitive types, pointer indirection, structures and
  length constraints, inferred from command encodings, userspace transfers and field accesses.
- **Dependency analysis** — resource relationships and the call ordering they impose, for example
  `syz_open_dev` produces a file-descriptor resource that is consumed by a subsequent `ioctl`.

<div align="justify">

These recovered constraints guide the LLM in generating the missing descriptions, or in extending
existing ones where commands or essential constraints are absent. Each candidate description is
validated through **Syzlang compilation followed by a Syzkaller build**; if either step fails, the
LLM revises the candidate using the diagnostics and both validation steps are repeated. Descriptions
that pass both steps constitute the *Generated Descriptions*.

</div>

### Targeted Syzkaller Fuzzing

<div align="justify">

The syscall variants defined by the reused descriptions and those defined by the newly validated
descriptions form the enabled call set for driver `D`:

</div>

```
Calls(D) = ExistingCalls(D) ∪ GeneratedCalls(D)
```

<div align="justify">

When no description completion is required, `GeneratedCalls(D) = ∅` and only `ExistingCalls(D)`
remains. The call names in `Calls(D)` are written to the `enable_syscalls` configuration so that
seed generation and mutation focus on the identified driver interfaces. During fuzzing, QEMU
instantiates the device defined by the validated model `M` and driver `D` binds to it in the guest
kernel; Syzkaller generates structured seeds as sequences of calls from `Calls(D)` with concrete
arguments and executes them against the interfaces exposed after binding, while the model handles
the driver's hardware accesses using the responses and state transitions implemented during
behavioral modeling.

</div>

<div align="justify">

To track the kernel code reached by these seeds, the kernel's **KCOV** facility records coverage
that Syzkaller uses to guide subsequent seed generation and mutation. **KASAN** is enabled to report
faults detected during execution, while kernel logs capture additional runtime failures. Each crash
report is saved together with its triggering seed, the corresponding device model and any
successfully minimized reproducer.

</div>

## Configuration and Results

1. All the configurations DevGen needs during its execution are placed in the `config` directory
   (`7.1.config` for the guest kernel, `enable_syscalls.json` mapping drivers to their enabled
   syscall variants, and `create-image.sh` for building the guest image).
2. The `result` directory contains the results of the different model simulations together with
   coverage and crash information.
3. `device_result/` holds the generated device models grouped by LLM backend (`GPT`, `Gemini`,
   `DeepSeek`), and `new_sys_linux/` holds the generated Syzlang descriptions together with their
   extracted `.const` files.

## Datasets and Downloads

Two items are too large to be versioned in this repository and are hosted on Zenodo instead:

- the pre-built QEMU guest image (`bullseye.img` together with its SSH key `bullseye.id_rsa`,
  produced by `fuzzing_integration/image/create-image.sh`), and
- the fuzzing logs captured during the experiments (`fuzzing_integration/log/`).

**Download:** [Zenodo record 23149295](https://zenodo.org/records/23149295?preview=1&token=eyJhbGciOiJIUzUxMiJ9.eyJpZCI6IjMxYWU1ZjFkLTI1NTUtNDJiZC1iYTU1LTM2YjE5NWFjMjc1OCIsImRhdGEiOnt9LCJyYW5kb20iOiJmZDNmNTQxMzEzMmI1YTA0NGNjMTY4OGU2MWQ4Mzk5ZiJ9.um5ERBmcntkq1G97mAU8iap1U6csyl_9B1v_PMg2OqeW9bnLXbCBCsr4elaRxojxBaRAHSmiK-riDHw1fIt7uw)

> <div align="justify">
>
> **Note.** This is currently a preview link to an unpublished Zenodo deposit: the record has no
> registered DOI yet and is reachable only through the access token embedded in the URL above.
> Publish the deposit and replace the link with the permanent URL or DOI so that the downloads stay
> available.
>
> </div>

## 🐛 Kernel Bug Tracker

<div align="justify">

DevGen has found the Linux kernel bugs listed below. Each entry links to its own write-up with the
report, the patch history and the mailing-list threads.

</div>

### 📊 Status Legend

| Symbol | Status               | Description                                                                                              |
| :----: | :------------------- | :------------------------------------------------------------------------------------------------------- |
|   🆕   | **Bug Reported**     | Initial bug report sent to the mailing list, awaiting first response.                                    |
|   👀   | **Bug Confirmed**    | Developers have acknowledged the bug or are actively debugging it.                                       |
|   💬   | **Needs Reply**      | Ongoing discussion requiring follow-up or maintainers have asked questions.                              |
|   🛠️  | **Patch Sent**       | A fix patch has been submitted and is pending review.                                                    |
|    ✅   | **Patch Accepted**   | The patch is approved by maintainers (e.g., `Reviewed-by`) and merged or queued upstream.                |
|    ❌   | **WontFix**          | The bug cannot be fixed (e.g., due to design choices or hardware limits).                                |
|   🔄   | **Indirectly Fixed** | Indirectly resolved through upstream codebase refactoring or unrelated patches.                          |
|   🛡️  | **Crash**            | Crash triggered by privileged API abuse. Upstream considers it intended behavior and not a security bug. |

---

### 📜 Vulnerability List

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

------

### 🔗 Upstream Public Records

<div align="justify">

For transparency and independent verification of our upstream engagements, you can track the complete public mailing list activity of our research team members directly on the Linux Kernel archive (`lore.kernel.org`):

</div>

- 📧 [Mingyu Wang (25181214217@stu.xidian.edu.cn) - Search Results](https://lore.kernel.org/all/?q=25181214217%40stu.xidian.edu.cn)
- 📧 [Mingyu Wang (Alternative) - Search Results](https://lore.kernel.org/all/?q=w15303746062%40163.com)
- 📧 [Zhi Wang - Search Results](https://lore.kernel.org/all/?q=%E7%8E%8B%E5%BF%97)

> <div align="justify">
>
> **Note:** These searches aggregate our patch submissions, bug reports, and technical discussions with kernel maintainers across various subsystem mailing lists.
>
> </div>

