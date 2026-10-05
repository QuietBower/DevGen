import os
from vdm.utils.logger import setup_logger
from vdm.services.config_loader import ConfigLoader
import json

logger = setup_logger("examples_loader")

class ExampleLoader:
    """
    用于加载示例的加载器
    """
    def __init__(self, _examples_path: str = None):
        self.examples_path = _examples_path
        self.examples_type_of_error_map = None

        # 加载配置
        config_loader = ConfigLoader()
        self.config = config_loader.load()

        if _examples_path is None:
            self.examples_path = self.config["paths"]["step4_examples_path"]

        # 加载错误类型字典
        with open(f"{self.examples_path}/step.json", "r", encoding="utf-8") as f:
            self.examples_type_of_error_map = json.load(f)
            logger.info("Loaded step4.json")

        logger.info("Initialized ExampleLoader")
    
    def get_examples_by_err_msg(self, err_msg: str) -> str:
        for key in self.examples_type_of_error_map:
            if key in err_msg:
                return self.get_all_files_content_from_dir(f"{self.examples_path}/{self.examples_type_of_error_map[key]}")
        return "No examples found for this error message"

    def get_all_files_content_from_dir(self, dir_path: str) -> str:
        """
        获取目录下的所有文件的内容
        :param dir_path: 目录路径
        :return: 文件内容拼接后的字符串
        """
        result = ""
        
        # Check if directory exists
        if not os.path.exists(dir_path):
            raise FileNotFoundError(f"Directory {dir_path} does not exist")
        
        # Iterate through all files in the directory
        for filename in os.listdir(dir_path):
            file_path = os.path.join(dir_path, filename)
            
            # Only process files, not subdirectories
            if os.path.isfile(file_path):
                try:
                    with open(file_path, 'r', encoding='utf-8') as file:
                        content = file.read()
                        result += f"\n--- Content of {filename} ---\n"
                        result += content
                        result += f"\n--- End of {filename} ---\n"
                except Exception as e:
                    # Skip files that cannot be read
                    result += f"\n--- Error reading {filename}: {str(e)} ---\n"
        
        return result

exampleloader = ExampleLoader()

if __name__ == "__main__":
    with open("/home/user/qemu_8.2_linux_6.7/src/out/shares/err_msg.txt", "r") as f:
        err_msg = f.read()

    examples = exampleloader.get_examples_by_err_msg(err_msg)

    print(examples)