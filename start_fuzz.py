#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import os
import subprocess
import signal
import shutil
import time
from threading import Timer, Thread
from copy import deepcopy

# =========================
# 路径配置
# =========================
FUZZ_PATH = "/home/user/Fuzzer/code/config/2_gpt-5.1-fuzz.json"
ENABLE_SYSCALL_PATH = "/home/user/Fuzzer/code/config/enable_syscalls.json"

DIR_A = "/home/user/Fuzzer/third_tool/syzkaller_syzbot_nodev"
DIR_B = "/home/user/Fuzzer/third_tool/syzkaller_syzbot_dev"
DIR_C = "/home/user/Fuzzer/third_tool/syzkaller_defconfig_nodev"
DIR_D = "/home/user/Fuzzer/third_tool/syzkaller_defconfig_dev"

SYZBOT_OUT_DIR = "/home/user/Fuzzer/out/fuzz_result/3_Gemini_3pro"
NON_SYZBOT_OUT_DIR = "/home/user/Fuzzer/out/fuzz_result/3_Gemini_3pro"

TIMEOUT = 60 * 60          # 单个 syz-manager 最长 60 分钟
DEVICE_SLEEP = 20          # 每个 device 完成后休眠 20s


# =========================
# 工具函数
# =========================
def kill_process_group(proc):
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        print(f"[+] Killed process group {proc.pid}")
    except Exception as e:
        print(f"[!] Kill failed: {e}")


def run_syz(cfg_name, workdir, bench_path):
    old_cwd = os.getcwd()
    os.chdir(workdir)

    cmd = f"./bin/syz-manager -config={cfg_name} -bench={bench_path}"
    print(f"[CMD] ({workdir}) {cmd}")

    proc = subprocess.Popen(
        cmd,
        shell=True,
        preexec_fn=os.setsid,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    timer = Timer(TIMEOUT, kill_process_group, [proc])
    timer.start()

    try:
        out, err = proc.communicate()
        if out:
            print(out.decode(errors="ignore"))
        if err:
            print(err.decode(errors="ignore"))
    finally:
        timer.cancel()
        os.chdir(old_cwd)


def run_syz_task(cfg_name, workdir, bench_path):
    try:
        run_syz(cfg_name, workdir, bench_path)
    except Exception as e:
        print(f"[!] syz-manager failed ({cfg_name}): {e}")


def release_ports(ports):
    """
    释放指定端口，尝试使用 fuser -k 或 lsof + kill
    """
    for port in ports:
        # 方法1：fuser -k
        try:
            subprocess.run(
                ["fuser", "-k", f"{port}/tcp"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=5
            )
            continue
        except FileNotFoundError:
            pass
        except Exception:
            pass

        # 方法2：lsof + kill
        try:
            result = subprocess.run(
                ["lsof", "-ti", f":{port}"],
                capture_output=True, text=True, timeout=5
            )
            pids = result.stdout.strip().split()
            for pid in pids:
                try:
                    os.kill(int(pid), signal.SIGKILL)
                except Exception:
                    pass
        except FileNotFoundError:
            print(f"[!] Neither fuser nor lsof found, cannot release port {port}")
        except Exception:
            pass


def collect_enable_syscalls_sequential(device_path, enable_syscall_db):
    """
    严格顺序查找：
    - 按 enable_syscalls.json 原始顺序遍历到文件末尾
    - drivers 任一命中即追加 syscalls
    - 不提前终止
    - 保留原始顺序
    """
    result = []
    seen = set()

    for entry in enable_syscall_db.values():
        drivers = entry.get("drivers", [])
        syscalls = entry.get("syscalls", [])

        for drv in drivers:
            if drv and drv in device_path:
                for sc in syscalls:
                    if sc not in seen:
                        result.append(sc)
                        seen.add(sc)
                break

    return result


# =========================
# 主流程
# =========================
with open(FUZZ_PATH, "r") as f:
    fuzz_cfg = json.load(f)

with open(ENABLE_SYSCALL_PATH, "r") as f:
    enable_syscall_db = json.load(f)

os.makedirs(SYZBOT_OUT_DIR, exist_ok=True)
os.makedirs(NON_SYZBOT_OUT_DIR, exist_ok=True)

# 当天日期后缀，格式 MMDD
DATE_SUFFIX = time.strftime("%m%d")

for device, info in fuzz_cfg.items():
    print("\n========================================")
    print(f"[DEVICE] {device}")
    print("========================================")

    # === 每次启动前先释放目标端口（避免 address already in use） ===
    print("[INFO] Releasing ports 10001-10004 before starting device ...")
    release_ports([10001, 10002, 10003, 10004])

    # 已处理直接跳过
    if info.get("enable_syscalls"):
        print(f"[SKIP] {device}: enable_syscalls already populated")
        continue

    flag = info.get("in_syzbot_config", "").lower()
    device_path = info.get("path", "")

    # 1. 顺序计算 enable_syscalls
    enable_syscalls = collect_enable_syscalls_sequential(
        device_path,
        enable_syscall_db
    )

    # 2. 写回 fuzz.json 内存结构
    info["enable_syscalls"] = enable_syscalls

    # 3. 选择 AB / CD（组内并行）
    if flag == "y":
        targets = [
            (DIR_A, False, SYZBOT_OUT_DIR),
            (DIR_B, True,  SYZBOT_OUT_DIR),
        ]
    elif flag == "n":
        targets = [
            (DIR_C, False, NON_SYZBOT_OUT_DIR),
            (DIR_D, True,  NON_SYZBOT_OUT_DIR),
        ]
    else:
        print(f"[!] Skip {device}: invalid in_syzbot_config")
        continue

    threads = []

    # 4. 并行执行 A/B 或 C/D
    for workdir, add_device, bench_root in targets:
        base_cfg_path = os.path.join(workdir, "cfg.json")
        if not os.path.exists(base_cfg_path):
            print(f"[!] cfg.json not found in {workdir}, skip")
            continue

        with open(base_cfg_path, "r") as f:
            cfg = deepcopy(json.load(f))

        cfg["enable_syscalls"] = enable_syscalls

        if add_device:
            cfg["vm"]["qemu_args"] += f" -device pcie-root-port,id=rp1 -device {device}_pci,bus=rp1"

        suffix = f"_{DATE_SUFFIX}"
        bench_name = f"{device}{suffix}.bench" if add_device else f"{device}_no{suffix}.bench"
        cfg_name = f"cfg_{device}{suffix}.json"

        cfg_path = os.path.join(workdir, cfg_name)

        with open(cfg_path, "w") as f:
            json.dump(cfg, f, indent=4)

        bench_path = os.path.join(bench_root, bench_name)

        t = Thread(
            target=run_syz_task,
            args=(cfg_name, workdir, bench_path),
            daemon=True
        )
        t.start()
        threads.append((t, cfg_path, bench_root))

    # 5. 等待 AB / CD 全部完成
    for t, cfg_path, bench_root in threads:
        t.join()
        shutil.move(cfg_path, os.path.join(bench_root, os.path.basename(cfg_path)))
        print(f"[+] cfg archived: {bench_root}/{os.path.basename(cfg_path)}")

    # 6. device 完成后休眠
    print(f"[INFO] device {device} done, sleeping {DEVICE_SLEEP}s")
    time.sleep(DEVICE_SLEEP)

# =========================
# 最终写回 fuzz.json
# =========================
with open(FUZZ_PATH, "w") as f:
    json.dump(fuzz_cfg, f, indent=4)

print("[+] fuzz.json updated")
