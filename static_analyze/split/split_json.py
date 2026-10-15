"""PCI驱动依赖分析主模块"""

import os
import json

# Import custom modules
from config import (
    FILE_MAPPINGS, OUTPUT_DIR, MAX_ITERATIONS
)
from file_utils import (
    get_file_path_without_line_number,
    get_base_directory_by_includes
)
from symbol_utils import (
    sanitize_driver_name,
    extract_final_driver_name,
    collect_same_file_symbols,
    find_symbol,
    verify_and_correct_symbol_type,
)
from dependency_analysis import (
    extract_dependencies,
    find_call_chain_ends
)
from output_utils import (
    add_symbol_to_driver_data,
    remove_duplicates,
    generate_declarations,
    create_driver_json_structure
)


def create_pci_driver_analysis():
    """主函数：创建PCI驱动分析"""
    # 1. Create the folder new_my_pci_driver (skip if it already exists)
    output_dir = OUTPUT_DIR
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"创建输出目录: {output_dir}")
    else:
        print(f"输出目录已存在: {output_dir}")
    
    # 2. Read all jsonl files
    symbols = {
        'define': {},
        'enum': {},
        'enum_typedef': {},
        'func': {},
        'struct': {},
        'struct_typedef': {},
        'struct_init': {},
        'var': {}
    }
    
    for symbol_type, file_path in FILE_MAPPINGS.items():
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                for line in f:
                    data = json.loads(line.strip())
                    
                    # Handle the different symbol formats
                    if symbol_type in ['enum_typedef', 'struct_typedef']:
                        # typedef format: use alias as the key, or name if alias is absent
                        key = data.get('alias', data.get('name', ''))
                    else:
                        # Non-typedef format: use name as the key
                        key = data.get('name', '')
                    
                    if key:
                        # Handle the multiple-definition case
                        if key in symbols[symbol_type]:
                            if not isinstance(symbols[symbol_type][key], list):
                                symbols[symbol_type][key] = [symbols[symbol_type][key]]
                            symbols[symbol_type][key].append(data)
                        else:
                            symbols[symbol_type][key] = data
                            
            print(f"已加载 {len(symbols[symbol_type])} 个 {symbol_type}")
        except FileNotFoundError:
            print(f"警告: 找不到文件 {file_path}")
        except Exception as e:
            print(f"读取文件 {file_path} 时出错: {e}")
    
    # 3. Get all pci_driver struct initialization variable definitions from struct-init.jsonl
    pci_drivers = []
    for name, data in symbols['struct_init'].items():
        if isinstance(data, list):
            for item in data:
                if "pci_driver" in item["source"]:
                    pci_drivers.append(item)
        else:
            if "pci_driver" in data["source"]:
                pci_drivers.append(data)
    
    print(f"找到 {len(pci_drivers)} 个PCI驱动初始化变量")
    
    # Add the ability to skip the first x drivers
    skip_count = 0  # Set the number of drivers to skip, e.g. 5 skips the first 5 drivers
    if skip_count > 0:
        print(f"跳过前 {skip_count} 个PCI驱动")
        pci_drivers = pci_drivers[skip_count:]
        print(f"剩余 {len(pci_drivers)} 个PCI驱动待分析")
    
    # Used to track filenames already used
    used_filenames = set()
    
    # Process each PCI driver
    for driver_index, driver_data in enumerate(pci_drivers):
        driver_path = driver_data["filename"]
        driver_name = driver_data["name"]
        
        # Show the index of the driver currently being processed (actual index after skipping)
        print(f"\n处理第 {skip_count + driver_index + 1} 个驱动 (总共 {len(pci_drivers) + skip_count} 个)")
        
        # 7. Extract the driver name: the final reference of the .name field
        final_driver_name = extract_final_driver_name(driver_data["source"], symbols, driver_path)
        if not final_driver_name:
            final_driver_name = driver_name
            print(f"警告: 无法提取驱动名称，使用变量名: {driver_name}")
        
        # Replace "/" characters in the driver name with "+++"
        final_driver_name = sanitize_driver_name(final_driver_name)
        print(f"处理驱动: {final_driver_name} (变量名: {driver_name})")
        
        # Determine the base directory and headers - using the new #include-based method
        base_dir, header_files = get_base_directory_by_includes(driver_path)
        print(f"基础目录: {base_dir}")
        print(f"找到 {len(header_files)} 个头文件")
        
        # Create the JSON data structure
        driver_json = create_driver_json_structure(final_driver_name, driver_path, base_dir, header_files)
        
        # Used to record symbols already added
        added_symbols = set()
        unknown_symbols = set()
        outside_base_dir_symbols = set()
        
        # 4. Get all symbols in the file where this variable is located
        same_file_symbols = collect_same_file_symbols(driver_path, symbols)
        print(f"找到 {len(same_file_symbols)} 个同文件符号")
        
        # 5. Find the upward call endpoint of this pci_driver
        call_chain_ends = find_call_chain_ends(driver_name, symbols, base_dir)
        print(f"找到 {len(call_chain_ends)} 个调用链终点")
        
        # Build the dependency-analysis queue
        queue = []
        
        # Enqueue symbols from the same file
        for symbol_name in same_file_symbols:
            if symbol_name not in added_symbols and symbol_name not in queue:
                queue.append(symbol_name)
        
        # Enqueue the PCI driver itself
        if driver_name not in added_symbols and driver_name not in queue:
            queue.append(driver_name)
        
        # Enqueue the endpoint of the upward call chain
        for end_function in call_chain_ends:
            if end_function not in added_symbols and end_function not in queue:
                queue.append(end_function)
        
        print(f"开始依赖分析，队列大小: {len(queue)}")
        
        # 6. Recursive dependency analysis
        processed_count = 0
        
        while queue and processed_count < MAX_ITERATIONS:
            symbol_name = queue.pop(0)
            processed_count += 1
            
            if symbol_name in added_symbols:
                continue
                
            # Look up the symbol definition
            symbol_data = find_symbol(symbol_name, symbols, base_dir, driver_path)
            
            if symbol_data:
                # Check whether the symbol is in the base directory
                symbol_path_no_line = get_file_path_without_line_number(symbol_data["filename"])
                if symbol_path_no_line.startswith(base_dir):
                    # Validate the type and relocate before adding the symbol
                    verified_symbol_data = verify_and_correct_symbol_type(symbol_name, symbol_data, symbols)
                    if verified_symbol_data:
                        added_symbols.add(symbol_name)
                        add_symbol_to_driver_data(symbol_name, verified_symbol_data, driver_json, symbols)
                        
                        # Extract dependency symbols
                        new_deps = extract_dependencies(verified_symbol_data["source"])
                        for new_dep in new_deps:
                            if (new_dep not in added_symbols and 
                                new_dep not in queue and
                                new_dep not in unknown_symbols and
                                new_dep not in outside_base_dir_symbols):
                                queue.append(new_dep)
                    else:
                        print(f"警告: 无法验证符号类型: {symbol_name}")
                    
                    if processed_count % 100 == 0:
                        print(f"已处理 {processed_count} 个符号，队列剩余: {len(queue)}，已添加: {len(added_symbols)}")
                else:
                    # The symbol has a definition but is not in the base directory
                    outside_base_dir_symbols.add(symbol_name)
            else:
                # Symbol definition not found
                unknown_symbols.add(symbol_name)
        
        if processed_count >= MAX_ITERATIONS:
            print(f"警告: 达到最大迭代次数 {MAX_ITERATIONS}")
        
        # 8. Deduplication
        driver_json = remove_duplicates(driver_json)
        
        # Add unknown symbols
        driver_json["UNKNOWN"] = list(unknown_symbols)
        
        # Add symbols that do not belong to the base path
        driver_json["不属于基础路径的字符"] = list(outside_base_dir_symbols)
        
        # 9. Create declarations for struct-init and func definitions
        generate_declarations(driver_json)
        
        # 10. Create the JSON file - handle drivers with the same name
        base_filename = f"{final_driver_name}.json"
        filename = base_filename
        counter = 1
        
        # Check whether the filename already exists; if so, append a numeric suffix
        while filename in used_filenames:
            filename = f"{final_driver_name}{counter}.json"
            counter += 1
        
        # Add to the set of used filenames
        used_filenames.add(filename)
        
        filepath = os.path.join(output_dir, filename)
        
        # If the file already exists, delete it
        if os.path.exists(filepath):
            os.remove(filepath)
        
        with open(filepath, "w", encoding="utf-8") as f:
            json.dump(driver_json, f, ensure_ascii=False, indent=2)
        
        print(f"已创建文件: {filename}")
        print(f"统计: {len(added_symbols)} 个符号, {len(unknown_symbols)} 个未知符号, {len(outside_base_dir_symbols)} 个不属于基础路径的符号")
        print("-" * 50)


if __name__ == "__main__":
    create_pci_driver_analysis()
    print("PCI驱动依赖分析完成！")
