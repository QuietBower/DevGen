# Driver Fuzzing Integration

This directory implements the **Driver Fuzzing Integration** module of DevGen. It assumes that the target driver already completes its `probe` routine and binds to a validated QEMU device model produced by [../DevGen_Core](../DevGen_Core), and it takes the driver from *"bound to a virtual device"* to *"fuzzed by Syzkaller through the interfaces it exposes"*.

Once binding succeeds, driver testing requires inputs that reach the driver's handlers through the software interfaces exposed to user space. Syzkaller generates and mutates test programs from **Syzlang** descriptions of syscall variants, argument constraints and resource relationships, so an interface whose description is missing, or whose description omits essential constraints, cannot be exercised properly. This module therefore:

1. **identifies the userspace interfaces** that become available after the virtual device is added, and separates the syscall variants already covered by the existing Syzlang corpus from those that are missing or incomplete;
2. **completes the Syzlang descriptions** — an LLM generates the missing syscall variants together with their argument and resource constraints, and the candidates are validated;
3. **runs targeted Syzkaller fuzzing** against the identified interfaces.

## Prerequisites

- A validated QEMU device model for the target driver (produced by [../DevGen_Core](../DevGen_Core)).
- A Linux kernel source tree built with the target config, plus a guest image and its SSH key (see [image/](image/)).
- A QEMU binary and a Syzkaller source tree.
- Python 3 with the packages listed in `requirements.txt` (`transformers`, `rich`, `loguru`, `pyyaml`, `openai`) for the specification-generation scripts.

## Workflow at a Glance

| # | Step                                                      | Entry point                                     |
| - | --------------------------------------------------------- | ----------------------------------------------- |
| 1 | Interface identification                                  | `test_def_qemu_7.1.py`                          |
| 2 | Syzlang description generation and validation             | [spec-gen/](spec-gen/), [spec-eval/](spec-eval/), `process_syscall.py` |
| 3 | Static-analysis simplification and driver code processing | `generate_json.py`, `simple_code.py`            |
| 4 | Targeted fuzzing                                          | `../start_fuzz.py` (repository root)            |

The recommended order is **1 → 2 → 3 → 4**: first run `test_def_qemu_7.1.py` to determine the interfaces newly exposed by the model and the interface differences, then generate and validate the missing Syzlang system-call variants, then simplify the static analysis / process the target driver code, and finally launch the fuzzing campaign.

> Steps 2 and 3 both consume the output of the LLVM static analysis ([spec-gen/analyzer](spec-gen/analyzer), described in step 2), so the analyzer has to have run at least once before `generate_json.py` and `simple_code.py` are used.

## 1. Interface Identification — `test_def_qemu_7.1.py`

This is the interface identification entry point. To locate the candidate device nodes exposed by the generated device, two QEMU runs are compared under otherwise identical conditions, with and without the validated model `M`. Let `N+(D)` and `N-(D)` denote the `/dev` node sets with and without `M`; the candidate node set is

```
ΔN(D) = N+(D) \ N-(D)
```

For every device listed in `test_qemu.json`, the script

1. boots the guest kernel with `-device pcie-root-port,id=rp1 -device <device>,bus=rp1`, i.e. with the generated model instantiated on the root port, and redirects the QEMU output to `log/vm_<device>.log`;
2. checks the binding status of the device at PCI address `01:00.0` over SSH with `lspci -k -s 01:00.0`; the driver is considered bound when the output contains `Kernel driver in use` (this is the binding criterion used in the paper);
3. when binding succeeds, captures `tree /dev` inside the guest, saves it to `dev_txt/<device>.txt`, and diffs it against the baseline snapshot `basedev.txt` (the `/dev` tree of the guest *without* the model) using `difflib.ndiff`, keeping the added/removed lines as the interface difference;
4. accumulates the diff into `changedev.json` (refreshed after every device) and writes the binding result back into `test_qemu.json`.

The device names coming from `test_qemu.json` are exactly the identification nodes: the name is used both as the QEMU `-device` identifier and as the key under which the interface difference is recorded, so an entry such as `atl1e` selects the corresponding model, checks which driver binds to it, and records the `/dev` nodes it newly exposes. Interfaces that do not create a new `/dev` node (debugfs nodes, or shared nodes such as `/dev/ptmx`) cannot be seen by this diff; they are identified through source registration analysis and predefined driver-to-Syzlang mappings, as described in step 2.

