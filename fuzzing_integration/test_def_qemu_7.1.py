import json
import subprocess
import time
import os
import difflib  # New: used for text diff comparison

# ================= Configuration section =================
JSON_FILE = "test_qemu.json"
SSH_KEY = "/home/user/Fuzzer/third_tool/Image-7.0/bullseye.id_rsa"
SSH_PORT = "10021"
SSH_USER_HOST = "root@127.0.0.1"

# Newly added configuration path
BASE_DEV_TXT = "/home/user/Fuzzer/code/python/basedev.txt"
CHANGE_DEV_JSON = "/home/user/Fuzzer/code/python/changedev.json"

# List of base QEMU commands
QEMU_CMD_BASE = [
    "/home/user/Fuzzer/bin/qemu/bin/qemu-system-x86_64",
    "-m", "2G",
    "-smp", "2",
    "-machine", "q35",
    "-kernel", "/home/user/Fuzzer/build/linux/linux-7.1-defconfig/arch/x86/boot/bzImage",
    "-append", "console=ttyS0 root=/dev/vda earlyprintk=serial net.ifnames=0 panic_on_warn=0 pci=realloc selinux=0 enforcing=0",
    "-drive", "file=/home/user/Fuzzer/third_tool/Image-7.0/bullseye.img,format=raw,if=virtio",
    "-device", "pcie-root-port,id=rp1",
    # "-device", "xxx,bus=rp1",  <-- inserted here dynamically in the loop
    "-net", f"user,host=10.0.2.10,hostfwd=tcp:127.0.0.1:{SSH_PORT}-:22",
    "-net", "nic,model=e1000",
    "-enable-kvm",
    "-nographic",
    "-serial", "mon:stdio",
    "-fsdev", "local,id=fsdev0,path=/home/user/Fuzzer/out/share,security_model=none",
    "-device", "virtio-9p-pci,id=fs0,fsdev=fsdev0,mount_tag=hostshare",
    "-pidfile", "vm.pid",
    "-d", "guest_errors,unimp",
    "-snapshot",
    "-s"
]
# ==========================================

