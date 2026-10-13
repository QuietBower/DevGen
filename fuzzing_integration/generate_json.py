import json
import os

# 配置参数 - 请根据需要修改这些参数
INPUT_DIRECTORY = ""  # 输入JSON文件所在的目录路径
OUTPUT_PATH = ""  # 输出文件的完整路径


def process_json_files():
    """主处理函数"""
    result = {}

    # 检查输入目录是否存在
    if not os.path.isdir(INPUT_DIRECTORY):
        print(f"错误: 输入目录 '{INPUT_DIRECTORY}' 不存在")
        print("请修改脚本中的 INPUT_DIRECTORY 变量为正确的路径")
        return None

    # 创建输出目录（如果不存在）
    output_dir = os.path.dirname(OUTPUT_PATH)
    if output_dir and not os.path.exists(output_dir):
        try:
            os.makedirs(output_dir, exist_ok=True)
            print(f"创建输出目录: {output_dir}")
        except Exception as e:
            print(f"创建输出目录失败: {str(e)}")
            return None

    # 遍历目录
    processed_count = 0
    skipped_count = 0
    for root, dirs, files in os.walk(INPUT_DIRECTORY):
        for file in files:
            if file.endswith('.json'):
                file_path = os.path.join(root, file)

                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        data = json.load(f)

                    driver_name = data.get("驱动名称")
                    if not driver_name:
                        print(f"警告: {file_path} 中没有找到'驱动名称'键")
                        continue

                    # 处理驱动核心路径
                    core_path = data.get("驱动核心路径", "")
                    if not core_path:
                        print(f"警告: {driver_name} 没有驱动核心路径，跳过此文件")
                        skipped_count += 1
                        continue

                    # 处理核心路径
                    processed_core_path = process_core_path(core_path)
                    if not processed_core_path:
                        print(f"警告: {driver_name} 的核心路径处理失败: {core_path}")
                        skipped_count += 1
                        continue

                    # 处理头文件（如果为空则忽略）
                    processed_header_paths = []
                    header_files = data.get("头文件", [])
                    if header_files:  # 只有当头文件列表不为空时才处理
                        for header in header_files:
                            processed_header = process_header_path(header)
                            if processed_header:
                                processed_header_paths.append(processed_header)
                            else:
                                print(f"警告: {driver_name} 的头文件路径处理失败: {header}")

                    # 构建最终路径列表
                    all_paths = [processed_core_path]
                    all_paths.extend(processed_header_paths)

                    # 添加到结果中
                    result[driver_name] = all_paths
                    processed_count += 1

                except json.JSONDecodeError:
                    print(f"错误: {file_path} 不是有效的JSON文件")
                except Exception as e:
                    print(f"处理文件 {file_path} 时出错: {str(e)}")

    # 写入输出文件
    try:
        with open(OUTPUT_PATH, 'w', encoding='utf-8') as f:
            json.dump(result, f, ensure_ascii=False, indent=2)

        print(f"\n处理完成，结果已保存到: {OUTPUT_PATH}")
        print(f"统计信息:")
        print(f"- 成功处理: {processed_count} 个驱动")
        print(f"- 跳过处理: {skipped_count} 个驱动")

        # 显示详细的统计信息
        if result:
            total_files = sum(len(paths) for paths in result.values())
            print(f"- 总文件数: {total_files} 个路径")

            # 显示每个驱动包含的文件数量
            print("\n各驱动文件数量统计:")
            for driver, paths in result.items():
                c_files = sum(1 for p in paths if p.endswith('.c'))
                h_files = sum(1 for p in paths if p.endswith('.h'))
                print(f"  {driver}: {len(paths)} 个文件 (C: {c_files}, H: {h_files})")
        else:
            print("警告: 没有找到任何有效的JSON文件或处理结果为空")

    except Exception as e:
        print(f"写入输出文件失败: {str(e)}")
        return None

    return result


def process_core_path(path):
    """
    处理驱动核心路径：
    1. 删除"/linux/"之前的所有内容
    2. 删除最后的":"及其后面的所有内容
    3. 只保留以.c或.h结尾的路径

    Args:
        path: 原始路径字符串

    Returns:
        处理后的路径，如果不满足条件则返回None
    """
    if not isinstance(path, str):
        return None

    # 1. 删除"/linux/"之前的所有内容
    if "/linux/" in path:
        linux_index = path.find("/linux/") + 7
        path = path[linux_index:]

    # 2. 删除最后的":"及其后面的所有内容
    colon_index = path.find(':')
    if colon_index != -1:
        path = path[:colon_index]

    # 3. 只保留以.c或.h结尾的路径
    if path.endswith(('.c', '.h')):
        return path

    return None


def process_header_path(path):
    """
    处理头文件路径：
    1. 删除"/linux/"之前的所有内容
    2. 删除最后的":"及其后面的所有内容
    3. 只保留以.c或.h结尾的路径

    Args:
        path: 原始路径字符串

    Returns:
        处理后的路径，如果不满足条件则返回None
    """
    if not isinstance(path, str):
        return None

    # 1. 删除"/linux/"之前的所有内容
    if "/linux/" in path:
        linux_index = path.find("/linux/") + 7
        path = path[linux_index:]

    # 2. 删除最后的":"及其后面的所有内容
    colon_index = path.find(':')
    if colon_index != -1:
        path = path[:colon_index]

    # 3. 只保留以.c或.h结尾的路径
    if path.endswith(('.c', '.h')):
        return path

    return None


if __name__ == "__main__":
    # 显示配置信息
    print("配置信息:")
    print(f"  输入目录: {INPUT_DIRECTORY}")
    print(f"  输出文件: {OUTPUT_PATH}")
    print("-" * 50)

    # 执行处理
    result = process_json_files()