Configuration at the top of the script:

```python
JSON_FILE       = "test_qemu.json"       # devices to test; also receives the binding result
SSH_KEY         = ".../bullseye.id_rsa"  # guest image key
SSH_PORT        = "10021"
BASE_DEV_TXT    = ".../basedev.txt"      # baseline /dev tree (guest without the model)
CHANGE_DEV_JSON = ".../changedev.json"   # accumulated ΔN(D), one entry per device
```

```bash
python3 test_def_qemu_7.1.py
```

[test_QEMU/](test_QEMU/)/*.sh holds standalone QEMU launch scripts for individual devices; they are useful to reproduce a single boot manually (the script also documents the configurable parameters).

The resulting `changedev.json` — the newly exposed interfaces — together with the collected source context is what the next step consumes: the operations behind these interfaces are matched against the existing Syzlang corpus, and the uncovered ones become the missing syscall variants.

## 2. Syzlang Description Generation and Validation

The operations reachable through the interfaces identified in step 1 are matched against the existing Syzlang corpus. Variants that already have a description are reused; variants that lack a description, or whose description omits essential constraints, are completed here.

### [spec-gen/](spec-gen/) — generate the missing Syzlang system-call variants

For each operation, [spec-gen/](spec-gen/) recovers the semantics from the driver context along three axes that mirror the paper's description-completion step:

- **Identifier deduction** — the device path, the opening method and the operation identifier (`step0_template_open.txt` → `step1_template_cmd.txt`);
- **Type recovery** — argument direction, primitive type, pointer indirection, structures and length constraints inferred from the command encodings and the `copy_from_user` / `copy_to_user` transfers (`step2_template_arg.txt` → `step3_template_type.txt`);
- **Dependency analysis** — resource relationships and the call ordering they impose, e.g. the fd resource produced by `syz_open_dev$DEVGEN_*` and consumed by a later `ioctl$DEVGEN_*` (`step4_template_ret.txt`).

| File                             | Role                                                                                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `gen_spec.py`                    | entry point; drives the step-by-step prompt chain and writes the generated specs                                                                              |
| `gen_utils.py`, `output_spec.py` | assemble the Syzlang text (resource + open + syscall + types)                                                                                                 |
| `find_utils.py`                  | looks up struct/enum/function/usage definitions from the analyzer output                                                                                      |
| `llm_utils.py`                   | LLM backend                                                                                                                                                   |
| `repair_spec.py`                 | repairs a candidate spec using the validator's stdout                                                                                                         |
| [prompt_template/](spec-gen/prompt_template/)               | the step prompts (`step0` … `step4`, plus `step-repair-struct`)                                                                                               |
| [existings/](spec-gen/existings/)                     | the existing corpus used to skip already-covered names (`cmds.txt`, `dev_files.txt`, `fds.txt`, `filenames.txt`, `resources.txt`, `sockopt.txt`, `types.txt`) |
| [analyzer/](spec-gen/analyzer/)                      | LLVM/Clang analyzer producing the definition/usage JSON consumed by `find_utils.py`                                                                           |

Typical invocation (paths relative to this directory):

```bash
python gen_spec.py -d analyzer/processed_handlers.json -o spec-output -n 1
#                 --data  handler/ioctl JSON   --output  dir   --num  specs to generate
```

Prerequisite — the analyzer pipeline that produces `analyzer/processed_*.json`:

```bash
# 1. preprocess the compilation database
python filter_compile_commands.py compile_commands.json -o new_compile_commands.json

# 2. build the analyzer (produces the `analyze` and `usage` executables)
cd spec-gen/analyzer && make all

# 3. run it under the Linux source tree, split the oversized func.jsonl, then process the output
./path/to/analyze -p new_compile_commands.json
python /path/to/split_jsonl.py
python process_output.py --linux-path /path/to/linux --fnum 3   # number of split files + 1
./path/to/usage   -p new_compile_commands.json
python process_output.py --linux-path /path/to/linux --usage
# -> analyzer/processed_{struct,enum,func,usage,handlers}.json
```

### [spec-eval/](spec-eval/) — validate the generated syscalls

[spec-eval/](spec-eval/) validates the generated descriptions. A candidate description is accepted only after it passes **Syzlang compilation followed by a Syzkaller build**; if either step fails, the candidate is repaired from the diagnostics (`spec-gen/repair_spec.py`, orchestrated by `spec-gen/eval_spec.py`) and both steps are repeated.

The validation harness prepares its own environment and runs Syzkaller with a given spec set:

```bash
python run-specs.py -d DIRECTORY |& tee run-specs.log
```

It shallow-clones `linux` with the provided kernel config, shallow-clones a separate `syzkaller` per run declared in `config.yaml`, copies the candidate specs into each `syzkaller/sys/linux` tree and builds it, injects the enabled calls into the run configuration (`template-valid.cfg` for a validity run, `template-all.cfg` for the full set, `template-repro.cfg` for reproducers), and runs `syzkaller.sh` until the timeout. Helper scripts: `add-new-spec.sh` (drop a spec into a syzkaller tree, replacing an existing file name), `shallow-clone.sh`, `apply-patch.sh`, `syzkaller-enabling.patch`, `linux-d2f51b35.patch`.

Descriptions that pass both validation steps are the generated descriptions used by the fuzzing step.

### `process_syscall.py` — compile the newly generated system calls

Once the generated specs are validated, `process_syscall.py` integrates them into the Syzkaller build used for fuzzing. For every `*.txt` spec in the generated-spec directory it

1. copies the spec into `syzkaller/sys/linux`,
2. runs `bin/syz-extract -os linux -sourcedir <kernel> -arch amd64 <spec>`, which extracts the constants referenced by the description from the kernel source,
3. runs `bin/syz-sysgen` to regenerate the syscall tables and descriptions,
4. periodically runs `make`, `make clean` and `make bin/syz-extract` to rebuild the Syzkaller binaries.


Specs that fail `syz-extract` are moved aside to an error directory instead of being kept. The paths at the top of the file (`source_dir`, `target_dir`, `syzkaller_dir`, `kernel_source`, `error_dir`) must be adjusted for the local setup:

```bash
python3 process_syscall.py
```

At this point the generated system calls are part of the Syzkaller build and can be enabled per driver in step 4.

## 3. Static-Analysis Simplification and Driver Code Processing

The raw static-analysis output covers the whole kernel and is too large to hand to the LLM directly. These two scripts reduce it to the driver-level JSON and driver-scoped code that is used downstream as the source context for interface association and description completion.

### `python3 generate_json.py` — simplify the static analysis into one JSON file

Walks a directory of per-driver JSON files and flattens them into a single mapping `{ driver_name: [paths...] }`:

- reads `驱动名称` (driver name), `驱动核心路径` (core path) and `头文件` (headers) from each file;
- normalises every path (drops everything before `/linux/`, drops the trailing `:line` suffix, and keeps only `.c` / `.h` paths);
- skips entries without a core path and reports per-driver file counts at the end.

Set the two variables at the top of the script before running:

```python
INPUT_DIRECTORY = ""   # directory holding the per-driver JSON files
OUTPUT_PATH     = ""   # full path of the resulting JSON file
```

```bash
python3 generate_json.py
```

### `python3 simple_code.py` — process the target driver code

Consumes the generated JSON and keeps only the declarations and definitions that belong to the target driver itself. For each driver it walks the corresponding static-analysis JSON and drops every object whose `path` field does not contain any of the configured path keywords, while preserving all top-level keys (`驱动名称`, `驱动核心路径`, `宏定义`, `enum定义`, `enum-typedef定义`, `struct定义`, `struct-typedef定义`, `var声明`, `struct-init声明`, `func声明`, `struct-init定义`, `func定义`). The result is a compact, driver-scoped context.

```python
config_file = "data/100_example.json"          # { driver: [path keywords] }
# input  : data/100_example_6.18/<driver>.json
# output : data/100_example_new_6.18/<driver>.json
```

```bash
python3 simple_code.py
```

## 4. Targeted Syzkaller Fuzzing — `../start_fuzz.py`

> The fuzzing driver lives at the **repository root** (`../start_fuzz.py`), not in this directory. It resolves every input and output through the absolute paths configured at the top of the file, so it can be launched from anywhere. Adjust those paths before running:

```python
FUZZ_PATH           = "/home/user/Fuzzer/code/config/2_gpt-5.1-fuzz.json"   # per-device fuzzing config
ENABLE_SYSCALL_PATH = "/home/user/Fuzzer/code/config/enable_syscalls.json"  # driver -> enabled syscall variants
DIR_A               = "/home/user/Fuzzer/third_tool/syzkaller_syzbot_nodev"      # syzbot config, without model
DIR_B               = "/home/user/Fuzzer/third_tool/syzkaller_syzbot_dev"        # syzbot config, with model
DIR_C               = "/home/user/Fuzzer/third_tool/syzkaller_defconfig_nodev"   # defconfig, without model
DIR_D               = "/home/user/Fuzzer/third_tool/syzkaller_defconfig_dev"     # defconfig, with model
SYZBOT_OUT_DIR      = "/home/user/Fuzzer/out/fuzz_result/3_Gemini_3pro"
NON_SYZBOT_OUT_DIR  = "/home/user/Fuzzer/out/fuzz_result/3_Gemini_3pro"
```

For each device in `FUZZ_PATH` the script:

1. releases ports 10001–10004, in case a previous `syz-manager` is still listening;
2. skips devices whose `enable_syscalls` is already populated;
3. builds the enabled call set — the union `Calls(D) = ExistingCalls(D) ∪ GeneratedCalls(D)` — by walking `enable_syscalls.json` in its original order and, for every entry whose `drivers` field matches the device path, appending its `syscalls` variants (deduplicated, order preserved);
4. launches **two `syz-manager` instances per device**: one *without* the device model (`nodev`, config pair A/C) and one *with* it (`dev`, pair B/D, appending `-device pcie-root-port,id=rp1 -device <device>_pci,bus=rp1` to `vm.qemu_args`). The pair is selected by the device's `in_syzbot_config` flag (`y` → syzbot configs, `n` → defconfig);
5. writes `enable_syscalls` into a copy of the workdir `cfg.json`, runs `./bin/syz-manager -config=<cfg> -bench=<bench>` with a 60-minute timeout per instance (killing the process group when it expires), and archives the used config next to the benchmark result.

```bash
# run from the repository root
python3 start_fuzz.py
```

The with/without-model pairing isolates the contribution of the generated device to coverage and crashes. During each run QEMU instantiates the validated model and the driver binds to it; Syzkaller executes structured seeds built from `Calls(D)`, KCOV records coverage, KASAN reports faults, and the kernel log captures additional runtime failures. Crashes are archived with their triggering seed, the corresponding device model and any minimized reproducer.

## Supporting Files

| Path                                                                           | Role                                                                           |
| ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------ |
| [image/](image/) (`create-image.sh`)                                                   | build the Debian bullseye guest image (`bullseye.img` + `bullseye.id_rsa`)     |
| [kernel-configs/](kernel-configs/)                                                              | guest kernel configs (`syzbot.config`, `syzbot-kmsan.config`, `allyesconfig*`) |
| `filter_compile_commands.py`, `split_jsonl.py`, `process_output.py`            | static-analysis preprocessing (see step 2)                                     |
| `syzkaller.sh`, `template.cfg`, `test.cfg`, `get-port.py`, `ssh.sh`, `qemu.sh` | run a single `syz-manager` or QEMU instance manually                           |
| `crash_watcher.sh`                                                             | poll `crashes/description` files and mail newly found crashes                  |
| [test_QEMU/](test_QEMU/)                                                                   | standalone per-device QEMU launch scripts                                      |
| [dev_txt/](dev_txt/), [log/](log/)                                                             | `tree /dev` snapshots and QEMU logs captured by step 1                         |
| [spec-gen/](spec-gen/), [spec-eval/](spec-eval/)                                                      | description generation and validation (see step 2)                             |

## Notes

- The generated Syzlang descriptions are collected under [../new_sys_linux/](../new_sys_linux/) in the repository root; the paper reports 438 call descriptions generated or completed.
- `enable_syscalls` defaults to all available variants when the relevant calls cannot be clearly determined, matching the behaviour described in the paper.
