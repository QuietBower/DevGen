import json
from vdm.utils.logger import setup_logger
from vdm.utils.save_logs import default_logger
from vdm.core.llm_client import client
from vdm.core.prompt_manager import prompt_manager
from vdm.core.context import get_app_start_time, get_driver_name
from vdm.core.driver_info import driver_info

logger = setup_logger('STEP 2')

def step2_run(device_code: str) -> str:
    """
    执行第二步处理流程，根据部分驱动代码（包含函数定义、struct-init定义等），生成补充好函数实现的qemu设备代码，返回设备代码
    """
    logger.info('============================= STEP 2 RUN =============================')
    device_code, needed_sources = step2(device_code)   

    # If the returned needed_sources is not empty, run the step2 iterative completion
    if needed_sources != []:
        device_code = step2_iterations(device_code, needed_sources)
    
    logger.info('============================= STEP 2 RUN END =============================')

    # Save device_code
    save_device_code_path = f"{get_app_start_time()}_{get_driver_name()}/step2/device_code.txt"
    default_logger.save_content(device_code, save_device_code_path)

    return device_code

def step2(device_code: str) -> tuple[str, list]:
    """
    执行第二步处理流程，根据部分驱动代码（包含函数定义、struct-init定义等），生成补充好函数实现的qemu设备代码，返回设备代码，执行一次
    """
    logger.info('============================= STEP 2 main =============================')

    # Get the step2 prompt
    step2_prompt = prompt_manager.get_prompt("step2", 
                                             DRIVER_PATH=driver_info.get_driver_path_by_driver_name(), 
                                             Relevant_Source=get_driver_code_detail(), 
                                             QEMU_CODE=device_code
                                            )

    # Build the history
    step2_history_messages = [
        {"role": "system", "content": prompt_manager.get_prompt("system")},
        {"role": "user", "content": prompt_manager.get_prompt("step0")},
        {"role": "assistant", "content": "OK"},
        {"role": "user", "content": prompt_manager.get_prompt("two_examples")},
        {"role": "assistant", "content": "OK"},
    ]

    # Build the step2 message
    step2_messages = step2_history_messages + [
        {"role": "user", "content": step2_prompt}
    ]

    # Save the step2 message
    save_step2_messages_path = f"{get_app_start_time()}_{get_driver_name()}/step2/messages"
    default_logger.save_messages(step2_messages, save_step2_messages_path)

    # Call the LLM
    response, finish_reason = client.chat_completion_json(step2_messages)

    # Handle the output-limit case with multiple replies
    continue_cnt = 1
    while finish_reason == "length":
        if continue_cnt >= 5:
            break
        # Build the message to continue fetching the full output
        step2_continue_messages = step2_messages + [
            {"role": "assistant", "content": response}, 
            {"role": "user", "content": prompt_manager.get_prompt("full_output")},
        ]

        # Save the step2_continue message
        save_step2_continue_messages_path = f"{get_app_start_time()}_{get_driver_name()}/step2/continue/{continue_cnt}/messages"
        default_logger.save_messages(step2_continue_messages, save_step2_continue_messages_path)

        # Call the LLM
        response_continue, finish_reason = client.chat_completion_json(step2_continue_messages)

        # Save the reply content
        save_step2_continue_response_path = f"{get_app_start_time()}_{get_driver_name()}/step2/continue/{continue_cnt}/response.txt"
        default_logger.save_content(response_continue, save_step2_continue_response_path)

        # Handle the output-limit case with multiple replies, concatenating them
        response += response_continue

        continue_cnt += 1

    # Save the step2 reply content
    save_step2_response_path = f"{get_app_start_time()}_{get_driver_name()}/step2/response.txt"
    default_logger.save_content(response, save_step2_response_path)
    save_step2_response_path = f"{get_app_start_time()}_{get_driver_name()}/step2/response_visual.txt"
    default_logger.save_response(json.loads(response), save_step2_response_path)

    # Handle the JSON returned by the LLM
    response_json = json.loads(response)
    device_code = response_json["code"]
    needed_sources = response_json["needed_sources"]

    logger.info('============================= STEP 2 main END =============================')

    return device_code, needed_sources

