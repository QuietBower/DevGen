import json
import asyncio
from vdm.utils.logger import setup_logger
from vdm.utils.save_logs import default_logger
from vdm.core.llm_client import client
from vdm.core.prompt_manager import prompt_manager
from vdm.core.context import get_app_start_time, get_driver_name
from vdm.core.driver_info import driver_info
from vdm.services.config_loader import ConfigLoader
from vdm.utils.file_op import write_to_file
from vdm.services.clangd_checker import check_c_syntax

logger = setup_logger('modify_after_build_fail')

config_loader = ConfigLoader()
config = config_loader.load()

def get_syntax_error_messages():
    """
    检查生成的PCI驱动文件的C语法错误和警告，并返回格式化的错误信息。
    
    该函数会构建一个PCI驱动源文件路径，检查其语法错误和警告，
    并将结果格式化为字符串返回。
    
    Returns:
        str: 包含语法错误、警告或无错误信息的格式化字符串
    """
    generated_file = f"{config['paths']['qemu_hw_fack_pci_path']}/{get_driver_name()}_pci.c"
    
    result = check_c_syntax(
        source_file=generated_file,
        build_dir=config['paths']['qemu_build_static_path'],
    )

    flag = 0
    res = ""
    # Handle syntax error information
    if result.has_errors():
        flag += 1
        res += "**Syntax errors found:**\n"
        for error in result.errors:
            res += f"===========================================================\nLine {error.range.start.line + 1}: {error.message}\n snippet:{error.snippet}"
    
    # Handle syntax warning information
    if result.has_warnings():
        flag += 1
        res += "**Syntax warnings found:**\n"
        for warning in result.warnings:
            res += f"===========================================================\nLine {warning.range.start.line + 1}: {warning.message}\nsnippet:\n{warning.snippet}"

    # Handling when there are no errors or warnings
    if flag == 0:
        res += "No syntax errors or warnings found."

    return res

def modify_after_build_fail_run(device_code: str, fail_logs: str = None) -> str:
    """
    执行第三步处理流程，根据部分驱动代码（包含函数定义、struct-init定义等），生成补充好函数实现的qemu设备代码，返回设备代码
    """
    logger.info('============================= MODIFY_AFTER_BUILD_FAIL RUN =============================')
    device_code, needed_sources = modify_after_build_fail(device_code, fail_logs)   

    # If the returned needed_sources is not empty, run the modify_after_build_fail iterative completion
    device_code = modify_after_build_fail_iterations(device_code, needed_sources, fail_logs)
    
    logger.info('============================= MODIFY_AFTER_BUILD_FAIL RUN END =============================')

    # Save device_code
    save_device_code_path = f"{get_app_start_time()}_{get_driver_name()}/modify_after_build_fail/device_code.txt"
    default_logger.save_content(device_code, save_device_code_path)


    return device_code

