import json
from pathlib import Path
from typing import Any, Union
from vdm.utils.logger import setup_logger
from vdm.services.config_loader import ConfigLoader

logger = setup_logger("file_logger")

config_loader = ConfigLoader()
config = config_loader.load()

class FileLogger:
    """
    文件日志记录器
    """
    def __init__(self, base_dir: str = "logs"):
        """
        初始化日志记录器
        
        Args:
            base_dir: 日志文件的基础目录
        """
        self.base_dir = Path(f"{config['paths']['logs_path']}")
        self.base_dir.mkdir(parents=True, exist_ok=True)
    
    def save_messages(self, messages: list, file_base_path: str):
        """保存messages消息到文件

        Args:
            messages (list): 传递给大模型的messages消息
            file_base_path (str): 指定保存文件的路径
        """
        index = 1

        for message in messages:
            file_path = file_base_path + f"/message_{index}.txt"
            content = message['role'] + ":\n" + message['content'] + "\n\n"
            self.save_content(content, file_path)

            index += 1

    def save_response(self, reponse: dict, file_path: str):
        """保存大模型响应到文件

        Args:
            reponse (dict): 响应结果
            file_path (str): 保存文件路径
        """

        content = "code:\n" + reponse['code'] + "\n\n" + "needed_sources:\n"
        for source in reponse['needed_sources']:
            content += source + "\n"
            
        self.save_content(content, file_path)
        
    def save_content(
        self, 
        content: Any, 
        file_path: Union[str, Path],
        mode: str = 'w',
        encoding: str = 'utf-8',
        ensure_dir: bool = True
    ) -> str:
        """
        保存内容到文件
        
        Args:
            content: 要保存的内容（字符串、字典、列表等）
            file_path: 文件路径，可以是相对路径或绝对路径
            mode: 写入模式 'w'覆盖 / 'a'追加
            encoding: 文件编码
            ensure_dir: 是否确保目录存在
            
        Returns:
            保存的文件绝对路径
        """
        # Handle the file path
        file_path = Path(file_path)
        if not file_path.is_absolute():
            file_path = self.base_dir / file_path
        
        # Ensure the directory exists
        if ensure_dir:
            file_path.parent.mkdir(parents=True, exist_ok=True)
        
        # Convert the content to a string
        if isinstance(content, (dict, list)):
            content_str = json.dumps(content, ensure_ascii=False, indent=2)
        else:
            content_str = str(content)
        
        # Write the file
        with open(file_path, mode, encoding=encoding) as f:
            f.write(content_str)
        
        logger.info(f"内容已保存到: {file_path.absolute()}")

# Create a global instance
default_logger = FileLogger()



if __name__ == '__main__':
    # Test the save_messages method
    # file_path = "/home/user/Devmod/logs/2025-11-15-16-55-46_aectc/step1/messages.txt"

    # with open(file_path, 'r', encoding='utf-8') as f:
    #     content = f.read()

    # content = json.loads(content)

    # default_logger.save_messages(content, "test")
    # Test the save_response method
    file_path = "/home/user/Devmod/logs/2025-11-15-16-32-52_adm8211/step1/answer.txt"

    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    content = json.loads(content)
    print(type(content))
    default_logger.save_response(content, "test.txt")