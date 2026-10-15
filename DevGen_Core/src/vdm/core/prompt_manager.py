import os
import glob
from typing import Dict, Optional
from vdm.utils.logger import setup_logger

#Set up the logger
logger = setup_logger("prompt_manager")

class PromptManager:
    """
    简化的 Prompt 管理器 - 只负责 prompt 的加载和变量填充
    """
    
    def __init__(self, prompts_base_dir: str = "prompts"):
        self.prompts_base_dir = prompts_base_dir
        self.prompts: Dict[str, str] = {}
        
        # Load all prompts at initialization
        self._load_all_prompts()
    
    def _load_all_prompts(self):
        """加载所有prompt文件"""
        if not os.path.exists(self.prompts_base_dir):
            logger.warning(f"Prompt目录不存在: {self.prompts_base_dir}")
            return
        
        # Recursively load all txt files
        for file_path in glob.glob(os.path.join(self.prompts_base_dir, "**", "*.txt"), recursive=True):
            try:
                # Use the relative path as the prompt name
                relative_path = os.path.relpath(file_path, self.prompts_base_dir)
                prompt_name = os.path.splitext(relative_path)[0].replace(os.path.sep, ".")
                
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read().strip()
                    self.prompts[prompt_name] = content
                    
            except Exception as e:
                logger.error(f"加载prompt失败 {file_path}: {e}")
        
        logger.info(f"已加载 {len(self.prompts)} 个prompt模板")
    
    def get_prompt(self, prompt_name: str, **kwargs) -> str:
        """
        获取prompt并填充变量
        
        Args:
            prompt_name: prompt名称，如 'templates.code_review' 或 'system.senior_developer'
            **kwargs: 模板变量
            
        Returns:
            填充后的完整prompt内容
            
        Raises:
            ValueError: prompt不存在或变量缺失时抛出
        """
        if prompt_name not in self.prompts:
            available_prompts = list(self.prompts.keys())
            raise ValueError(f"Prompt '{prompt_name}' 不存在。可用prompt: {available_prompts}")
        
        template_content = self.prompts[prompt_name]
        
        try:
             # Replace the text
            for key, value in kwargs.items():
                template_content = template_content.replace(f"`{key}`", str(value))   
            
            logger.info(f"prompt填充成功: {prompt_name}")
            return template_content

        except KeyError as e:
            missing_key = str(e).strip("'")
            raise ValueError(f"Prompt '{prompt_name}' 需要变量 '{missing_key}'，但未提供")
        except Exception as e:
            raise ValueError(f"Prompt填充失败: {e}")
    
    def reload_prompts(self):
        """重新加载prompt文件"""
        self.prompts.clear()
        self._load_all_prompts()
        logger.info("Prompt文件重新加载完成")

# Create a global instance
prompt_manager = PromptManager()


if __name__ == "__main__":
    prompt_manager = PromptManager()
    print(list(prompt_manager.prompts.keys()))