import subprocess
import os
import shutil
from vdm.utils.logger import setup_logger
from vdm.services.config_loader import ConfigLoader

config_loader = ConfigLoader()
config = config_loader.load()
logger = setup_logger("build qemu")

def recreate_directory(dir_path):
    """
    重建目录：如果存在则删除后重建，不存在则直接创建
    """
    # 确保父目录存在
    parent_dir = os.path.dirname(dir_path)
    if parent_dir and not os.path.exists(parent_dir):
        os.makedirs(parent_dir, exist_ok=True)
    
    # 如果目录存在，删除它
    if os.path.exists(dir_path):
        shutil.rmtree(dir_path)
    
    # 创建新目录
    os.makedirs(dir_path, exist_ok=True)

    logger.info(f"已重建目录：{dir_path}")

def build_qemu() -> bool:
    # build_path = "/home/user/qemu_8.2_linux_6.7/src/qemu-stable-8.2/build"
    build_path = config["paths"]["qemu_build_path"]
    # 重建build_path
    recreate_directory(build_path)

    # 运行构建命令
    try:
        process = subprocess.run(
            ["bash", config["paths"]["qemu_build_sh_path"]],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=build_path
        )
        
        logger.info("=== 标准输出 ===")
        logger.info(process.stdout)
        
        if process.stderr:
            logger.info("=== 错误信息 ===")
            logger.info(process.stderr)
            
        return True
        
    except subprocess.CalledProcessError as e:
        # 从异常对象 e 中获取 stdout 和 stderr
        logger.info("命令执行失败！")
        logger.info(f"返回码: {e.returncode}")
        logger.info("=== 标准输出 ===")
        logger.info(e.stdout if e.stdout else "(无标准输出)")
        logger.info("=== 错误信息 ===")
        logger.info(e.stderr if e.stderr else "(无错误输出)")

        logger.info(f"保存错误信息到文件{config['paths']['qemu_out_share_path']}/build_fail.txt")
        with open(f"{config['paths']['qemu_out_share_path']}/build_fail.txt", "w") as f:
            f.write(e.stderr)
            f.write("\n")
            f.write(e.stdout)

        return False
        
    except FileNotFoundError:
        logger.info("构建脚本未找到")
        return False

if __name__ == "__main__":
    build_qemu()