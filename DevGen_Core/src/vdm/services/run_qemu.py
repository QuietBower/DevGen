#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import subprocess
import threading
from vdm.services.config_loader import ConfigLoader

config_loader = ConfigLoader()
config = config_loader.load()

def _pipe_reader(pipe, file_obj):
    """仅把管道内容写文件，不写终端/logger"""
    try:
        for line in iter(pipe.readline, ''):
            file_obj.write(line)      # Preserve the original newlines
            file_obj.flush()
    finally:
        pipe.close()

def run_qemu():
    script_path = f"{config['paths']['qemu_run_dir']}/run.sh"
    if not os.path.isfile(script_path):
        return None

    vm_log_path = f"{config['paths']['qemu_out_share_path']}/vm.log"
    os.makedirs(os.path.dirname(vm_log_path), exist_ok=True)
    vm_log_fp = open(vm_log_path, 'a', buffering=1)   # Line buffering

    proc = subprocess.Popen(
        ["bash", script_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding='utf-8',
        errors='ignore', 
        cwd=os.path.dirname(script_path),
    )

    # The daemon thread is only responsible for writing to the file
    threading.Thread(target=_pipe_reader, args=(proc.stdout, vm_log_fp), daemon=True).start()
    threading.Thread(target=_pipe_reader, args=(proc.stderr, vm_log_fp), daemon=True).start()

    # Bind the file handle to the object to prevent it from being closed early by GC
    proc._vm_log_fp = vm_log_fp
    return proc

if __name__ == "__main__":
    qemu = run_qemu()
    if qemu:
        print("QEMU 已在后台运行，日志写入 vm.log。")
        try:
            qemu.wait()
        except KeyboardInterrupt:
            print("\n脚本退出，QEMU 仍在后台。")
    else:
        print("启动 QEMU 失败！")