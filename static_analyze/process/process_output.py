# 读取 .jsonl（每行一个 JSON 对象）格式的中间结果文件，
# 进行进一步的归类、去重、合并和过滤。
import json
from pathlib import Path
import re
import os
# 日志输出
from loguru import logger
# 命令行参数解析
from argparse import ArgumentParser

# Linux 源码的根目录路径，由命令行参数传入
linux_path = None
# 需要跳过的子系统路径列表
SKIP_SUBSYSTEMS = [
    "linux/net/",
    "linux/fs/",
    "linux/virt/",
]

#递归地将字符串中的 \n 和 \t 转义
def debug_output(data_dict, output_path: Path):
    # Recursively replace the value: \n -> \\n, \t -> \\t
    def replace_value(data):
        if isinstance(data, dict):
            # 如果数据是字典类型
            for key, value in data.items():
                # 对字典中的每个键值对进行递归替换
                data[key] = replace_value(value)
        elif isinstance(data, list):
            # 如果数据是列表类型
            for i in range(len(data)):
                # 对列表中的每个元素进行递归替换
                data[i] = replace_value(data[i])
        elif isinstance(data, str):
            # 如果数据是字符串类型
            # 将字符串中的换行符替换为双反斜杠加 n
            data = data.replace("\n", "\\n")
            # 将字符串中的制表符替换为双反斜杠加 t
            data = data.replace("\t", "\\t")
        return data

    # 对输入的数据字典进行递归替换
    data_dict = replace_value(data_dict)
    # 将替换后的数据字典以 JSON 格式写入指定的输出路径
    output_path.write_text(json.dumps(data_dict, indent=2))


# 用正则表达式从代码片段中提取 ioctl 函数名
def extract_ioctl_function_name(code: str) -> str:
    """Extract the ioctl function name from the code."""
    pattern = r"\.(unlocked_)?ioctl\s*=\s*(\w+)[,\n]"
    match = re.search(pattern, code)
    if match:
        return match.group(2)
    return ""


# 计算两个路径的相似度（用于后续合并最相近的 typedef 源码）
def path_similarity(path1, path2):
    """Calculate the similarity of two paths based on their components."""
    components1 = path1.split(os.sep)
    components2 = path2.split(os.sep)

    # Count the common components
    common_components = len(set(components1) & set(components2))
    total_components = len(set(components1) | set(components2))

    # Simple ratio of common components to total unique components
    return common_components / total_components


# 处理 ioctl.jsonl 文件，提取 ioctl 相关的 handler 信息，
# 归类、过滤并输出为 JSON 文件和文本列表。
def process_ioctl_handler():
    # 定义一个函数，用于输出数据到文件
    def output_data(data_dict, name_suffix=""):
        ioctl_names = list(data_dict.keys())
        ioctl_names.sort()
        handler_names = []
        for _, handler_data in data_dict.items():
            handler_names.extend(list(handler_data.keys()))
        handler_names = list(set(handler_names))
        handler_names.sort()
        # 将ioctl名称写入文件
        Path(f"ioctl_names{name_suffix}.txt").write_text("\n".join(ioctl_names))
        # 将handler名称写入文件
        Path(f"handler_names{name_suffix}.txt").write_text(
            "\n".join(handler_names)
        )

    # 指定jsonl文件的路径
    jsonl_path = linux_path / "ioctl.jsonl"

    # 用于存储ioctl数据的字典
    ioctl_data = {}
    # 用于存储handler数据的字典
    ops_data = {}
    for line in jsonl_path.read_text().splitlines():
        data = json.loads(line)
        filename = data["filename"] # 文件名
        source = data["source"]     # 源代码
        handler_name = data["name"] # 结构名

        if handler_name not in ops_data:
            ops_data[handler_name] = {}
        ops_data[handler_name][filename] = source

        # 正则方法匹配ioctl函数名
        ioctl_name = extract_ioctl_function_name(source)
        if not ioctl_name:
            logger.warning(f"Cannot find ioctl function name in {filename}")
            continue

        if ioctl_name not in ioctl_data:
            ioctl_data[ioctl_name] = {}

        filepath = linux_path / filename.split(":")[0]
        abs_filename = filepath.absolute().resolve().as_posix()
        if handler_name not in ioctl_data[ioctl_name]:
            ioctl_data[ioctl_name][handler_name] = {}

        ioctl_data[ioctl_name][handler_name][abs_filename] = source

    # 将handler数据存储到文件
    ops_data_path = Path("processed_handlers.json")
    ops_data_path.write_text(json.dumps(ops_data, indent=2))
    # 输出handler数据的调试信息
    debug_output(ops_data, ops_data_path.with_suffix(".debug.json"))

    # 将ioctl数据存储到json文件
    json_path = Path("processed_ioctl.json")
    json_path.write_text(json.dumps(ioctl_data, indent=2))

    # 打印ioctl总数
    print(f"Total number of ioctl: {len(ioctl_data)}")

    # 过滤后的ioctl数据
    filtered_ioctl_data = {}
    for ioctl_name, handler_data in ioctl_data.items():
        cur_ioctl_data = {}
        for handler_name, filename_data in handler_data.items():
            handler_data = {}
            for filename, source in filename_data.items():
                skip = False
                for subsystem in SKIP_SUBSYSTEMS:
                    if subsystem in filename:
                        skip = True
                if skip:
                    continue
                handler_data[filename] = source
            if handler_data:
                cur_ioctl_data[handler_name] = handler_data
        if cur_ioctl_data:
            filtered_ioctl_data[ioctl_name] = cur_ioctl_data

    # 将过滤后的ioctl数据存储到json文件
    json_path = Path("processed_ioctl_filtered.json")
    json_path.write_text(json.dumps(filtered_ioctl_data, indent=2))

    # 打印过滤后的ioctl总数
    print(f"Total number of ioctl after filtering: {len(filtered_ioctl_data)}")

    # 输出ioctl名称和handler名称
    output_data(ioctl_data)
    # 输出过滤后的ioctl名称和handler名称
    output_data(filtered_ioctl_data, "_filtered")


