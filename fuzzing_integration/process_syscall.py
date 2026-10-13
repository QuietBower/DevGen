#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import glob

def main():
    # 设置路径
    source_dir = "/home/user/Fuzzer/out/kernelgpt_syscalls"
    target_dir = "/home/user/Fuzzer/third_tool/syzkaller_defconfig_dev/sys/linux"
    syzkaller_dir = "/home/user/Fuzzer/third_tool/syzkaller_defconfig_dev"
    kernel_source = "/home/user/Fuzzer/linux-6.18"
    error_dir = "/home/user/Fuzzer/out/kernelgpt_err"
    
    # 确保目标目录存在
    os.makedirs(target_dir, exist_ok=True)
    os.makedirs(error_dir, exist_ok=True)
    
    # 获取所有txt文件并按文件名排序
    txt_files = sorted(glob.glob(os.path.join(source_dir, "*.txt")))
    if not txt_files:
        print("No TXT files found in source directory.")
        return
    
    print(f"Found {len(txt_files)} TXT files to process.")
    
    # 计数器
    processed_count = 0
    
    try:
        # 遍历所有txt文件（按顺序）
        for i, filepath in enumerate(txt_files):
            filename = os.path.basename(filepath)
            dest_path = os.path.join(target_dir, filename)
            
            # 1. 检查目标目录是否已存在对应的.const文件
            const_file = os.path.join(target_dir, f"{filename}.const")
            if os.path.exists(const_file):
                print(f"[{i+1}/{len(txt_files)}] Skipping {filename} (const file exists)")
                os.remove(filepath)  # 删除源文件
                continue
            
            print(f"\n[{i+1}/{len(txt_files)}] Processing {filename}")
            
            # 2. 拷贝文件
            shutil.copy2(filepath, dest_path)
            print(f"Copied {filename} to {target_dir}")
            
            # 3. 在syzkaller目录执行命令
            os.chdir(syzkaller_dir)
            
            # 执行syz-extract
            extract_cmd = [
                "bin/syz-extract",
                "-os", "linux",
                "-sourcedir", kernel_source,
                "-arch", "amd64",
                filename
            ]
            
            print("Executing:", " ".join(extract_cmd))
            result = subprocess.run(extract_cmd, capture_output=True, text=True)
            
            # 检查命令执行结果
            if result.returncode != 0:
                print(f"Error executing syz-extract for {filename} (exit code {result.returncode})")
                print("STDERR:", result.stderr)
                print("STDOUT:", result.stdout)
                
                # 处理错误：移动目标文件到错误目录并删除源文件
                error_file = os.path.join(error_dir, filename)
                if os.path.exists(dest_path):
                    shutil.move(dest_path, error_file)
                    print(f"Moved failed file to error directory: {error_file}")
                
                os.remove(filepath)  # 删除源文件
                print(f"Deleted source file: {filepath}")
                
                continue  # 跳过后续处理，继续下一个文件
            
            # 执行syz-sysgen
            sysgen_cmd = ["bin/syz-sysgen"]
            print("Executing:", " ".join(sysgen_cmd))
            result = subprocess.run(sysgen_cmd, capture_output=True, text=True)
            
            # 检查命令执行结果
            if result.returncode != 0:
                print(f"Error executing syz-sysgen after {filename} (exit code {result.returncode})")
                print("STDERR:", result.stderr)
                print("STDOUT:", result.stdout)
                # 注意：这里我们不移动文件到错误目录，因为syz-sysgen是全局操作
                # 但我们会删除源文件
                os.remove(filepath)  # 删除源文件
                print(f"Deleted source file: {filepath}")
                continue  # 继续下一个文件
            
            # 4. 处理完成后删除源文件
            os.remove(filepath)
            print(f"Deleted source file: {filepath}")
            
            processed_count += 1
            
            # 5. 每处理30个文件执行一次构建
            if processed_count % 100 == 0:
                print(f"\nProcessed {processed_count} files. Running build commands...")
                
                # 执行 make clean
                print("Executing: make clean")
                result = subprocess.run(["make", "clean"], capture_output=True, text=True)
                if result.returncode != 0:
                    print(f"Error executing make clean (exit code {result.returncode})")
                    print("STDERR:", result.stderr)
                    raise RuntimeError("Build failed during make clean")
                
                # 执行 make
                print("Executing: make")
                result = subprocess.run(["make"], capture_output=True, text=True)
                if result.returncode != 0:
                    print(f"Error executing make (exit code {result.returncode})")
                    print("STDERR:", result.stderr)
                    raise RuntimeError("Build failed during make")
                
                # 执行 make bin/syz-extract
                print("Executing: make bin/syz-extract")
                result = subprocess.run(["make", "bin/syz-extract"], capture_output=True, text=True)
                if result.returncode != 0:
                    print(f"Error executing make bin/syz-extract (exit code {result.returncode})")
                    print("STDERR:", result.stderr)
                    raise RuntimeError("Build failed during make bin/syz-extract")
                
                print("Build commands completed successfully.")
        
        # 处理完成后执行最后一次构建（如果文件总数不是30的倍数）
        if processed_count % 300 != 0:
            remaining = processed_count % 30
            print(f"\nProcessed {remaining} file(s) since last build. Running final build commands...")
            
            # 执行 make clean
            print("Executing: make clean")
            result = subprocess.run(["make", "clean"], capture_output=True, text=True)
            if result.returncode != 0:
                print(f"Error executing make clean (exit code {result.returncode})")
                print("STDERR:", result.stderr)
                raise RuntimeError("Build failed during make clean")
            
            # 执行 make
            print("Executing: make")
            result = subprocess.run(["make"], capture_output=True, text=True)
            if result.returncode != 0:
                print(f"Error executing make (exit code {result.returncode})")
                print("STDERR:", result.stderr)
                raise RuntimeError("Build failed during make")
            
            # 执行 make bin/syz-extract
            print("Executing: make bin/syz-extract")
            result = subprocess.run(["make", "bin/syz-extract"], capture_output=True, text=True)
            if result.returncode != 0:
                print(f"Error executing make bin/syz-extract (exit code {result.returncode})")
                print("STDERR:", result.stderr)
                raise RuntimeError("Build failed during make bin/syz-extract")
            
            print("Final build commands completed successfully.")
        
        print("\nAll files processed successfully!")
    
    except Exception as e:
        print(f"\nERROR: Processing stopped due to an error!")
        print(f"Failed at file: {filename}")
        print(f"Error details: {str(e)}")
        sys.exit(1)

if __name__ == "__main__":
    main()
