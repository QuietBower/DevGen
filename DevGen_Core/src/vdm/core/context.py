# Global context variables holding runtime-wide information, such as the program start time and the driver being modeled
import time

# Program start time (set at the program entry point)
CURRENT_MODEL_START_TIME = None
DRIVER_NAME = None

def initialize_app_start_time():
    """在程序入口处调用此函数来设置启动时间"""
    global CURRENT_MODEL_START_TIME
    CURRENT_MODEL_START_TIME = time.strftime("%Y-%m-%d-%H-%M-%S", time.localtime())
    return CURRENT_MODEL_START_TIME

def get_app_start_time() -> str:
    """获取程序启动时间，用于命名日志文件"""
    if CURRENT_MODEL_START_TIME is None:
        raise ValueError("应用程序启动时间未初始化，请先调用 initialize_app_start_time()")
    return CURRENT_MODEL_START_TIME

def set_driver_name(driver_name):
    """设置本次建模的驱动名称        
    """
    global DRIVER_NAME
    DRIVER_NAME = driver_name

def get_driver_name() -> str:
    """获得本次建模的驱动名称，用于命名日志文件、加载静态分析文件、命名建模代码等
    """
    if DRIVER_NAME is None:
        raise ValueError("驱动名称未设置，请先调用 set_driver_name()")
    return DRIVER_NAME

def get_driver_name_replace() -> str:
    """获得本次建模的驱动名称，将-换成_，用于命名设备名称
    """
    if DRIVER_NAME is None:
        raise ValueError("驱动名称未设置，请先调用 set_driver_name()")
    return DRIVER_NAME.replace("-", "_")