def run_tests():
    # 1. Load the JSON file
    if not os.path.exists(JSON_FILE):
        print(f"[-] 错误: 找不到 JSON 文件 {JSON_FILE}")
        return

    with open(JSON_FILE, 'r') as f:
        devices_data = json.load(f)

    print(f"[*] 成功加载 {len(devices_data)} 个待测试的设备。")

    # ========== New part: load baseline device data and historical diff data ==========
    base_tree_lines = []
    if os.path.exists(BASE_DEV_TXT):
        with open(BASE_DEV_TXT, "r") as bf:
            base_tree_lines = bf.read().splitlines()
        print(f"[*] 成功加载基于 {BASE_DEV_TXT} 的设备基线数据。")
    else:
        print(f"[!] 警告: 找不到基线文件 {BASE_DEV_TXT}，将把所有输出视为新增差异。")

    changedev_data = {}
    if os.path.exists(CHANGE_DEV_JSON):
        try:
            with open(CHANGE_DEV_JSON, "r") as cf:
                changedev_data = json.load(cf)
        except Exception:
            print(f"[!] 警告: {CHANGE_DEV_JSON} 格式错误，将重新初始化。")
    # ==============================================================

    # 2. Iterate over all devices
    for device_name in devices_data.keys():
        print(f"\n=============================================")
        print(f"[*] 正在测试设备: {device_name}")
        
        # Build the QEMU startup command for the current device
        cmd = QEMU_CMD_BASE.copy()
        
        # Dynamically insert -device xxx,bus=rp1 (positioned right after pcie-root-port)
        rp1_index = cmd.index("pcie-root-port,id=rp1") + 1
        cmd.insert(rp1_index, "-device")
        cmd.insert(rp1_index + 1, f"{device_name},bus=rp1")

        # Start QEMU and redirect stdout and stderr to this device's dedicated log
        log_path = f"/home/user/Fuzzer/code/python/log/vm_{device_name}.log"
        # Ensure the log directory exists
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        print(f"[*] 启动 QEMU，日志将写入: {log_path}")
        
        with open(log_path, "w") as log_file:
            qemu_proc = subprocess.Popen(cmd, stdout=log_file, stderr=subprocess.STDOUT)

        # 3. Wait for the virtual machine to boot
        print("[*] 等待 90 秒让虚拟机完全启动...")
        time.sleep(90)

        # 4. Run the lspci command over SSH
        ssh_cmd = [
            "ssh",
            "-i", SSH_KEY,
            "-p", SSH_PORT,
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            "-o", "BatchMode=yes",  
            SSH_USER_HOST,
            "lspci -k -s 01:00.0"
        ]

        result_flag = "n"
        try:
            print("[*] 正在执行 SSH 检查驱动状态 (定位地址 01:00.0)...")
            ssh_result = subprocess.run(ssh_cmd, capture_output=True, text=True, timeout=15)
            output = ssh_result.stdout

            print("[*] lspci 输出结果:")
            print(output.strip())

            if "Kernel driver in use" in output:
                result_flag = "y"
                print(f"[+] 发现 'Kernel driver in use'，设备 {device_name} 建模且驱动挂载成功！")
            else:
                print(f"[-] 未发现 'Kernel driver in use'，驱动挂载失败。")

        except subprocess.TimeoutExpired:
            print(f"[!] SSH 连接超时，虚拟机可能尚未启动完成或发生内核崩溃 (Panic)。")
        except Exception as e:
            print(f"[!] SSH 执行发生未知错误: {e}")

        # ========== Extended feature: if the driver binds successfully, run tree /dev and compare ==========
        if result_flag == "y":
            print(f"[*] 驱动挂载成功，正在执行 tree /dev 并保存及对比输出...")
            tree_ssh_cmd = [
                "ssh",
                "-i", SSH_KEY,
                "-p", SSH_PORT,
                "-o", "StrictHostKeyChecking=no",
                "-o", "UserKnownHostsFile=/dev/null",
                "-o", "BatchMode=yes",
                SSH_USER_HOST,
                "tree /dev"
            ]
            try:
                tree_result = subprocess.run(tree_ssh_cmd, capture_output=True, text=True, timeout=15)
                tree_output = tree_result.stdout
                if tree_result.returncode != 0:
                    print(f"[!] tree 命令返回非零状态码: {tree_result.returncode}")
                    if tree_result.stderr:
                        tree_output = tree_result.stderr

                # Save the original tree result
                output_dir = "/home/user/Fuzzer/code/python/dev_txt"
                os.makedirs(output_dir, exist_ok=True)
                output_file = os.path.join(output_dir, f"{device_name}.txt")

                with open(output_file, "w") as f:
                    f.write(tree_output)
                print(f"[+] tree /dev 完整输出已保存至: {output_file}")

                # === Core comparison logic ===
                current_tree_lines = tree_output.splitlines()
                
                # Use difflib to compute the diff (returns a generator)
                diff_generator = difflib.ndiff(base_tree_lines, current_tree_lines)
                
                # Filter the diff, keeping only lines starting with "+ " (added) or "- " (removed)
                changes = [line for line in diff_generator if line.startswith('+ ') or line.startswith('- ')]
                
                # Save the diff result into the dict
                changedev_data[device_name] = changes
                
                # Flush-write changedev.json in real time
                with open(CHANGE_DEV_JSON, "w", encoding="utf-8") as cf:
                    json.dump(changedev_data, cf, indent=4, ensure_ascii=False)
                    
                print(f"[+] 差异对比完成，共发现 {len(changes)} 处变动，已更新至: {CHANGE_DEV_JSON}")

            except subprocess.TimeoutExpired:
                print("[!] tree 命令执行超时")
            except Exception as e:
                print(f"[!] 执行 tree 命令时发生未知错误: {e}")
        # ===========================================================

        # 5. Update the JSON and save it in real time
        devices_data[device_name]["result"] = result_flag
        with open(JSON_FILE, 'w') as f:
            json.dump(devices_data, f, indent=4)

        # 6. Kill the QEMU process, prepare for the next test
        print("[*] 正在关闭 QEMU 虚拟机...")
        qemu_proc.terminate()
        try:
            qemu_proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            print("[!] 虚拟机未正常响应，强制杀死进程 (kill -9)...")
            qemu_proc.kill()
        
        time.sleep(5)

    print("\n[*] ================= 测试任务全部完成 =================")

if __name__ == "__main__":
    run_tests()
