import subprocess
import shlex # Used to safely split the command string

def kill_qemu_with_subprocess():
    """
    使用 subprocess 模拟 'ps aux | grep qemu' 并杀死进程。
    """
    print("--- 任务准备阶段 (使用subprocess) ---")
    
    # 1. Run the 'ps aux' command
    try:
        # Using shell=False is safer, but the command must be split manually
        # ps_command = ['ps', 'aux']
        # ps_output = subprocess.check_output(ps_command, text=True)
        
        # To emulate a pipe we could use shell=True, but we must ensure the input is trusted
        ps_command = "ps aux | grep '[q]emu'"
        # grep '[q]emu' is a small trick to prevent the grep process itself from being matched
        ps_output = subprocess.check_output(ps_command, shell=True, text=True)
        
    except subprocess.CalledProcessError:
        # If grep finds nothing it returns a non-zero exit code, causing an exception
        print("未发现正在运行的 QEMU 进程。")
        print("--- QEMU 环境清理完毕，开始执行核心任务 ---")
        return 

    # 2. Parse the output and extract the PIDs
    pids_to_kill = []
    for line in ps_output.strip().split('\n'):
        if line:
            # In the output of ps aux, the PID is the second column
            parts = line.split()
            pid = parts[1]
            pids_to_kill.append(pid)
            print(f"发现 QEMU 进程: PID={pid}")

    # 3. Kill the PIDs that were found
    if pids_to_kill:
        for pid in pids_to_kill:
            try:
                kill_command = ['sudo', 'kill', '-9', pid] # -9 means force-kill
                subprocess.run(kill_command, check=True)
                print(f"已成功杀死 PID {pid}")
            except subprocess.CalledProcessError as e:
                print(f"杀死 PID {pid} 失败: {e}")
    
    print("--- QEMU 环境清理完毕，开始执行核心任务 ---")


# --- Usage example ---
def my_main_task():
    """
    核心业务函数。
    """
    print("正在执行核心任务...")
    print("核心任务完成！")
    return "任务成功"

if __name__ == "__main__":
    print("准备调用 my_main_task()...")
    # Before calling the function, run the cleanup function manually
    kill_qemu_with_subprocess()
    
    # Then call your core function
    result = my_main_task()
    print(f"函数返回结果: {result}")
