import json
from vdm.utils.logger import setup_logger
from vdm.utils.save_logs import default_logger
from vdm.core.llm_client import client
from vdm.core.prompt_manager import prompt_manager
from vdm.core.context import get_app_start_time, get_driver_name
from vdm.core.driver_info import driver_info
from vdm.core.examples_loader import exampleloader
from vdm.step.step3 import step3_run

logger = setup_logger('STEP 4')

def step4_run(device_code: str, err_msg: str, cnt: int) -> str:
    """
    执行第四步处理流程，返回修复完成后的设备代码。step3的QEMU代码、err_msg.txt的内容、probe和pci_id结构体作为第一步的相关信息，遍历Author B统计的所有修复结果信息，找两个例子传入分析。
    """
    logger.info('============================= STEP 4 RUN =============================')
    device_code, needed_sources = step4(device_code, err_msg, cnt)   
    device_code = step3_run(device_code, f"{get_app_start_time()}_{get_driver_name()}/step4/{cnt}")

    # If the returned needed_sources is not empty, run the step4 iterative completion
    if needed_sources != []:
        device_code = step4_iterations(device_code, needed_sources, err_msg, cnt)
    
    logger.info('============================= STEP 4 RUN END =============================')

    # Save device_code
    save_device_code_path = f"{get_app_start_time()}_{get_driver_name()}/step4/{cnt}/device_code.txt"
    default_logger.save_content(device_code, save_device_code_path)

    return device_code

def step4(device_code: str, err_msg: str, cnt: int) -> tuple[str, list]:
    """
    执行第四步处理流程
    """
    logger.info('============================= STEP 4 main =============================')

    # Search the related resources according to needed_sources and obtain the required resource
    relevant_source = ""
    two_examples = exampleloader.get_examples_by_err_msg(err_msg)

    # Get the step4 prompt
    step4_prompt = prompt_manager.get_prompt("step4", 
                                             DRIVER_PATH=driver_info.get_driver_path_by_driver_name(), 
                                             QEMU_CODE=device_code,
                                             ERR_MSG=err_msg,
                                             Relevant_Source=relevant_source,
                                             TWO_EXAM=two_examples,
                                            )

    # Build the history
    step4_history_messages = [
        {"role": "system", "content": prompt_manager.get_prompt("system")},
        {"role": "user", "content": prompt_manager.get_prompt("step0")},
        {"role": "assistant", "content": "OK"},
    ]

    # Build the step4 message
    step4_messages = step4_history_messages + [
        {"role": "user", "content": step4_prompt}
    ]

    # Save the step4 message
    save_step4_messages_path = f"{get_app_start_time()}_{get_driver_name()}/step4/{cnt}/messages"
    default_logger.save_messages(step4_messages, save_step4_messages_path)

    # Call the LLM
    response, finish_reason = client.chat_completion_json(step4_messages)

    # Handle the output-limit case with multiple replies
    continue_cnt = 1
    while finish_reason == "length":
        if continue_cnt >= 5:
            break
        # Build the message to continue fetching the full output
        step4_continue_messages = step4_messages + [
            {"role": "assistant", "content": response}, 
            {"role": "user", "content": prompt_manager.get_prompt("full_output")},
        ]

        # Save the step4_continue message
        save_step4_continue_messages_path = f"{get_app_start_time()}_{get_driver_name()}/step4/{cnt}/continue/{continue_cnt}/messages"
        default_logger.save_messages(step4_continue_messages, save_step4_continue_messages_path)

        # Call the LLM
        response_continue, finish_reason = client.chat_completion_json(step4_continue_messages)

        # Save the reply content
        save_step4_continue_response_path = f"{get_app_start_time()}_{get_driver_name()}/step4/{cnt}/continue/{continue_cnt}/response.txt"
        default_logger.save_content(response_continue, save_step4_continue_response_path)

        # Handle the output-limit case with multiple replies, concatenating them
        response += response_continue

        continue_cnt += 1

    # Save the step4 reply content
    save_step4_response_path = f"{get_app_start_time()}_{get_driver_name()}/step4/{cnt}/response.txt"
    default_logger.save_content(response, save_step4_response_path)
    save_step4_response_path = f"{get_app_start_time()}_{get_driver_name()}/step4/{cnt}/response_visual.txt"
    default_logger.save_response(json.loads(response), save_step4_response_path)

    # Handle the JSON returned by the LLM
    response_json = json.loads(response)
    device_code = response_json["code"]
    needed_sources = response_json["needed_sources"]

    logger.info('============================= STEP 4 main END =============================')

    return device_code, needed_sources

