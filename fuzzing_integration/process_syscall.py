#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import glob

def main():
    # Set up the paths
    source_dir = "/home/user/Fuzzer/out/kernelgpt_syscalls"
    target_dir = "/home/user/Fuzzer/third_tool/syzkaller_defconfig_dev/sys/linux"
    syzkaller_dir = "/home/user/Fuzzer/third_tool/syzkaller_defconfig_dev"
    kernel_source = "/home/user/Fuzzer/linux-6.18"
    error_dir = "/home/user/Fuzzer/out/kernelgpt_err"
    
    # Ensure the target directory exists
    os.makedirs(target_dir, exist_ok=True)
    os.makedirs(error_dir, exist_ok=True)
    
    # Get all txt files and sort them by filename
    txt_files = sorted(glob.glob(os.path.join(source_dir, "*.txt")))
    if not txt_files:
        print("No TXT files found in source directory.")
        return
    
    print(f"Found {len(txt_files)} TXT files to process.")
    
    # Counter
    processed_count = 0
    
    try:
        # Iterate over all txt files (in order)
        for i, filepath in enumerate(txt_files):
            filename = os.path.basename(filepath)
            dest_path = os.path.join(target_dir, filename)
            
            # 1. Check whether the corresponding .const file already exists in the target directory
            const_file = os.path.join(target_dir, f"{filename}.const")
            if os.path.exists(const_file):
                print(f"[{i+1}/{len(txt_files)}] Skipping {filename} (const file exists)")
                os.remove(filepath)  # Delete the source file
                continue
            
            print(f"\n[{i+1}/{len(txt_files)}] Processing {filename}")
            
            # 2. Copy the file
            shutil.copy2(filepath, dest_path)
            print(f"Copied {filename} to {target_dir}")
            
            # 3. Run commands in the syzkaller directory
            os.chdir(syzkaller_dir)
            
            # Run syz-extract
            extract_cmd = [
                "bin/syz-extract",
                "-os", "linux",
                "-sourcedir", kernel_source,
                "-arch", "amd64",
                filename
            ]
            
            print("Executing:", " ".join(extract_cmd))
            result = subprocess.run(extract_cmd, capture_output=True, text=True)
            
            # Check the command execution result
            if result.returncode != 0:
                print(f"Error executing syz-extract for {filename} (exit code {result.returncode})")
                print("STDERR:", result.stderr)
                print("STDOUT:", result.stdout)
                
                # Handle the error: move the target file to the error directory and delete the source file
                error_file = os.path.join(error_dir, filename)
                if os.path.exists(dest_path):
                    shutil.move(dest_path, error_file)
                    print(f"Moved failed file to error directory: {error_file}")
                
                os.remove(filepath)  # Delete the source file
                print(f"Deleted source file: {filepath}")
                
                continue  # Skip the remaining processing and continue with the next file
            
            # Run syz-sysgen
            sysgen_cmd = ["bin/syz-sysgen"]
            print("Executing:", " ".join(sysgen_cmd))
            result = subprocess.run(sysgen_cmd, capture_output=True, text=True)
            
            # Check the command execution result
            if result.returncode != 0:
                print(f"Error executing syz-sysgen after {filename} (exit code {result.returncode})")
                print("STDERR:", result.stderr)
                print("STDOUT:", result.stdout)
                # Note: here we do not move files to the error directory, because syz-sysgen is a global operation
                # but we do delete the source file
                os.remove(filepath)  # Delete the source file
                print(f"Deleted source file: {filepath}")
                continue  # Continue with the next file
            
            # 4. Delete the source file after processing is done
            os.remove(filepath)
            print(f"Deleted source file: {filepath}")
            
            processed_count += 1
            
            # 5. Run a build after every 30 files processed
            if processed_count % 100 == 0:
                print(f"\nProcessed {processed_count} files. Running build commands...")
                
                # Run make clean
                print("Executing: make clean")
                result = subprocess.run(["make", "clean"], capture_output=True, text=True)
                if result.returncode != 0:
                    print(f"Error executing make clean (exit code {result.returncode})")
                    print("STDERR:", result.stderr)
                    raise RuntimeError("Build failed during make clean")
                
                # Run make
                print("Executing: make")
                result = subprocess.run(["make"], capture_output=True, text=True)
                if result.returncode != 0:
                    print(f"Error executing make (exit code {result.returncode})")
                    print("STDERR:", result.stderr)
                    raise RuntimeError("Build failed during make")
                
                # Run make bin/syz-extract
                print("Executing: make bin/syz-extract")
                result = subprocess.run(["make", "bin/syz-extract"], capture_output=True, text=True)
                if result.returncode != 0:
                    print(f"Error executing make bin/syz-extract (exit code {result.returncode})")
                    print("STDERR:", result.stderr)
                    raise RuntimeError("Build failed during make bin/syz-extract")
                
                print("Build commands completed successfully.")
        
        # Run a final build after processing is done (if the total number of files is not a multiple of 30)
        if processed_count % 300 != 0:
            remaining = processed_count % 30
            print(f"\nProcessed {remaining} file(s) since last build. Running final build commands...")
            
            # Run make clean
            print("Executing: make clean")
            result = subprocess.run(["make", "clean"], capture_output=True, text=True)
            if result.returncode != 0:
                print(f"Error executing make clean (exit code {result.returncode})")
                print("STDERR:", result.stderr)
                raise RuntimeError("Build failed during make clean")
            
            # Run make
            print("Executing: make")
            result = subprocess.run(["make"], capture_output=True, text=True)
            if result.returncode != 0:
                print(f"Error executing make (exit code {result.returncode})")
                print("STDERR:", result.stderr)
                raise RuntimeError("Build failed during make")
            
            # Run make bin/syz-extract
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
