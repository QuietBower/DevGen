from vdm.utils.file_op import write_to_file
from vdm.utils.kill_process import kill_qemu_with_subprocess
from vdm.services.config_loader import ConfigLoader
from vdm.services.build_qemu import build_qemu
from vdm.services.run_qemu import run_qemu
from vdm.services.execute_ssh_commands_and_get_files import execute_ssh_commands_and_get_files
import time
from vdm.utils.logger import setup_logger
import os

logger = setup_logger("modify compile run")

config_loader = ConfigLoader()
config = config_loader.load()


def modify_compile_run(driver_name, device_code) -> tuple[int, bool]:
    """返回一个int一个bool，第一个为运行结果代码（0表示编译并启动成功，2为编译失败，1为启动失败），第二个为是否出现kernel in use
    """
    # Copy (or overwrite) the generated QEMU device code into hw/fake_pci
    write_to_file(f"{config['paths']['qemu_hw_fack_pci_path']}/{driver_name}_pci.c", device_code)

    logger.info(f"{driver_name}QEMU设备代码，成功保存至{config['paths']['qemu_hw_fack_pci_path']}/{driver_name}_pci.c")

    # Run the build.sh script to build QEMU
    logger.info("开始编译QEMU")
    res = build_qemu()
    
    if res == False:
        logger.info("Build QEMU failed")
        return 2, False
        # Stop building and modeling this device, save the current information, and remove the appended meson.build content
    else:
        logger.info("Build QEMU success")

    # Start QEMU
    logger.info("开始启动QEMU")
    # Kill the QEMU process that did not stop properly
    kill_qemu_with_subprocess()
    # Delete the old pid file
    pid_file = f"{config['paths']['qemu_run_dir']}/vm.pid"

    if os.path.isfile(pid_file):
        os.remove(pid_file)

    time.sleep(20)
    
    res_process = run_qemu()
    if res_process:
        logger.info("Run QEMU success")
    else:
        logger.info("Run QEMU failed")
        return 1, False

    # Run commands inside QEMU to obtain dev.txt and load.txt
    logger.info("开始执行ssh命令，获得dev.txt和load.txt")
    is_have_kernel_in_use, res = execute_ssh_commands_and_get_files()

    if res == 0:
        logger.info("Get files successfully")
        if is_have_kernel_in_use:
            logger.info("Kernel in use")
            return 0, True
        else:
            logger.info("Kernel not in use")
            return 0, False
    else:
        logger.info("Get files failed")
        return res, False


if __name__ == "__main__":
    with open("/home/user/qemu_8.2_linux_6.7/src/qemu-stable-8.2/hw/fake_pci/aectc_pci.c", "r", encoding="utf-8") as f:
        device_code = f.read()
    modify_compile_run("aectc", device_code)
    