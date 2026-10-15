import openai
from typing import Optional, Dict, List
from vdm.services.config_loader import ConfigLoader
from vdm.utils.retry_api_call import retry_api_call, log_retry_details
from vdm.utils.logger import setup_logger
from requests.exceptions import RequestException    

logger = setup_logger('llm_client')

class LLMClient:
    """
    LLM客户端 - 单例模式
    """
    _instance = None
    _initialized = False
    
    def __new__(cls):
        if cls._instance is None:
            cls._instance = super(LLMClient, cls).__new__(cls)
        return cls._instance
    
    def __init__(self, provider: str = None):
        if not self._initialized:
            config_loader = ConfigLoader()
            self.config = config_loader.load()

            # Set the default provider
            self.provider = provider or self.config['project']['default_provider']
            self.client = None
            
            self._setup_client()
            self._initialized = True
            
            logger.info(f"LLMClient initialized with provider: {self.provider}")
    
    def _setup_client(self):
        """设置客户端连接"""
        try:
            self.client = openai.OpenAI(
                api_key=self.config['models'][self.provider]["api_key"],
                base_url=self.config['models'][self.provider]['api_base']
            )
            logger.info(f"Client setup completed for {self.provider}")
            
        except Exception as e:
            logger.error(f"Failed to setup client for {self.provider}: {e}")
            raise
    
    @retry_api_call(
        max_retries=10,
        initial_delay=60.0,  # 1 minute
        backoff_factor=2.0,  # Double each time
        exceptions=(Exception, RequestException),
        on_retry=log_retry_details  # Optional callback
    )
    def chat_completion_json(self, 
                       messages: List[Dict[str, str]], 
                       model: Optional[str] = None,
                       **kwargs) -> Optional[str]:
        """
        统一的聊天补全接口
        
        Args:
            messages: 消息列表
            model: 模型名称，为None时使用默认模型
            **kwargs: 其他参数
            
        Returns:
            模型回复内容
        """
        if self.client is None:
            logger.error("Client not initialized")
            return None
        
        # Get the model configuration
        if model is None:
            model = self.config['models'][self.provider]['default_model']
        
        # Merge the parameters
        params = {
            "model": model,
            "messages": messages,
            "stream": False,
            "response_format":{
                'type': 'json_object'
            },
            "max_tokens": self.config['models'][self.provider]['max_tokens'],
            "temperature": 0.1,
            **kwargs
        }
        
        logger.info(f"Sending request to {self.provider} model: {model}")
        
        response = self.client.chat.completions.create(**params)

        logger.info(f"received response: {response}")
        content = response.choices[0].message.content
        logger.info(f"Received response with {len(content)} characters")

        # Record token usage
        usage = response.usage
        if usage:
            logger.info(f"Token usage - Prompt: {usage.prompt_tokens}, Completion: {usage.completion_tokens}, Total: {usage.total_tokens}")

        finish_reason = response.choices[0].finish_reason

        logger.info(f"Finish reason: {finish_reason}")

        return content, finish_reason
        
                
# Create a global instance
client = LLMClient()

if __name__ == "__main__":
    # Test code
    messages = [
        {"role": "system", "content": "You are a helpful assistant."},
        {"role": "user", "content": "What is the capital of France? replace me with a JSON object"}
    ]
    
    response = client.chat_completion_json(messages)
    print(response)