# DevGen

## Updated Compilation Steps

Because the latest Linux kernel version is too new to compile, and compiling DevGen's static analysis tool still requires clang-14 (attempts to upgrade the version were unsuccessful), the following revised steps are used:

First, copy `filter_compile_commands.py` and `split_jsonl.py` into Linux, and replace DevGen's original `process_output.py` with the `process_output.py` from this directory.

## Preprocessing the Compilation Database

```bash
python filter_compile_commands.py compile_commands.json -o new_compile_commands.json
```

This preprocesses `compile_commands.json` and produces a new JSON file.

## Building the Static Analysis Tool

```bash
make all
```

This generates two executables: `analyze` and `usage`.

## Static Analysis

### Step 1

Run under the Linux path:

```bash
./path/to/analyze -p new_compile_commands.json
```

Because the resulting `func.jsonl` is too large, it must be split before it can be loaded successfully.\
Run `split_jsonl.py`:

```bash
python path/to/split_jsonl.py
```

Normally this splits `func.jsonl` into two files.

### Step 2

Run in the directory where `analyze` resides:

```bash
process_output.py --linux-path /path/to/linux --fnum 3
#                                             this is the number of split files + 1
```

### Step 3

Run under the Linux path:

```bash
./path/to/usage -p new_compile_commands.json
```

### Step 4

Run in the directory where `analyze` resides:

```bash
python process_output.py --linux-path /path/to/linux --usage
```

At this point the static analysis is complete and all JSON files have been produced.

## Generating System Calls

```bash
python gen_spec.py -d analyzer/processed_handlers.json -o spec-output -n 1
```