def step2_iterations(device_code: str, needed_sources: list) -> str:
    """
    进行step2的多次迭代补全，直到needed_sources为空或者达到最大迭代次数
    """
    iteration = 0
    max_iterations = 3

    while needed_sources != [] and iteration < max_iterations: 
        logger.info(f'============================= STEP 2 iteration {iteration} =============================')

        is_searched, device_code, needed_sources = step2_iteration(iteration, device_code, needed_sources)

        # If the search can no longer continue, exit the iteration; the resource for needed_sources cannot be found
        if not is_searched:
            break

        iteration += 1

        logger.info(f'============================= STEP 2 iteration {iteration} END =============================')

    return device_code
        

def step2_iteration(cnt: int, device_code: str, needed_sources: list) -> tuple[bool, str, list]:
    """
    进行step2的一次迭代补全，返回补全后的设备代码和所需资源列表，如果无法补全则返回原始设备代码和所需资源列表，同时返回标志位（True表示可以完成搜索,False为反）
    """
    # Handle needed_sources
    relevant_source = handle_needed_resources(needed_sources)
    if relevant_source == "None":
        return False, device_code, needed_sources
    
    # Get the step2-iteration prompt
    step2_iteration_prompt = prompt_manager.get_prompt("step2_iteration", 
                                                       DRIVER_PATH=driver_info.get_driver_path_by_driver_name(), 
                                                       Relevant_Source=relevant_source, 
                                                       QEMU_CODE=device_code
                                                      )

    # Build the step2-iteration history
    step2_iteration_history_messages = [
        {"role": "system", "content": prompt_manager.get_prompt("system")},
        {"role": "user", "content": prompt_manager.get_prompt("step0")},
        {"role": "assistant", "content": "OK"},
        {"role": "user", "content": prompt_manager.get_prompt("two_examples")},
        {"role": "assistant", "content": "OK"},
    ]
     
    # Build the step2-iteration message
    step2_iteration_messages = step2_iteration_history_messages + [
        {"role": "user", "content": step2_iteration_prompt}
    ]

    # Save the step2-iteration message
    save_step2_iteration_messages_path = f"{get_app_start_time()}_{get_driver_name()}/step2/iteration/{cnt}/messages"
    default_logger.save_messages(step2_iteration_messages, save_step2_iteration_messages_path)

    # Call the LLM
    response, finish_reason = client.chat_completion_json(step2_iteration_messages)

    # Handle the output-limit case with multiple replies
    continue_cnt = 1
    while finish_reason == "length" or finish_reason == "None":
        if continue_cnt >= 3:
            break
        # Build the message to continue fetching the full output
        step2_iteration_continue_messages = step2_iteration_messages + [
            {"role": "assistant", "content": response}, 
            {"role": "user", "content": prompt_manager.get_prompt("full_output")},
        ]

        # Save the step2-iteration_continue message
        save_step2_iteration_continue_messages_path = f"{get_app_start_time()}_{get_driver_name()}/step2/iteration/{cnt}/continue/{continue_cnt}/messages"
        default_logger.save_messages(step2_iteration_continue_messages, save_step2_iteration_continue_messages_path)

        # Call the LLM
        response_continue, finish_reason = client.chat_completion_json(step2_iteration_continue_messages)

        # Save the reply content
        save_step2_iteration_continue_response_path = f"{get_app_start_time()}_{get_driver_name()}/step2/iteration/{cnt}/continue/{continue_cnt}/response.txt"
        default_logger.save_content(response_continue, save_step2_iteration_continue_response_path)
            
        # Handle the output-limit case with multiple replies, concatenating them
        response += response_continue

        continue_cnt += 1
    
    # Save the step2-iteration reply content
    save_step2_iteration_response_path = f"{get_app_start_time()}_{get_driver_name()}/step2/iteration/{cnt}/response.txt"
    default_logger.save_response(json.loads(response), save_step2_iteration_response_path)
    
    # Handle the JSON returned by the LLM
    response_json = json.loads(response)
    device_code = response_json["code"]
    needed_sources = response_json["needed_sources"]

    return True, device_code, needed_sources

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
                logger.info(f'Get {name} from {type} failed')
            else:
                logger.info(f'Get {name} from all successfully')
                if res == "None":
                    res = ""
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

def get_driver_code_detail() -> str:
    """
    获取驱动代码，完整的驱动代码
    """
    return driver_info.get_function_implementations()

    