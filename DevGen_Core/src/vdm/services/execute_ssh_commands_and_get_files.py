import subprocess
import shlex
import re
import time
from vdm.utils.logger import setup_logger
from vdm.core.context import get_driver_name
from vdm.services.config_loader import ConfigLoader

config_loader = ConfigLoader()
config = config_loader.load()

logger = setup_logger("execute_ssh_commands_and_get_files")

def check_is_running() -> bool:
    logger.info("Checking if QEMU is running...")
    with open(f"{config['paths']['qemu_out_share_path']}/vm.log", "r", encoding='utf-8', errors="ignore") as f:
        return "syzkaller login:" in f.read()

def execute_ssh_commands_and_get_files() -> tuple[bool, int]:
    """返回一个bool，一个int，分别为是否出现kernel in use，执行结果代码（0为成功，1为ssh连接失败（启动qemu失败），2为执行ssh命令失败）
    """
    # 1. Define the base SSH command (specify the key, host and port)
    ssh_base = (
        f"ssh -i {config['paths']['id_rsa_path']} "
        "root@127.0.0.1 -p 10021"
    )
    
    # 2. Define the commands to run on the remote host
    remote_commands = [
        "mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt",
        "lspci -k -s 00:04.0> /mnt/load.txt",
        "tree /dev > /mnt/dev.txt"
    ]
    
    remote_cmd_str = " && ".join(remote_commands)
    full_ssh_cmd = f"{ssh_base} {shlex.quote(remote_cmd_str)}"

    try:
        # Wait 20 seconds; for unknown reasons, running immediately makes the SSH connection fail
        time.sleep(30)
        # Check whether vm.log contains "syzkaller login:"
        try_cnt = 10
        logger.info("开始执行远程命令...")
        
        while try_cnt > 0:
            logger.info(f"尝试次数：{try_cnt}")
            if check_is_running():
                logger.info("Qemu启动成功，开始ssh连接")
                # Run the remote command (create a virtual terminal to avoid garbled output)
                result = subprocess.run(
                    full_ssh_cmd,
                    shell=True,
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    encoding="utf-8"
                )

                if result.returncode == 0:
                    logger.info("远程命令执行成功！")
                    break
                else:
                    logger.error(f"远程命令执行失败！错误信息：{result.stderr}")
                    logger.error("远程命令执行失败！")

                    # Save the SSH connection failure information
                    with open(f"{config['paths']['qemu_out_share_path']}/ssh_failed.txt", "w", encoding="utf-8") as f:
                        f.write(result.stderr)
                    
                    return False, 2
            else:
                logger.info("qemu未启动完成，等待30秒...")
                time.sleep(30)

            try_cnt -= 1
        
        if try_cnt == 0:
            logger.error("尝试次数已用完，无法连接虚拟机")
            return False, 1

        # 3. Read the remote load.txt and dev.txt contents
        def read_remote_file(file_path):
            cmd = f"{ssh_base} {shlex.quote(f'cat {file_path}')}"
            file_result = subprocess.run(
                cmd,
                shell=True,
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8"
            )
            return file_result.stdout

        load_content = read_remote_file("/mnt/load.txt")
        is_have_kernel_in_use = "Kernel driver in use:" in load_content

        if not is_have_kernel_in_use:
            match = re.search(r'Kernel modules:\s*(\S+)', load_content)
            # If there is no 'Kernel modules' entry, run modprobe on the driver name (without the _pci suffix)
            if match is not None:
                kernel_modules = match.group(1)
                # If kernel_modules exists, run it
                if "," in kernel_modules:
                    kernel_modules = kernel_modules.split(",")[0]
                logger.info(f"找到kernel modules，执行modprobe {kernel_modules}...")
            else:
                return is_have_kernel_in_use, 0

            # Get diagnostic information and shut down
            diag_commands = [
                "dmesg -C",
                f"modprobe {shlex.quote(kernel_modules)}",
                f"modprobe -r {shlex.quote(kernel_modules)}",
                f"modprobe {shlex.quote(kernel_modules)}",
                "echo 0000:00:04.0 > /sys/bus/pci/drivers_probe",
                "dmesg >> /mnt/err_msg.txt",
                "init 0"
            ]

            diag_cmd_str = " && ".join(diag_commands)
            full_diag_ssh_cmd = f"{ssh_base} {shlex.quote(diag_cmd_str)}"

            # Run the diagnostic command (init 0 drops the connection and necessarily returns non-zero, so check=True is not used)
            try:
                logger.info("未找到kernel driver in use，执行诊断命令...，获得err_msg.txt...")
                subprocess.run(
                    full_diag_ssh_cmd,
                    shell=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    encoding="utf-8",
                    timeout=30  
                )
                logger.info("诊断命令已发送，虚拟机正在关机...")
            except subprocess.TimeoutExpired:
                logger.info("诊断命令执行超时（可能已关机）")
                return is_have_kernel_in_use, 2
            except Exception as e:
                # SSH disconnects and closed connections are expected behavior, not errors
                logger.info(f"执行诊断命令时发生异常（可能因关机）: {str(e)}")
                return is_have_kernel_in_use, 2
        else:
            diag_commands = ["init 0"]
            diag_cmd_str = " && ".join(diag_commands)
            full_diag_ssh_cmd = f"{ssh_base} {shlex.quote(diag_cmd_str)}"

            # Run the shutdown command
            try:
                logger.info("已找到kernel driver in use，执行关机命令...")
                subprocess.run(
                    full_diag_ssh_cmd,
                    shell=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    encoding="utf-8",
                    timeout=30  
                )
                logger.info("关机命令已发送，虚拟机正在关机...")
            except subprocess.TimeoutExpired:
                logger.info("关机命令执行超时（可能已关机）")
                return is_have_kernel_in_use, 2
            except Exception as e:
                # SSH disconnects and closed connections are expected behavior, not errors
                logger.info(f"执行关机命令时发生异常（可能因关机）: {str(e)}")
                return is_have_kernel_in_use, 2

        # Obtain the error diagnostic information
        return is_have_kernel_in_use, 0

    except subprocess.CalledProcessError as e:
        logger.info(f"命令执行失败，错误码：{e.returncode}")
        logger.info(f"标准错误：{e.stderr}")
        return False, False
    except Exception as e:
        logger.info(f"未知错误：{str(e)}")
        return False, False