def modify_after_build_fail(device_code: str, fail_logs: str = None) -> tuple[str, list]:
    """
    执行第三步处理流程
    """
    logger.info('============================= MODIFY_AFTER_BUILD_FAIL main =============================')

    
    # Copy (or overwrite) the generated QEMU device code into hw/fake_pci
    write_to_file(f"{config['paths']['qemu_hw_fack_pci_path']}/{get_driver_name()}_pci.c", device_code)
    logger.info(f"代码，成功保存至{config['paths']['qemu_hw_fack_pci_path']}/{get_driver_name()}_pci.c")

    logger.info("开始检查代码语法错误")
    syntax_error_message = get_syntax_error_messages()
    logger.info(f"语法错误检查结束：{syntax_error_message}")

    # On the first run of modify_after_build_fail, regardless of whether there are syntax errors, the presence of fail_logs means there are build errors that need fixing.

    # Get the modify_after_build_fail prompt
    modify_after_build_fail_prompt = prompt_manager.get_prompt("modify_after_build_fail", 
                                             DRIVER_PATH=driver_info.get_driver_path_by_driver_name(), 
                                             QEMU_CODE=device_code,
                                             SYNTAX_ERROR_MESSAGE=syntax_error_message,
                                             Relevant_Source="",
                                             FAIL_LOGS=fail_logs
                                            )

    # Build the history
    modify_after_build_fail_history_messages = [
        {"role": "system", "content": prompt_manager.get_prompt("system")},
        {"role": "user", "content": prompt_manager.get_prompt("step0")},
        {"role": "assistant", "content": "OK"},
        {"role": "user", "content": prompt_manager.get_prompt("two_examples")},
        {"role": "assistant", "content": "OK"},
    ]

    # Build the modify_after_build_fail message
    modify_after_build_fail_messages = modify_after_build_fail_history_messages + [
        {"role": "user", "content": modify_after_build_fail_prompt}
    ]

    # Save the modify_after_build_fail message
    save_modify_after_build_fail_messages_path = f"{get_app_start_time()}_{get_driver_name()}/modify_after_build_fail/messages"
    default_logger.save_messages(modify_after_build_fail_messages, save_modify_after_build_fail_messages_path)


    # Call the LLM
    response, finish_reason = client.chat_completion_json(modify_after_build_fail_messages)

    # Handle the output-limit case with multiple replies
    continue_cnt = 1
    while finish_reason == "length" or finish_reason == "None":
        if continue_cnt >= 5:
            break
        # Build the message to continue fetching the full output
        modify_after_build_fail_continue_messages = modify_after_build_fail_messages + [
            {"role": "assistant", "content": response}, 
            {"role": "user", "content": prompt_manager.get_prompt("full_output")},
        ]

        # Save the modify_after_build_fail_continue message
        save_modify_after_build_fail_continue_messages_path = f"{get_app_start_time()}_{get_driver_name()}/modify_after_build_fail/continue/{continue_cnt}/messages"
        default_logger.save_messages(modify_after_build_fail_continue_messages, save_modify_after_build_fail_continue_messages_path)

        # Call the LLM
        response_continue, finish_reason = client.chat_completion_json(modify_after_build_fail_continue_messages)

        # Save the reply content
        save_modify_after_build_fail_continue_response_path = f"{get_app_start_time()}_{get_driver_name()}/modify_after_build_fail/continue/{continue_cnt}/response.txt"
        default_logger.save_content(response_continue, save_modify_after_build_fail_continue_response_path)

        # Handle the output-limit case with multiple replies, concatenating them
        response += response_continue

        continue_cnt += 1

    # Save the modify_after_build_fail reply content
    save_modify_after_build_fail_response_path = f"{get_app_start_time()}_{get_driver_name()}/modify_after_build_fail/response.txt"
    default_logger.save_content(response, save_modify_after_build_fail_response_path)
    save_modify_after_build_fail_response_path = f"{get_app_start_time()}_{get_driver_name()}/modify_after_build_fail/response_visual.txt"
    default_logger.save_response(json.loads(response), save_modify_after_build_fail_response_path)

    # Handle the JSON returned by the LLM
    response_json = json.loads(response)
    device_code = response_json["code"]
    needed_sources = response_json["needed_sources"]

    logger.info('============================= MODIFY_AFTER_BUILD_FAIL main END =============================')

    return device_code, needed_sources

def modify_after_build_fail_iterations(device_code: str, needed_sources: list, fail_logs: str = None) -> str:
    """
    进行modify_after_build_fail的多次迭代补全，直到needed_sources为空或者达到最大迭代次数
    """
    iteration = 0
    # Maximum number of iterations
    max_iterations = 5

    while iteration < max_iterations: 
        logger.info(f'============================= MODIFY_AFTER_BUILD_FAIL iteration {iteration} =============================')

        is_searched, device_code, needed_sources = modify_after_build_fail_iteration(iteration, device_code, needed_sources, fail_logs)

        # If the search can no longer continue, exit the iteration; the resource for needed_sources cannot be found
        if not is_searched:
            break

        iteration += 1

        logger.info(f'============================= MODIFY_AFTER_BUILD_FAIL iteration {iteration} END =============================')

    return device_code
        

