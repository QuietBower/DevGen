# src/vdm_qemu_llm/services/config_loader.py

import os
import yaml
from pathlib import Path
from typing import Dict, Any, List

class ConfigLoader:
    """
    加载和管理 YAML 配置文件。
    支持多环境配置和环境变量注入。
    """
    def __init__(self, config_dir: Path = Path("./config")):
        """
        初始化配置加载器。

        :param config_dir: 存放配置文件的目录。
        """
        self.config_dir = Path(config_dir)
        if not self.config_dir.is_dir():
            raise FileNotFoundError(f"Configuration directory not found: {self.config_dir}")
        
        self.config: Dict[str, Any] = {}

    def load(self) -> Dict[str, Any]:
        """
        加载指定环境的配置。
        """
        # 1. Load the default configuration
        default_path = self.config_dir / "default.yaml"
        if not default_path.exists():
            raise FileNotFoundError(f"Default configuration file not found: {default_path}")
        
        with open(default_path, 'r', encoding='utf-8') as f:
            self.config = yaml.safe_load(f) or {}
        
        return self.config


if __name__ == '__main__':
    pass
