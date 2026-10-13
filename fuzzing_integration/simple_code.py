import json
import os


def filter_json_by_path(data, path_keywords):
    """
    递归过滤JSON数据，删除所有包含"path"字段但不包含指定关键词的对象
    """
    if isinstance(data, dict):
        # 检查当前字典是否有"path"字段
        if "path" in data:
            path_value = data["path"]
            # 检查path是否包含任何关键词
            if not any(keyword in path_value for keyword in path_keywords):
                return None  # 不包含关键词，删除该对象

        # 递归处理字典的所有值
        result = {}
        for key, value in data.items():
            filtered_value = filter_json_by_path(value, path_keywords)
            # 对于顶级键，始终保留键，即使值为空
            if filtered_value is not None:
                result[key] = filtered_value
            else:
                # 如果过滤后的值为None，但这是顶级键，我们保留键并设置空值
                # 检查是否是顶级字典（通过检查键名来判断）
                top_level_keys = ["驱动名称", "驱动核心路径", "宏定义", "enum定义", "enum-typedef定义",
                                  "struct定义", "struct-typedef定义", "var声明", "struct-init声明",
                                  "func声明", "struct-init定义", "func定义"]
                if key in top_level_keys:
                    # 对于列表类型的顶级键，设置为空列表
                    if isinstance(value, list):
                        result[key] = []
                    else:
                        result[key] = None

        return result if result else None

    elif isinstance(data, list):
        # 递归处理列表的所有元素
        result = []
        for item in data:
            filtered_item = filter_json_by_path(item, path_keywords)
            if filtered_item is not None:
                result.append(filtered_item)
        # 始终返回列表，即使为空
        return result

    else:
        # 基本数据类型，直接返回
        return data


def create_empty_structure(original_data):
    """
    根据原始数据创建包含所有顶级键的空结构
    """
    empty_structure = {}
    for key, value in original_data.items():
        if isinstance(value, list):
            empty_structure[key] = []
        else:
            empty_structure[key] = None
    return empty_structure


def ensure_top_level_keys(original_data, filtered_data):
    """
    确保过滤后的数据包含所有原始数据的顶级键
    """
    if filtered_data is None:
        filtered_data = {}

    for key in original_data.keys():
        if key not in filtered_data:
            # 如果键在过滤后的数据中不存在，添加它
            original_value = original_data[key]
            if isinstance(original_value, list):
                filtered_data[key] = []
            else:
                filtered_data[key] = None

    return filtered_data


def process_single_json_file(input_file, output_file, path_keywords):
    """
    处理单个JSON文件的函数
    """
    try:
        # 检查输入文件是否存在
        if not os.path.exists(input_file):
            print(f"警告: 输入文件 '{input_file}' 不存在，跳过处理")
            return False

        # 读取JSON文件
        with open(input_file, 'r', encoding='utf-8') as f:
            data = json.load(f)

        print(f"开始过滤JSON数据: {input_file}")
        print(f"过滤关键词: {path_keywords}")

        # 过滤数据
        filtered_data = filter_json_by_path(data, path_keywords)

        # 如果过滤后数据为空，创建一个包含所有顶级键的空结构
        if filtered_data is None:
            filtered_data = create_empty_structure(data)

        # 确保所有顶级键都被保留
        filtered_data = ensure_top_level_keys(data, filtered_data)

        # 写入过滤后的数据到新文件
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(filtered_data, f, ensure_ascii=False, indent=2)

        print(f"过滤完成! 结果已保存到: {output_file}")
        return True

    except Exception as e:
        print(f"处理文件 '{input_file}' 时发生错误: {e}")
        return False


def batch_process_json_files(config_file):
    """
    批量处理JSON文件的主函数
    """
    try:
        # 读取配置文件
        with open(config_file, 'r', encoding='utf-8') as f:
            config = json.load(f)

        print(f"开始批量处理，共 {len(config)} 个文件需要处理")
        print("=" * 50)

        success_count = 0
        fail_count = 0

        # 逐个处理每个配置项
        for key, path_keywords in config.items():
            print(f"\n正在处理: {key}")

            # 构建输入输出文件名
            input_dir= "data/100_example_6.18/"
            output_dir= "data/100_example_new_6.18/"
            input_file = input_dir + f"{key}.json"
            output_file = output_dir + f"{key}.json"

            # 处理单个文件
            if process_single_json_file(input_file, output_file, path_keywords):
                success_count += 1
            else:
                fail_count += 1

        print("\n" + "=" * 50)
        print(f"批量处理完成!")
        print(f"成功: {success_count} 个文件")
        print(f"失败: {fail_count} 个文件")

    except Exception as e:
        print(f"批量处理时发生错误: {e}")
        return False


if __name__ == "__main__":
    # 配置文件路径
    config_file = "data/100_example.json"  # 您提供的JSON文件

    # 执行批量处理
    batch_process_json_files(config_file)