import os
from config import INCLUDE_PATTERN

def get_file_path_without_line_number(file_path):
    """从文件路径中移除行号部分"""
    if not file_path:
        return file_path
    
    colon_pos = file_path.rfind(':')
    if colon_pos != -1:
        line_part = file_path[colon_pos + 1:]
        if line_part.isdigit():
            return file_path[:colon_pos]
    return file_path

def resolve_include_path(include_path, current_file):
    """解析#include路径为绝对路径"""
    current_dir = os.path.dirname(current_file)
    
    # Handle relative paths
    if include_path.startswith('./'):
        resolved = os.path.join(current_dir, include_path[2:])
    elif include_path.startswith('../'):
        resolved = os.path.join(current_dir, include_path)
    else:
        # Look up directly in the current directory
        resolved = os.path.join(current_dir, include_path)
    
    # Return the normalized absolute path
    return os.path.abspath(resolved)

def find_common_parent_directory(directories):
    """找到所有目录的最大公共路径"""
    if not directories:
        return ""
    
    # Split the path into components
    path_components = [d.split(os.sep) for d in directories]
    
    # Find the length of the shortest path
    min_length = min(len(components) for components in path_components)
    
    # Compare the components at each position
    common_components = []
    for i in range(min_length):
        current_component = path_components[0][i]
        if all(components[i] == current_component for components in path_components):
            common_components.append(current_component)
        else:
            break
    
    # Rebuild the path
    if not common_components:
        return ""
    
    return os.sep.join(common_components)

def get_base_directory_by_includes(file_path):
    """根据文件中的#include ""指令确定驱动核心路径，并收集所有头文件"""
    file_path_no_line = get_file_path_without_line_number(file_path)
    
    # Collect the directories of all related files
    all_dirs = set()
    # Collect the absolute paths of all header files
    header_files = []
    
    # Process the initial file
    all_dirs.add(os.path.dirname(file_path_no_line))
    
    # Recursively process #include "" files
    processed_files = set()
    queue = [file_path_no_line]
    
    while queue:
        current_file = queue.pop(0)
        if current_file in processed_files:
            continue
        processed_files.add(current_file)
        
        # Read the file content
        try:
            with open(current_file, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
        except Exception as e:
            print(f"警告: 无法读取文件 {current_file}: {e}")
            continue
        
        # Extract all #include "" directives
        includes = INCLUDE_PATTERN.findall(content)
        
        for include_path in includes:
            # Resolve the include path
            resolved_path = resolve_include_path(include_path, current_file)
            if resolved_path and os.path.exists(resolved_path):
                # Add the directory containing the file
                all_dirs.add(os.path.dirname(resolved_path))
                # Add the header file to the list
                header_files.append(resolved_path)
                # Enqueue the file for further processing
                queue.append(resolved_path)
    
    # Compute the longest common path of all directories
    if not all_dirs:
        base_dir = os.path.dirname(file_path_no_line)
    else:
        base_dir = find_common_parent_directory(list(all_dirs))
    
    return base_dir, header_files

def get_base_directory_by_includes_simple(file_path):
    """简化版的基础路径确定函数，用于驱动名称提取阶段"""
    file_path_no_line = get_file_path_without_line_number(file_path)
    return os.path.dirname(file_path_no_line)

def is_symbol_in_base_dir(symbol_path, base_dir):
    """检查符号路径是否在基础目录中"""
    symbol_path_no_line = get_file_path_without_line_number(symbol_path)
    return symbol_path_no_line.startswith(base_dir)

def calculate_path_similarity(path1, path2):
    """计算两个路径的相似度（去除行号）"""
    path1 = get_file_path_without_line_number(path1)
    path2 = get_file_path_without_line_number(path2)
    
    # If path2 is None, return 0
    if not path2:
        return 0
    
    dirs1 = path1.split('/')
    dirs2 = path2.split('/')
    
    # Compute the length of the common prefix
    common_prefix = 0
    for i in range(min(len(dirs1), len(dirs2))):
        if dirs1[i] == dirs2[i]:
            common_prefix += 1
        else:
            break
    
    # Similarity = common prefix length / max path length
    max_length = max(len(dirs1), len(dirs2))
    similarity = common_prefix / max_length if max_length > 0 else 0
    
    return similarity

def select_by_path_similarity(definitions, base_dir):
    """通过路径相似度选择最佳定义"""
    best_definition = None
    best_similarity = -1
    
    for definition in definitions:
        definition_path = get_file_path_without_line_number(definition["filename"])
        similarity = calculate_path_similarity(definition_path, base_dir)
        
        if similarity > best_similarity:
            best_similarity = similarity
            best_definition = definition
    
    return best_definition