def step4_iterations(device_code: str, needed_sources: list, err_msg: str, cnt: int) -> str:
    """
    进行step4的多次迭代补全，直到needed_sources为空或者达到最大迭代次数
    """
    iteration = 0
    max_iterations = 3

    while needed_sources != [] and iteration < max_iterations: 
        logger.info(f'============================= STEP 4 iteration {iteration} =============================')

        is_searched, device_code, needed_sources = step4_iteration(iteration, device_code, needed_sources, err_msg, cnt)

        device_code = step3_run(device_code, f"{get_app_start_time()}_{get_driver_name()}/step4/{cnt}/iteration/{iteration}")

        # If the search can no longer continue, exit the iteration; the resource for needed_sources cannot be found
        if not is_searched:
            break

        iteration += 1

        logger.info(f'============================= STEP 4 iteration {iteration} END =============================')

    return device_code
        

def step4_iteration(cnt: int, device_code: str, needed_sources: list, err_msg: str, retry_cnt: int) -> tuple[bool, str, list]:
    """
    进行step4的一次迭代补全，返回补全后的设备代码和所需资源列表，如果无法补全则返回原始设备代码和所需资源列表，同时返回标志位（True表示可以完成搜索,False为反）
    """
    # Handle needed_sources
    relevant_source = handle_needed_resources(needed_sources)
    if relevant_source == "None":
        return False, device_code, needed_sources
    
    two_examples = exampleloader.get_examples_by_err_msg(err_msg)

    # Get the step4 prompt
    step4_iteration_prompt = prompt_manager.get_prompt("step4", 
                                             DRIVER_PATH=driver_info.get_driver_path_by_driver_name(), 
                                             QEMU_CODE=device_code,
                                             ERR_MSG=err_msg,
                                             Relevant_Source=relevant_source,
                                             TWO_EXAM=two_examples,
                                            )

    # Build the history
    step4_iteration_history_messages = [
        {"role": "system", "content": prompt_manager.get_prompt("system")},
        {"role": "user", "content": prompt_manager.get_prompt("step0")},
        {"role": "assistant", "content": "OK"},
    ]

    # Build the step4-iteration message
    step4_iteration_messages = step4_iteration_history_messages + [
        {"role": "user", "content": step4_iteration_prompt}
    ]

    # Save the step4-iteration message
    save_step4_iteration_messages_path = f"{get_app_start_time()}_{get_driver_name()}/step4/{retry_cnt}/iteration/{cnt}/messages"
    default_logger.save_messages(step4_iteration_messages, save_step4_iteration_messages_path)

    # Call the LLM
    response, finish_reason = client.chat_completion_json(step4_iteration_messages)

    # Handle the output-limit case with multiple replies
    continue_cnt = 1
    while finish_reason == "length":
        if continue_cnt >= 5:
            break
        # Build the message to continue fetching the full output
        step4_iteration_continue_messages = step4_iteration_messages + [
            {"role": "assistant", "content": response}, 
            {"role": "user", "content": prompt_manager.get_prompt("full_output")},
        ]

        # Save the step4-iteration_continue message
        save_step4_iteration_continue_messages_path = f"{get_app_start_time()}_{get_driver_name()}/step4/{retry_cnt}/iteration/{cnt}/continue/{continue_cnt}/messages"
        default_logger.save_messages(step4_iteration_continue_messages, save_step4_iteration_continue_messages_path)

        # Call the LLM
        response_continue, finish_reason = client.chat_completion_json(step4_iteration_continue_messages)

        # Save the reply content
        save_step4_iteration_continue_response_path = f"{get_app_start_time()}_{get_driver_name()}/step4/{retry_cnt}/iteration/{cnt}/continue/{continue_cnt}/response.txt"
        default_logger.save_content(response_continue, save_step4_iteration_continue_response_path)
            
        # Handle the output-limit case with multiple replies, concatenating them
        response += response_continue

        continue_cnt += 1
    
    # Save the step4-iteration reply content
    save_step4_iteration_response_path = f"{get_app_start_time()}_{get_driver_name()}/step4/{retry_cnt}/iteration/{cnt}/response.txt"
    default_logger.save_response(json.loads(response), save_step4_iteration_response_path)

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