def modify_after_build_fail_iteration(cnt: int, device_code: str, needed_sources: list, fail_logs: str = None) -> tuple[bool, str, list]:
    """
    进行modify_after_build_fail的一次迭代补全，返回补全后的设备代码和所需资源列表，如果无法补全则返回原始设备代码和所需资源列表，同时返回标志位（True表示可以完成搜索,False为反）
    """
    # Handle needed_sources
    relevant_source = handle_needed_resources(needed_sources)
    
    # Copy (or overwrite) the generated QEMU device code into hw/fake_pci
    write_to_file(f"{config['paths']['qemu_hw_fack_pci_path']}/{get_driver_name()}_pci.c", device_code)
    logger.info(f"代码，成功保存至{config['paths']['qemu_hw_fack_pci_path']}/{get_driver_name()}_pci.c")

    logger.info("开始检查代码语法错误")
    syntax_error_message = get_syntax_error_messages()
    logger.info(f"语法错误检查结束：{syntax_error_message}")

    if syntax_error_message == "No syntax errors or warnings found." and relevant_source == "None":
        if syntax_error_message == "No syntax errors or warnings found.":
            logger.info("无语法错误或warnning，不进行modify_after_build_fail的迭代")
        
        if needed_sources == []:
            logger.info("无所需资源，不进行modify_after_build_fail的迭代")

        return False, device_code, []

    # Get the modify_after_build_fail-iteration prompt
    modify_after_build_fail_iteration_prompt = prompt_manager.get_prompt("modify_after_build_fail_iteration", 
                                                       DRIVER_PATH=driver_info.get_driver_path_by_driver_name(), 
                                                       Relevant_Source=relevant_source, 
                                                       QEMU_CODE=device_code,
                                                       SYNTAX_ERROR_MESSAGE=syntax_error_message,
                                                       FAIL_LOGS=fail_logs
                                                      )

    # Build the modify_after_build_fail-iteration history
    modify_after_build_fail_iteration_history_messages = [
        {"role": "system", "content": prompt_manager.get_prompt("system")},
        {"role": "user", "content": prompt_manager.get_prompt("step0")},
        {"role": "assistant", "content": "OK"},
        {"role": "user", "content": prompt_manager.get_prompt("two_examples")},
        {"role": "assistant", "content": "OK"},
    ]

    # Build the modify_after_build_fail-iteration message
    modify_after_build_fail_iteration_messages = modify_after_build_fail_iteration_history_messages + [
        {"role": "user", "content": modify_after_build_fail_iteration_prompt}
    ]

    # Save the modify_after_build_fail-iteration message
    save_modify_after_build_fail_iteration_messages_path = f"{get_app_start_time()}_{get_driver_name()}/modify_after_build_fail/iteration/{cnt}/messages"
    default_logger.save_messages(modify_after_build_fail_iteration_messages, save_modify_after_build_fail_iteration_messages_path)

    # Call the LLM
    response, finish_reason = client.chat_completion_json(modify_after_build_fail_iteration_messages)

    # Handle the output-limit case with multiple replies
    continue_cnt = 1
    while finish_reason == "length" or finish_reason == "None":
        if continue_cnt >= 5:
            break
        # Build the message to continue fetching the full output
        modify_after_build_fail_iteration_continue_messages = modify_after_build_fail_iteration_messages + [
            {"role": "assistant", "content": response}, 
            {"role": "user", "content": prompt_manager.get_prompt("full_output")},
        ]

        # Save the modify_after_build_fail-iteration_continue message
        save_modify_after_build_fail_iteration_continue_messages_path = f"{get_app_start_time()}_{get_driver_name()}/modify_after_build_fail/iteration/{cnt}/continue/{continue_cnt}/messages"
        default_logger.save_messages(modify_after_build_fail_iteration_continue_messages, save_modify_after_build_fail_iteration_continue_messages_path)

        # Call the LLM
        response_continue, finish_reason = client.chat_completion_json(modify_after_build_fail_iteration_continue_messages)

        # Save the reply content
        save_modify_after_build_fail_iteration_continue_response_path = f"{get_app_start_time()}_{get_driver_name()}/modify_after_build_fail/iteration/{cnt}/continue/{continue_cnt}/response.txt"
        default_logger.save_content(response_continue, save_modify_after_build_fail_iteration_continue_response_path)

        # Handle the output-limit case with multiple replies, concatenating them
        response += response_continue

        continue_cnt += 1
    
    # Save the modify_after_build_fail-iteration reply content
    save_modify_after_build_fail_iteration_response_path = f"{get_app_start_time()}_{get_driver_name()}/modify_after_build_fail/iteration/{cnt}/response.txt"
    default_logger.save_response(json.loads(response), save_modify_after_build_fail_iteration_response_path)

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