#处理结构体、枚举、函数等类型定义的 jsonl 文件，归类并输出为 JSON 文件。
#读取如 struct.jsonl、enum.jsonl、func.jsonl 文件。
def process_func(file_name: str, nums):
    # 初始化一个空字典用于存储类型数据
    # The following dict {type_name: {filename: source}}}
    type_data = {}
    for num in range(1,nums):
        # 拼接文件路径
        file_path = linux_path / f'func_part_{num}.jsonl'
        print(f"正在读取{file_path}")
        file_data = file_path.read_text()
        print(f"读取{file_path}成功")
        for line in file_data.splitlines():
            line = line.strip()
            data = json.loads(line)
            type_name = data["name"]
            source_file_name = data["filename"]
            source_file_path = linux_path / source_file_name
            # 获取源文件的绝对路径
            source_file_name = source_file_path.absolute().resolve().as_posix()
            source = data["source"]
            if type_name not in type_data:
                type_data[type_name] = {}
            # 将源文件路径和对应的源代码存储到类型数据中
            type_data[type_name][source_file_name] = source

    # 将处理后的数据写入到一个JSON文件中
    json_path = Path(f"processed_func.json")
    json_path.write_text(json.dumps(type_data, indent=2))

    return type_data


def process_type(file_name: str):
    file_path = linux_path / file_name

    # The following dict {type_name: {filename: source}}}
    type_data = {}
    for line in file_path.read_text().splitlines():
        line = line.strip()
        data = json.loads(line)
        type_name = data["name"]
        source_file_name = data["filename"]
        source_file_path = linux_path / source_file_name
        source_file_name = source_file_path.absolute().resolve().as_posix()
        source = data["source"]
        if type_name not in type_data:
            type_data[type_name] = {}
        type_data[type_name][source_file_name] = source

    # Write the data to a json file
    json_path = Path(f"processed_{file_path.stem}.json")
    json_path.write_text(json.dumps(type_data, indent=2))

    return type_data



#处理 typedef 信息（如结构体别名、枚举别名），并尝试合并别名和原类型的源码内容。
def process_typedef(file_name: str, type_data: dict):
    file_path = linux_path / file_name

    # The following dict {type_name: {filename: source}}}
    typedef_data = {}
    for line in file_path.read_text().splitlines():
        line = line.strip()
        data = json.loads(line)
        type_name = data["name"]
        alias_name = data["alias"]
        source_file_name = data["filename"]
        source_file_path = linux_path / source_file_name
        # 将源文件路径转换为绝对路径并解析符号链接
        source_file_name = source_file_path.absolute().resolve().as_posix()
        source = data["source"]
        if type_name not in typedef_data:
            # 如果类型名称不在typedef_data字典中，则添加该类型名称的键
            typedef_data[type_name] = {}

        if alias_name and alias_name in type_data:
            # 如果存在别名，并且在type_data字典中存在该别名
            alias_data = type_data[alias_name]
            # 找到与源文件路径最相似的路径
            most_similar = max(
                alias_data, key=lambda x: path_similarity(source_file_name, x)
            )
            source_most_similar = alias_data[most_similar]
            # 将最相似的源代码与当前源代码合并
            source = source + "\n" + source_most_similar

        # 将处理后的源代码添加到typedef_data字典中
        typedef_data[type_name][source_file_name] = source

    # 将处理后的数据写入json文件
    # Write the data to a json file
    json_path = Path(f"processed_{file_path.stem}.json")
    json_path.write_text(json.dumps(typedef_data, indent=2))

# 处理 usage.jsonl 文件，归类 fops（file operations）相关的使用信息。
def process_usage():
    file_path = linux_path / "usage.jsonl"

    # 初始化 usage_data 字典，结构为 {fops_name: {filename: [source]}}
    usage_data = {}
    for line in file_path.read_text().splitlines():
        line = line.strip()
        data = json.loads(line)
        fops_name = data["alias"]
        source_file_name = data["filename"]
        source_file_path = linux_path / source_file_name
        # 获取文件的绝对路径并转换为POSIX格式
        source_file_name = source_file_path.absolute().resolve().as_posix()
        source = data["source"]
        if fops_name not in usage_data:
            # 如果 fops_name 不在 usage_data 中，则初始化一个空字典
            usage_data[fops_name] = {}
        # 将 source 添加到对应的 filename 中
        usage_data[fops_name][source_file_name] = source

    # 将数据写入 JSON 文件
    # Write the data to a json file
    json_path = Path("processed_usage.json")
    json_path.write_text(json.dumps(usage_data, indent=2))


if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument("--linux-path", type=str, required=True)
    parser.add_argument("--fnum", type=int, default=2)
    parser.add_argument("--usage", action="store_true")
    args = parser.parse_args()

    linux_path = Path(args.linux_path)

    # 如果加了 --usage 参数，则只处理 usage 信息。否则，处理所有信息。
    if args.usage:
        print("usage")
        process_usage()
    else:
        process_ioctl_handler()
        print('finish_ioctl')
        type_data = process_type("struct.jsonl")
        process_typedef("struct-typedef.jsonl", type_data)
        print('finish_struct')
        type_data = process_type("enum.jsonl")
        process_typedef("enum-typedef.jsonl", type_data)
        print('finish_enum')
        type_data = process_func("func.jsonl", args.fnum)
        print('finish_func')

