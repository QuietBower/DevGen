# DevGen

## 编译新步骤

由于编译最新版的Linux内核版本过高，而编译DevGen的静态分析工具还需要clang-14（尝试更新版本未成功），因此使用以下新的步骤：

DevGen的github仓库：<https://github.com/ise-uiuc/DevGen.git>

首先将filter\_compile\_commands.py、split\_jsonl.py复制到Linux中，用这里的process\_output.py替换DevGen原来的process\_output.py。

## 预处理编译数据库

```bash
python filter_compile_commands.py compile_commands.json -o new_compile_commands.json
```

这会预处理 `compile_commands.json`，得到新的json文件。

## 编译静态分析工具

```bash
make all
```

这会生成 `analyze` 和 `usage` 两个可执行文件。

## 静态分析

### 步骤1

在Linux路径下执行：

```bash
./path/to/analyze -p new_compile_commands.json
```

由于得到的func.jsonl太大，需要分割才可以加载成功。\
执行split\_jsonl.py:

```bash
python path/to/split_jsonl.py
```

正常会将func.jsonl分割为两个文件。

### 步骤2

在analyze所在的目录下执行：

```bash
process_output.py --linux-path /path/to/linux --fnum 3
#                                             这个是分割后的文件数量+1
```

### 步骤3

在Linux路径下执行：

```bash
./path/to/usage -p new_compile_commands.json
```

### 步骤4

在analyze所在的目录下执行：

```bash
python process_output.py --linux-path /path/to/linux --usage
```

至此，静态分析完毕，得到所有的json文件。

## 生成系统调用

```bash
python gen_spec.py -d analyzer/processed_handlers.json -o spec-output -n 1
```

