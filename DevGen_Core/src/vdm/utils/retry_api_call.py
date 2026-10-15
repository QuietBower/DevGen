import functools
import time
import logging
from typing import List, Dict, Optional, Tuple, Any, Callable, Type
from requests.exceptions import RequestException

# Configure logging
logger = logging.getLogger(__name__)

def retry_api_call(
    max_retries: int = 10,
    initial_delay: float = 60.0,  # 1 minute
    backoff_factor: float = 2.0,
    exceptions: Tuple[Type[Exception], ...] = (Exception, RequestException),
    on_retry: Optional[Callable] = None
):
    """
    API调用重试装饰器
    
    Args:
        max_retries: 最大重试次数
        initial_delay: 初始延迟时间（秒）
        backoff_factor: 退避系数
        exceptions: 需要重试的异常类型
        on_retry: 重试回调函数
    """
    def decorator(func: Callable) -> Callable:
        @functools.wraps(func)
        def wrapper(*args, **kwargs) -> Any:
            last_exception = None
            
            for attempt in range(max_retries + 1):
                try:
                    return func(*args, **kwargs)
                    
                except exceptions as e:
                    last_exception = e
                    
                    # If this is the last attempt, do not retry
                    if attempt == max_retries:
                        logger.error(
                            f"函数 '{func.__name__}' 第{attempt + 1}次尝试失败，"
                            f"已达最大重试次数 {max_retries}，最终异常: {e}"
                        )
                        break
                    
                    # Compute the wait time (exponential backoff)
                    wait_time = initial_delay * (backoff_factor ** attempt)
                    
                    logger.warning(
                        f"函数 '{func.__name__}' 第{attempt + 1}次尝试失败: {e}. "
                        f"{wait_time}秒后进行第{attempt + 2}次尝试..."
                    )
                    
                    # Call the retry callback function
                    if on_retry:
                        on_retry(attempt + 1, e, wait_time)
                    
                    # Wait
                    time.sleep(wait_time)
            
            # All attempts failed; raise the last exception
            raise last_exception
        
        return wrapper
    return decorator

# Optional retry callback function
def log_retry_details(attempt: int, exception: Exception, wait_time: float):
    """记录重试详细信息"""
    logger.info(f"🔁 重试第{attempt}次 | 异常: {type(exception).__name__} | 等待: {wait_time}秒")