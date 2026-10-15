# Read intermediate result files in .jsonl format (one JSON object per line),
# and perform further classification, deduplication, merging and filtering.
import json
from pathlib import Path
import re
import os
# Logging output
from loguru import logger
# Command-line argument parsing
from argparse import ArgumentParser

# Root path of the Linux source tree, passed in via command-line arguments
linux_path = None
# List of subsystem paths to skip
SKIP_SUBSYSTEMS = [
    "linux/net/",
    "linux/fs/",
    "linux/virt/",
]

# Recursively escape \n and \t in strings
def debug_output(data_dict, output_path: Path):
    # Recursively replace the value: \n -> \\n, \t -> \\t
    def replace_value(data):
        if isinstance(data, dict):
            # If the data is a dict
            for key, value in data.items():
                # Recursively replace each key-value pair in the dict
                data[key] = replace_value(value)
        elif isinstance(data, list):
            # If the data is a list
            for i in range(len(data)):
                # Recursively replace each element in the list
                data[i] = replace_value(data[i])
        elif isinstance(data, str):
            # If the data is a string
            # Replace newline characters with a double backslash followed by n
            data = data.replace("\n", "\\n")
            # Replace tab characters with a double backslash followed by t
            data = data.replace("\t", "\\t")
        return data

    # Recursively replace the input data dict
    data_dict = replace_value(data_dict)
    # Write the replaced data dict to the specified output path in JSON format
    output_path.write_text(json.dumps(data_dict, indent=2))


# Extract the ioctl function name from a code snippet using a regular expression
def extract_ioctl_function_name(code: str) -> str:
    """Extract the ioctl function name from the code."""
    pattern = r"\.(unlocked_)?ioctl\s*=\s*(\w+)[,\n]"
    match = re.search(pattern, code)
    if match:
        return match.group(2)
    return ""


# Compute the similarity of two paths (used later to merge the most similar typedef source)
def path_similarity(path1, path2):
    """Calculate the similarity of two paths based on their components."""
    components1 = path1.split(os.sep)
    components2 = path2.split(os.sep)

    # Count the common components
    common_components = len(set(components1) & set(components2))
    total_components = len(set(components1) | set(components2))

    # Simple ratio of common components to total unique components
    return common_components / total_components


# Process the ioctl.jsonl file, extract ioctl-related handler information,
# classify, filter and output as a JSON file and text lists.
def process_ioctl_handler():
    # Define a function that outputs data to a file
    def output_data(data_dict, name_suffix=""):
        ioctl_names = list(data_dict.keys())
        ioctl_names.sort()
        handler_names = []
        for _, handler_data in data_dict.items():
            handler_names.extend(list(handler_data.keys()))
        handler_names = list(set(handler_names))
        handler_names.sort()
        # Write the ioctl names to a file
        Path(f"ioctl_names{name_suffix}.txt").write_text("\n".join(ioctl_names))
        # Write the handler names to a file
        Path(f"handler_names{name_suffix}.txt").write_text(
            "\n".join(handler_names)
        )

    # Specify the path of the jsonl file
    jsonl_path = linux_path / "ioctl.jsonl"

    # Dict used to store ioctl data
    ioctl_data = {}
    # Dict used to store handler data
    ops_data = {}
    for line in jsonl_path.read_text().splitlines():
        data = json.loads(line)
        filename = data["filename"] # Filename
        source = data["source"]     # Source code
        handler_name = data["name"] # Struct name

        if handler_name not in ops_data:
            ops_data[handler_name] = {}
        ops_data[handler_name][filename] = source

        # Regex method to match the ioctl function name
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

    # Store the handler data to a file
    ops_data_path = Path("processed_handlers.json")
    ops_data_path.write_text(json.dumps(ops_data, indent=2))
    # Output debug information for the handler data
    debug_output(ops_data, ops_data_path.with_suffix(".debug.json"))

    # Store the ioctl data to a json file
    json_path = Path("processed_ioctl.json")
    json_path.write_text(json.dumps(ioctl_data, indent=2))

    # Print the total number of ioctls
    print(f"Total number of ioctl: {len(ioctl_data)}")

    # Filtered ioctl data
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

    # Store the filtered ioctl data to a json file
    json_path = Path("processed_ioctl_filtered.json")
    json_path.write_text(json.dumps(filtered_ioctl_data, indent=2))

    # Print the total number of filtered ioctls
    print(f"Total number of ioctl after filtering: {len(filtered_ioctl_data)}")

    # Output the ioctl names and handler names
    output_data(ioctl_data)
    # Output the filtered ioctl names and handler names
    output_data(filtered_ioctl_data, "_filtered")


# Process jsonl files of type definitions such as structs, enums and functions, and classify and output them as JSON files.
# Read files such as struct.jsonl, enum.jsonl and func.jsonl.
def process_func(file_name: str, nums):
    # Initialize an empty dict to store type data
    # The following dict {type_name: {filename: source}}}
    type_data = {}
    for num in range(1,nums):
        # Join the file path
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
            # Get the absolute path of the source file
            source_file_name = source_file_path.absolute().resolve().as_posix()
            source = data["source"]
            if type_name not in type_data:
                type_data[type_name] = {}
            # Store the source file path and its source code into the type data
            type_data[type_name][source_file_name] = source

    # Write the processed data to a JSON file
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



# Process typedef information (such as struct aliases and enum aliases), and try to merge the source of the alias and the original type.
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
        # Convert the source file path to an absolute path and resolve symlinks
        source_file_name = source_file_path.absolute().resolve().as_posix()
        source = data["source"]
        if type_name not in typedef_data:
            # If the type name is not in the typedef_data dict, add a key for that type name
            typedef_data[type_name] = {}

        if alias_name and alias_name in type_data:
            # If an alias exists and that alias is present in the type_data dict
            alias_data = type_data[alias_name]
            # Find the path most similar to the source file path
            most_similar = max(
                alias_data, key=lambda x: path_similarity(source_file_name, x)
            )
            source_most_similar = alias_data[most_similar]
            # Merge the most similar source code with the current source code
            source = source + "\n" + source_most_similar

        # Add the processed source code to the typedef_data dict
        typedef_data[type_name][source_file_name] = source

    # Write the processed data to a json file
    # Write the data to a json file
    json_path = Path(f"processed_{file_path.stem}.json")
    json_path.write_text(json.dumps(typedef_data, indent=2))

# Process the usage.jsonl file and classify fops (file operations) usage information.
def process_usage():
    file_path = linux_path / "usage.jsonl"

    # Initialize the usage_data dict, structured as {fops_name: {filename: [source]}}
    usage_data = {}
    for line in file_path.read_text().splitlines():
        line = line.strip()
        data = json.loads(line)
        fops_name = data["alias"]
        source_file_name = data["filename"]
        source_file_path = linux_path / source_file_name
        # Get the absolute path of the file and convert it to POSIX format
        source_file_name = source_file_path.absolute().resolve().as_posix()
        source = data["source"]
        if fops_name not in usage_data:
            # If fops_name is not in usage_data, initialize an empty dict
            usage_data[fops_name] = {}
        # Add the source to the corresponding filename
        usage_data[fops_name][source_file_name] = source

    # Write the data to a JSON file
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

    # If the --usage argument is given, only process usage information; otherwise process everything.
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

