import re
from symbol_utils import get_symbol_type

def add_symbol_to_driver_data(symbol_name, symbol_data, driver_data, symbols):
    """将符号添加到驱动数据中"""

    
    symbol_type = get_symbol_type(symbol_name, symbols)
    
    entry = {
        "name": symbol_name,
        "path": symbol_data["filename"],
        "source": symbol_data["source"]
    }
    
    if symbol_type == "define":
        driver_data["宏定义"].append(entry)
    elif symbol_type == "enum":
        driver_data["enum定义"].append(entry)
    elif symbol_type == "enum_typedef":
        driver_data["enum-typedef定义"].append(entry)
    elif symbol_type == "struct":
        driver_data["struct定义"].append(entry)
    elif symbol_type == "struct_typedef":
        driver_data["struct-typedef定义"].append(entry)
    elif symbol_type == "func":
        driver_data["func定义"].append(entry)
    elif symbol_type == "struct_init":
        driver_data["struct-init定义"].append(entry)
    elif symbol_type == "var":
        driver_data["var声明"].append(entry)

def remove_duplicates(driver_data):
    """8. 对前面得到的定义进行去重操作"""
    for category in [
        "宏定义", "enum定义", "enum-typedef定义", "struct定义", 
        "struct-typedef定义", "var声明", "struct-init声明", 
        "func声明", "struct-init定义", "func定义"
    ]:
        if category in driver_data:
            unique_symbols = {}
            for symbol in driver_data[category]:
                # Use the name as the unique identifier
                unique_symbols[symbol["name"]] = symbol
            # Convert back to a list
            driver_data[category] = list(unique_symbols.values())
    
    return driver_data

def generate_declarations(driver_data):
    """9. 为每一个struct-init定义和func定义都创建struct-init声明和func声明"""
    # Generate a declaration for the function definition
    for func_def in driver_data["func定义"]:
        func_declaration = extract_function_declaration(func_def["source"])
        if func_declaration:
            # Check whether a declaration with the same name already exists
            exists = False
            for decl in driver_data["func声明"]:
                if decl["name"] == func_def["name"]:
                    exists = True
                    break
            
            if not exists:
                driver_data["func声明"].append({
                    "name": func_def["name"],
                    "path": func_def["path"],
                    "source": func_declaration
                })
    
    # Generate a declaration for the struct initialization definition
    for struct_init_def in driver_data["struct-init定义"]:
        struct_init_declaration = extract_struct_init_declaration(struct_init_def["source"])
        if struct_init_declaration:
            # Check whether a declaration with the same name already exists
            exists = False
            for decl in driver_data["struct-init声明"]:
                if decl["name"] == struct_init_def["name"]:
                    exists = True
                    break
            
            if not exists:
                driver_data["struct-init声明"].append({
                    "name": struct_init_def["name"],
                    "path": struct_init_def["path"],
                    "source": struct_init_declaration
                })

def extract_function_declaration(function_source):
    """从函数定义中提取函数声明"""
    brace_count = 0
    in_quotes = False
    escape_next = False
    declaration_end = -1
    
    for i, char in enumerate(function_source):
        if escape_next:
            escape_next = False
            continue
            
        if char == '\\':
            escape_next = True
            continue
            
        if char == '"' and not escape_next:
            in_quotes = not in_quotes
            continue
            
        if not in_quotes:
            if char == '{':
                if brace_count == 0:
                    declaration_end = i
                brace_count += 1
            elif char == '}':
                brace_count -= 1
    
    if declaration_end != -1:
        declaration = function_source[:declaration_end].strip()
        declaration = re.sub(r',\s*$', '', declaration)
        return declaration + ';'
    
    return None

def extract_struct_init_declaration(struct_init_source):
    """从结构体初始化中提取声明"""
    equal_pos = struct_init_source.find('=')
    if equal_pos != -1:
        declaration = struct_init_source[:equal_pos].strip()
        declaration = re.sub(r',\s*$', '', declaration)
        return declaration + ';'
    
    return None

def create_driver_json_structure(final_driver_name, driver_path, base_dir, header_files):
    """创建驱动JSON数据结构"""
    return {
        "驱动名称": final_driver_name,
        "驱动核心路径": driver_path,
        "驱动基础路径": base_dir,
        "头文件": header_files,
        "宏定义": [],
        "enum定义": [],
        "enum-typedef定义": [],
        "struct定义": [],
        "struct-typedef定义": [],
        "var声明": [],
        "struct-init声明": [],
        "func声明": [],
        "struct-init定义": [],
        "func定义": [],
        "UNKNOWN": [],
        "不属于基础路径的字符": []
    }
