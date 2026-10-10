import json
import os


def split_jsonl_file(input_file, output_prefix, lines_per_file=1000):
    """
    分割大的JSONL文件为多个小文件

    :param input_file: 输入文件名
    :param output_prefix: 输出文件前缀
    :param lines_per_file: 每个文件包含的行数
    """
    # 确保输入文件存在
    if not os.path.exists(input_file):
        print(f"错误: 输入文件 {input_file} 不存在")
        return

    # 读取并分割文件
    with open(input_file, 'r', encoding='utf-8') as f:
        file_count = 0
        line_count = 0
        current_file = None

        for line in f:
            # 如果是新文件的第一行，创建新文件
            if line_count % lines_per_file == 0:
                if current_file is not None:
                    current_file.close()
                file_count += 1
                output_file = f"{output_prefix}_{file_count}.jsonl"
                current_file = open(output_file, 'w', encoding='utf-8')
	    
	    # 写入当前行
            try:
                # 验证JSON格式是否正确
                json.loads(line.strip())
                current_file.write(line)
                line_count += 1
            except json.JSONDecodeError:
                print(f"警告: 跳过无效的JSON行: {line.strip()}")


        # 关闭最后一个文件
        if current_file is not None:
            current_file.close()

    print(f"分割完成! 共生成 {file_count} 个文件，每个文件最多 {lines_per_file} 行")


# 使用示例
if __name__ == "__main__":
    input_filename = "func.jsonl"
    output_prefix = "func_part"
    lines_per_file = 500000

    split_jsonl_file(input_filename, output_prefix, lines_per_file)

