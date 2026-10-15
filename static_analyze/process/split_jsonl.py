import json
import os


def split_jsonl_file(input_file, output_prefix, lines_per_file=1000):
    """
    分割大的JSONL文件为多个小文件

    :param input_file: 输入文件名
    :param output_prefix: 输出文件前缀
    :param lines_per_file: 每个文件包含的行数
    """
    # Ensure the input file exists
    if not os.path.exists(input_file):
        print(f"错误: 输入文件 {input_file} 不存在")
        return

    # Read and split the file
    with open(input_file, 'r', encoding='utf-8') as f:
        file_count = 0
        line_count = 0
        current_file = None

        for line in f:
            # If this is the first line of a new file, create the new file
            if line_count % lines_per_file == 0:
                if current_file is not None:
                    current_file.close()
                file_count += 1
                output_file = f"{output_prefix}_{file_count}.jsonl"
                current_file = open(output_file, 'w', encoding='utf-8')
	    
	    # Write the current line
            try:
                # Validate that the JSON format is correct
                json.loads(line.strip())
                current_file.write(line)
                line_count += 1
            except json.JSONDecodeError:
                print(f"警告: 跳过无效的JSON行: {line.strip()}")


        # Close the last file
        if current_file is not None:
            current_file.close()

    print(f"分割完成! 共生成 {file_count} 个文件，每个文件最多 {lines_per_file} 行")


# Usage example
if __name__ == "__main__":
    input_filename = "func.jsonl"
    output_prefix = "func_part"
    lines_per_file = 500000

    split_jsonl_file(input_filename, output_prefix, lines_per_file)

