import json
import subprocess
import time
import os
import difflib  # 新增：用于进行文本差异比对

# ================= 配置区 =================
JSON_FILE = "test_qemu.json"
SSH_KEY = "/home/user/Fuzzer/third_tool/Image-7.0/bullseye.id_rsa"
SSH_PORT = "10021"
SSH_USER_HOST = "root@127.0.0.1"

# 新增配置路径
BASE_DEV_TXT = "/home/user/Fuzzer/code/python/basedev.txt"
CHANGE_DEV_JSON = "/home/user/Fuzzer/code/python/changedev.json"

# QEMU 基础命令列表
QEMU_CMD_BASE = [
    "/home/user/Fuzzer/bin/qemu/bin/qemu-system-x86_64",
    "-m", "2G",
    "-smp", "2",
    "-machine", "q35",
    "-kernel", "/home/user/Fuzzer/build/linux/linux-7.1-defconfig/arch/x86/boot/bzImage",
    "-append", "console=ttyS0 root=/dev/vda earlyprintk=serial net.ifnames=0 panic_on_warn=0 pci=realloc selinux=0 enforcing=0",
    "-drive", "file=/home/user/Fuzzer/third_tool/Image-7.0/bullseye.img,format=raw,if=virtio",
    "-device", "pcie-root-port,id=rp1",
    # "-device", "xxx,bus=rp1",  <-- 将在循环中动态插入于此
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
    # 1. 加载 JSON 文件
    if not os.path.exists(JSON_FILE):
        print(f"[-] 错误: 找不到 JSON 文件 {JSON_FILE}")
        return

    with open(JSON_FILE, 'r') as f:
        devices_data = json.load(f)

    print(f"[*] 成功加载 {len(devices_data)} 个待测试的设备。")

    # ========== 新增部分：加载基线设备数据和历史差异数据 ==========
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

    # 2. 遍历所有设备
    for device_name in devices_data.keys():
        print(f"\n=============================================")
        print(f"[*] 正在测试设备: {device_name}")
        
        # 构造当前设备的 QEMU 启动命令
        cmd = QEMU_CMD_BASE.copy()
        
        # 动态插入 -device xxx,bus=rp1 (定位到 pcie-root-port 之后)
        rp1_index = cmd.index("pcie-root-port,id=rp1") + 1
        cmd.insert(rp1_index, "-device")
        cmd.insert(rp1_index + 1, f"{device_name},bus=rp1")

        # 启动 QEMU，并将标准输出和错误重定向到该设备的专属日志
        log_path = f"/home/user/Fuzzer/code/python/log/vm_{device_name}.log"
        # 确保日志目录存在
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        print(f"[*] 启动 QEMU，日志将写入: {log_path}")
        
        with open(log_path, "w") as log_file:
            qemu_proc = subprocess.Popen(cmd, stdout=log_file, stderr=subprocess.STDOUT)

        # 3. 等待虚拟机启动
        print("[*] 等待 90 秒让虚拟机完全启动...")
        time.sleep(90)

        # 4. 通过 SSH 执行 lspci 命令
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

        # ========== 扩展功能：如果驱动成功挂载，执行 tree /dev 并对比 ==========
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

                # 保存原版 tree 结果
                output_dir = "/home/user/Fuzzer/code/python/dev_txt"
                os.makedirs(output_dir, exist_ok=True)
                output_file = os.path.join(output_dir, f"{device_name}.txt")

                with open(output_file, "w") as f:
                    f.write(tree_output)
                print(f"[+] tree /dev 完整输出已保存至: {output_file}")

                # === 核心对比逻辑 ===
                current_tree_lines = tree_output.splitlines()
                
                # 使用 difflib 计算差异（返回一个生成器）
                diff_generator = difflib.ndiff(base_tree_lines, current_tree_lines)
                
                # 过滤差异，仅保留以 "+ " (新增) 或 "- " (删除) 开头的行
                changes = [line for line in diff_generator if line.startswith('+ ') or line.startswith('- ')]
                
                # 将差异结果保存进字典
                changedev_data[device_name] = changes
                
                # 实时刷新写入 changedev.json
                with open(CHANGE_DEV_JSON, "w", encoding="utf-8") as cf:
                    json.dump(changedev_data, cf, indent=4, ensure_ascii=False)
                    
                print(f"[+] 差异对比完成，共发现 {len(changes)} 处变动，已更新至: {CHANGE_DEV_JSON}")

            except subprocess.TimeoutExpired:
                print("[!] tree 命令执行超时")
            except Exception as e:
                print(f"[!] 执行 tree 命令时发生未知错误: {e}")
        # ===========================================================

        # 5. 更新 JSON 并实时保存
        devices_data[device_name]["result"] = result_flag
        with open(JSON_FILE, 'w') as f:
            json.dump(devices_data, f, indent=4)

        # 6. 杀死 QEMU 进程，准备下一个测试
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
