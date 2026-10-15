import json
from vdm.utils.logger import setup_logger
from vdm.utils.save_logs import default_logger
from vdm.core.llm_client import client
from vdm.core.prompt_manager import prompt_manager
from vdm.core.context import get_app_start_time, get_driver_name_replace, get_driver_name
from vdm.core.driver_info import driver_info

logger = setup_logger('STEP 1')
def step1_run() -> str:
    """
    执行第一步处理流程，根据部分驱动代码（包含宏定义、头文件、函数声明等），生成补充好头文件、宏定义的qemu设备代码，返回设备代码
    """
    logger.info('============================= STEP 1 RUN =============================')
    device_code, needed_sources = step1()   

    if device_code == "TERMINATE":
        return "TERMINATE"
    
    # If the returned needed_sources is not empty, run the step1 iterative completion
    if needed_sources != []:
        device_code = step1_iterations(device_code, needed_sources)
    
    if device_code == "TERMINATE":
        return "TERMINATE"
    
    logger.info('============================= STEP 1 RUN END =============================')

    # Save device_code
    save_device_code_path = f"{get_app_start_time()}_{get_driver_name()}/step1/device_code.txt"
    default_logger.save_content(device_code, save_device_code_path)

    return device_code

def step1() -> tuple[str, list]:
    """
    执行第一步处理流程，根据部分驱动代码（包含宏定义、头文件、函数声明等），生成补充好头文件、宏定义的qemu设备代码，返回设备代码和所需资源列表，执行一次
    """
    logger.info('============================= STEP 1 main =============================')

    # Get the step1 prompt
    step1_prompt = prompt_manager.get_prompt("step1", 
                                             DRIVER_PATH=driver_info.get_driver_path_by_driver_name(), 
                                             Relevant_Source=get_driver_code_simple(), 
                                             PCI_TEMPLATE=prompt_manager.get_prompt("pci_template", DRIVER_NAME=get_driver_name_replace())
                                            )

    # Build the history
    step1_history_messages = [
        {"role": "system", "content": prompt_manager.get_prompt("system")},
        {"role": "user", "content": prompt_manager.get_prompt("step0")},
        {"role": "assistant", "content": "OK"},
        {"role": "user", "content": prompt_manager.get_prompt("two_examples")},
        {"role": "assistant", "content": "OK"},
    ]

    # Build the step1 message
    step1_messages = step1_history_messages + [
        {"role": "user", "content": step1_prompt}
    ]

    # Save the step1 message
    save_step1_messages_path = f"{get_app_start_time()}_{get_driver_name()}/step1/messages"
    default_logger.save_messages(step1_messages, save_step1_messages_path)

    # Call the LLM
    response, finish_reason = client.chat_completion_json(step1_messages)

    if finish_reason == "length":
        logger.info("STEP 1 output length limit reached, terminate task")
        save_step1_terminate_path = f"{get_app_start_time()}_{get_driver_name()}/step1/terminate.txt"
        default_logger.save_content("STEP 1 output length limit reached, terminate task", save_step1_terminate_path)

        # Save the step1 reply content
        save_step1_response_path = f"{get_app_start_time()}_{get_driver_name()}/step1/response.txt"
        default_logger.save_content(response, save_step1_response_path)
        save_step1_response_path = f"{get_app_start_time()}_{get_driver_name()}/step1/response_visual.txt"
        default_logger.save_response(json.loads(response), save_step1_response_path)

        return "TERMINATE", []

    # Save the step1 reply content
    save_step1_response_path = f"{get_app_start_time()}_{get_driver_name()}/step1/response.txt"
    default_logger.save_content(response, save_step1_response_path)
    save_step1_response_path = f"{get_app_start_time()}_{get_driver_name()}/step1/response_visual.txt"
    default_logger.save_response(json.loads(response), save_step1_response_path)

    # Handle the JSON returned by the LLM
    response_json = json.loads(response)
    device_code = response_json["code"]
    needed_sources = response_json["needed_sources"]

    logger.info('============================= STEP 1 main END =============================')

    return device_code, needed_sources

def step1_iterations(device_code: str, needed_sources: list) -> str:
    """
    进行step1的多次迭代补全，直到needed_sources为空或者达到最大迭代次数
    """
    iteration = 0
    max_iterations = 3

    while needed_sources != [] and iteration < max_iterations: 
        logger.info(f'============================= STEP 1 iteration {iteration} =============================')

        is_searched, device_code, needed_sources = step1_iteration(iteration, device_code, needed_sources)

        if device_code == "TERMINATE":
            return "TERMINATE"
        # If the search can no longer continue, exit the iteration; the resource for needed_sources cannot be found
        if not is_searched:
            break

        iteration += 1

        logger.info(f'============================= STEP 1 iteration {iteration} END =============================')

    return device_code
        

