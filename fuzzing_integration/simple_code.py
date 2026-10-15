import json
import os


def filter_json_by_path(data, path_keywords):
    """
    递归过滤JSON数据，删除所有包含"path"字段但不包含指定关键词的对象
    """
    if isinstance(data, dict):
        # Check whether the current dict has a "path" field
        if "path" in data:
            path_value = data["path"]
            # Check whether path contains any of the keywords
            if not any(keyword in path_value for keyword in path_keywords):
                return None  # Does not contain the keyword, delete this object

        # Recursively process all values of the dict
        result = {}
        for key, value in data.items():
            filtered_value = filter_json_by_path(value, path_keywords)
            # For top-level keys, always keep the key even if the value is empty
            if filtered_value is not None:
                result[key] = filtered_value
            else:
                # If the filtered value is None but this is a top-level key, keep the key and set an empty value
                # Check whether it is a top-level dict (judged by checking the key names)
                top_level_keys = ["驱动名称", "驱动核心路径", "宏定义", "enum定义", "enum-typedef定义",
                                  "struct定义", "struct-typedef定义", "var声明", "struct-init声明",
                                  "func声明", "struct-init定义", "func定义"]
                if key in top_level_keys:
                    # For top-level keys of list type, set an empty list
                    if isinstance(value, list):
                        result[key] = []
                    else:
                        result[key] = None

        return result if result else None

    elif isinstance(data, list):
        # Recursively process all elements of the list
        result = []
        for item in data:
            filtered_item = filter_json_by_path(item, path_keywords)
            if filtered_item is not None:
                result.append(filtered_item)
        # Always return a list, even if empty
        return result

    else:
        # Primitive data type, return directly
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
            # If the key does not exist in the filtered data, add it
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
        # Check whether the input file exists
        if not os.path.exists(input_file):
            print(f"警告: 输入文件 '{input_file}' 不存在，跳过处理")
            return False

        # Read the JSON file
        with open(input_file, 'r', encoding='utf-8') as f:
            data = json.load(f)

        print(f"开始过滤JSON数据: {input_file}")
        print(f"过滤关键词: {path_keywords}")

        # Filter the data
        filtered_data = filter_json_by_path(data, path_keywords)

        # If the filtered data is empty, create an empty structure containing all top-level keys
        if filtered_data is None:
            filtered_data = create_empty_structure(data)

        # Ensure all top-level keys are preserved
        filtered_data = ensure_top_level_keys(data, filtered_data)

        # Write the filtered data to a new file
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
        # Read the configuration file
        with open(config_file, 'r', encoding='utf-8') as f:
            config = json.load(f)

        print(f"开始批量处理，共 {len(config)} 个文件需要处理")
        print("=" * 50)

        success_count = 0
        fail_count = 0

        # Process each configuration item one by one
        for key, path_keywords in config.items():
            print(f"\n正在处理: {key}")

            # Build the input and output filenames
            input_dir= "data/100_example_6.18/"
            output_dir= "data/100_example_new_6.18/"
            input_file = input_dir + f"{key}.json"
            output_file = output_dir + f"{key}.json"

            # Process a single file
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
    # Configuration file path
    config_file = "data/100_example.json"  # The JSON file you provided

    # Run the batch processing
    batch_process_json_files(config_file)