import re

from config import (
    FUNCTION_PATTERN, STRUCT_PATTERN, TYPEDEF_PATTERN, 
    MACRO_PATTERN, ARRAY_PATTERN, ARRAY_SIZE_PATTERN, 
    FUNC_PTR_PATTERN, C_KEYWORDS
)
from file_utils import is_symbol_in_base_dir
from symbol_utils import remove_comments, check_symbol_reference

def extract_dependencies(source_code):
    """从源代码中提取所有依赖的符号（忽略注释）- 使用简单可靠的模式"""
    # Remove comments
    clean_source = remove_comments(source_code)
    dependencies = set()
    
    # Various match patterns - using precompiled regular expressions
    patterns = [
        (FUNCTION_PATTERN, True),
        (STRUCT_PATTERN, False),
        (TYPEDEF_PATTERN, True),
        (MACRO_PATTERN, True),
        (ARRAY_PATTERN, True),
        (ARRAY_SIZE_PATTERN, True),
    ]
    
    for pattern, single_match in patterns:
        matches = pattern.findall(clean_source)
        for match in matches:
            symbol = match if single_match else match[1]
            
            if (symbol not in C_KEYWORDS and 
                symbol != "NULL" and 
                len(symbol) > 1 and
                not symbol.startswith('_') and
                not re.match(r'^[0-9]', symbol)):
                dependencies.add(symbol)
    
    # Specifically extract function pointer assignments
    func_ptr_deps = extract_function_pointer_dependencies(source_code)
    dependencies.update(func_ptr_deps)
    
    return dependencies

def extract_function_pointer_dependencies(source_code):
    """专门提取函数指针赋值中的依赖"""
    clean_source = remove_comments(source_code)
    dependencies = set()
    
    # Use precompiled regular expressions
    matches = FUNC_PTR_PATTERN.findall(clean_source)
    for symbol in matches:
        if (symbol not in C_KEYWORDS and 
            symbol != "NULL" and 
            len(symbol) > 1):
            dependencies.add(symbol)
    
    return dependencies

def find_call_chain_ends(driver_name, symbols, base_dir):
    """找到向上调用链的终点"""
    call_chain_ends = set()
    
    # Search all symbol tables for symbols that use this PCI driver
    for symbol_type in ['func', 'var', 'struct_init']:
        for symbol_name, symbol_data in symbols[symbol_type].items():
            # Handle the single-definition and multiple-definition cases
            if isinstance(symbol_data, list):
                for data in symbol_data:
                    if is_symbol_in_base_dir(data["filename"], base_dir):
                        # Check whether it contains a reference to this PCI driver
                        if check_symbol_reference(data["source"], driver_name):
                            call_chain_ends.add(symbol_name)
                            break
            else:
                if is_symbol_in_base_dir(symbol_data["filename"], base_dir):
                    # Check whether it contains a reference to this PCI driver
                    if check_symbol_reference(symbol_data["source"], driver_name):
                        call_chain_ends.add(symbol_name)
    
    # Recursively find higher-level callers
    visited = set()
    queue = list(call_chain_ends)
    
    while queue:
        current_symbol = queue.pop(0)
        if current_symbol in visited:
            continue
        visited.add(current_symbol)
        
        # Find symbols that reference the current symbol
        for symbol_type in ['func', 'var', 'struct_init']:
            for symbol_name, symbol_data in symbols[symbol_type].items():
                if symbol_name in visited:
                    continue
                    
                # Handle the single-definition and multiple-definition cases
                definitions = []
                if isinstance(symbol_data, list):
                    definitions = symbol_data
                else:
                    definitions = [symbol_data]
                
                for data in definitions:
                    if is_symbol_in_base_dir(data["filename"], base_dir):
                        # Check whether it contains a reference to the current symbol
                        if check_symbol_reference(data["source"], current_symbol):
                            call_chain_ends.add(symbol_name)
                            queue.append(symbol_name)
                            break
    
    return list(call_chain_ends)