def step1_iteration(cnt: int, device_code: str, needed_sources: list) -> tuple[bool, str, list]:
    """
    进行step1的一次迭代补全，返回补全后的设备代码和所需资源列表，如果无法补全则返回原始设备代码和所需资源列表，同时返回标志位（True表示可以完成搜索,False为反）
    """
    # Handle needed_sources
    relevant_source = handle_needed_resources(needed_sources)
    if relevant_source == "None":
        return False, device_code, needed_sources
    
    # Get the step1-iteration prompt
    step1_iteration_prompt = prompt_manager.get_prompt("step1_iteration", 
                                                       DRIVER_PATH=driver_info.get_driver_path_by_driver_name(), 
                                                       Relevant_Source=relevant_source, 
                                                       QEMU_CODE=device_code
                                                       )

    # Build the step1-iteration history
    step1_iteration_history_messages = [
        {"role": "system", "content": prompt_manager.get_prompt("system")},
        {"role": "user", "content": prompt_manager.get_prompt("step0")},
        {"role": "assistant", "content": "OK"},
        {"role": "user", "content": prompt_manager.get_prompt("two_examples")},
        {"role": "assistant", "content": "OK"},
    ]

    # Build the step1-iteration message
    step1_iteration_messages = step1_iteration_history_messages + [
        {"role": "user", "content": step1_iteration_prompt}
    ]

    # Save the step1-iteration message
    save_step1_iteration_messages_path = f"{get_app_start_time()}_{get_driver_name()}/step1/iteration/{cnt}/messages"
    default_logger.save_messages(step1_iteration_messages, save_step1_iteration_messages_path)

    # Call the LLM
    response, finish_reason = client.chat_completion_json(step1_iteration_messages)

    if finish_reason == "length" or finish_reason == "None":
        logger.info(f"STEP 1 iteration {cnt} output length limit reached, terminate task")
        save_step1_iteration_terminate_path = f"{get_app_start_time()}_{get_driver_name()}/step1/iteration/{cnt}/terminate.txt"
        default_logger.save_content(f"STEP 1 iteration {cnt} output length limit reached, terminate task", save_step1_iteration_terminate_path)

        # Save the step1-iteration reply content
        save_step1_iteration_response_path = f"{get_app_start_time()}_{get_driver_name()}/step1/iteration/{cnt}/response.txt"
        default_logger.save_content(response, save_step1_iteration_response_path)
        save_step1_iteration_response_path = f"{get_app_start_time()}_{get_driver_name()}/step1/iteration/{cnt}/response_visual.txt"
        default_logger.save_response(json.loads(response), save_step1_iteration_response_path)

        return False, "TERMINATE", []
    
    # Save the step1-iteration reply content
    save_step1_iteration_response_path = f"{get_app_start_time()}_{get_driver_name()}/step1/iteration/{cnt}/response.txt"
    default_logger.save_content(response, save_step1_iteration_response_path)
    save_step1_iteration_response_path = f"{get_app_start_time()}_{get_driver_name()}/step1/iteration/{cnt}/response_visual.txt"
    default_logger.save_response(json.loads(response), save_step1_iteration_response_path)

    # Handle the JSON returned by the LLM
    response_json = json.loads(response)
    device_code = response_json["code"]
    needed_sources = response_json["needed_sources"]
    
    return True, device_code, needed_sources

def get_driver_code_simple() -> str:
    """
    获取驱动代码，主要包括头文件、宏定义、函数声明、结构体等
    """
    return driver_info.get_macro_struct_info()

def handle_needed_resources(needed_sources: list) -> str:
    """
    处理所需资源
    """
    # Obtain the required resource by calling get_symbol_by_name in the driver_info module, return the detailed content of the required resource, or an empty string if there is none
    res = "None"
    cnt = 0
    suc_cnt = 0
    for source in needed_sources:
        cnt += 1
        
        if source.count(":") > 1:
            continue
        
        if ":" in source:
            type, name = source.split(":")
        else:
            name = source
            type = "all"

        logger.info(f'Try getting {name} from {type}')
        tmp = driver_info.get_symbol_by_name(name, type)
        
        if tmp is None:
            if type != "all":
                tmp = driver_info.get_symbol_by_name(name, "all")
            if tmp is None:
                logger.warning(f'Get {name} from {type} failed')
            else:
                if res == "None":
                    res = ""
                logger.info(f'Get {name} from all successfully')
                res += tmp + '\n'
                suc_cnt += 1
        else:
            if res == "None":
                res = ""
            logger.info(f'Get {name} from {type} successfully')
            res += tmp + '\n'
            suc_cnt += 1

    logger.info(f'Get needed_source successfully. Total: {cnt}, Success: {suc_cnt}')

    return res