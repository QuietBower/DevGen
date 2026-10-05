from vdm.utils.logger import setup_logger
from vdm.step.step1 import step1_run
from vdm.step.step2 import step2_run
from vdm.step.step3 import step3_run
from vdm.step.step4 import step4_run
from vdm.step.modify_after_build_fail import modify_after_build_fail_run
from vdm.core.context import initialize_app_start_time, set_driver_name, get_driver_name_replace, get_app_start_time, get_driver_name
from vdm.step.modify_compile_run import modify_compile_run
from vdm.utils.file_op import append_to_file, remove_content_from_file, move_all_files, read_file
from vdm.services.config_loader import ConfigLoader
from vdm.utils.get_capital_driver_name import get_capital_driver_name
from vdm.utils.fill_qemu_run_template import fill_qemu_run_template
from pathlib import Path
from vdm.services.config_loader import ConfigLoader
import os
import re

logger = setup_logger('main')

config_loader = ConfigLoader()
config = config_loader.load()

def model_device(driver_name):
    config_loader = ConfigLoader()
    config = config_loader.load()

    device_code = ""

    # 设置运行开始时间，设置驱动路径和名称
    initialize_app_start_time()
    set_driver_name(driver_name)

    device_code = step1_run()

    if device_code == "TERMINATE":
        logger.info("STEP1 出现continue情况，停止设备建模")
        return 
    
    device_code = step2_run(device_code)
    
    device_code = step3_run(device_code)
    
    # with open("/home/user/vdm/adm8211_pci.c", "r") as f:
    #     device_code = f.read()

    # 在hw/fake_pci目录下的meson.build中换行并追加内容
    meson_contentd = f"system_ss.add(when: 'CONFIG_{get_capital_driver_name(get_driver_name_replace())}', if_true: files('{get_driver_name_replace()}_pci.c'))\n"
    append_to_file(f"{config['paths']['qemu_hw_fack_pci_path']}/meson.build", meson_contentd)
    logger.info(f"成功修改hw/fake_pci目录下的meson.build文件")

    # 复制start_qemu_template.sh，将start_qemu_template.sh内容中的`DRIVER_NAME`进行替换，替换为现在正在处理的程序名，并保存至run.sh中
    fill_qemu_run_template(get_driver_name_replace())
    logger.info(f"成功生成run.sh文件")

    # 用于测试，获得某个驱动的代码，在不运行step1-3的情况下，测试step4的效果
    # with open("/home/user/vdm/adm8211_pci.c", "r") as f:
    #     device_code = f.read()

    # 执行编译、错误修改、step4
    is_have_kernel_in_use_after_fix = False
    modify_compile_run_retry_cnt = 1

    while modify_compile_run_retry_cnt <= 2:
        logger.info(f"开始执行modify_compile_run, 第{modify_compile_run_retry_cnt}次")
        # 将新生成的代码，保存至hw/fake_pci下，执行build编译和run启动qemu，通过ssh连接到qemu，获得load.txt、dev.txt、err_msg.txt等信息
        is_success_run, is_have_kernel_in_use = modify_compile_run(get_driver_name_replace(), device_code)

        if is_success_run == 1 and modify_compile_run_retry_cnt == 1:
            # 启动失败，也调用step4进行修复，进行step3进行语法检验，然后编译启动，该流程只执行1次
            try:
                err_msg = read_file(f"{config['paths']['qemu_out_share_path']}/vm.log")
                logger.info("qemu编译成功，启动失败，开始执行step4进行修复")
                device_code = step4_run(device_code, err_msg, 0)

                is_success_run, is_have_kernel_in_use = modify_compile_run(get_driver_name_replace(), device_code)
            except Exception as e:
                logger.error(e)
                remove_content_from_file(f"{config['paths']['qemu_hw_fack_pci_path']}/meson.build", meson_contentd)

                directory = Path(f"{config['paths']['qemu_out_shares_path']}/err/{driver_name}")
                directory.mkdir(parents=True, exist_ok=True)

                move_all_files(f"{config['paths']['qemu_out_share_path']}/", directory)

                logger.info("qemu启动失败，进行step4修复时出错")
                break
        if is_success_run == 2 and modify_compile_run_retry_cnt == 1:
            # 修改编译失败，把编译失败信息提供给大模型进行修改，使用step3的prompt进行修复
            # try:
            fail_logs_path = f"{config['paths']['qemu_out_share_path']}/build_fail.txt"
            exists = os.path.isfile(fail_logs_path)
            if exists:
                try:
                    fail_logs = read_file(fail_logs_path)
                    
                    block_re = re.compile(r'^FAILED:.*?(?=^\[\d+/\d+\])', re.MULTILINE | re.DOTALL)
                    match = block_re.search(fail_logs)
                    if match:
                        fail_logs = match.group(0)

                    logger.info("qemu编译失败，开始执行modify_after_build_fail进行修复")
                    device_code = modify_after_build_fail_run(device_code, fail_logs)
                    logger.info(device_code)
                    is_success_run, is_have_kernel_in_use = modify_compile_run(get_driver_name_replace(), device_code)

                    if is_success_run == 1:
                        try:
                            err_msg = read_file(f"{config['paths']['qemu_out_share_path']}/vm.log")
                            logger.info("qemu编译、启动成功，没有出现Kernel driver in use，开始执行step4进行修复")
                            device_code = step4_run(device_code, err_msg, 0)

                            is_success_run, is_have_kernel_in_use = modify_compile_run(get_driver_name_replace(), device_code)
                        except Exception as e:
                            logger.error(e)
                            remove_content_from_file(f"{config['paths']['qemu_hw_fack_pci_path']}/meson.build", meson_contentd)

                            directory = Path(f"{config['paths']['qemu_out_shares_path']}/err/{driver_name}")
                            directory.mkdir(parents=True, exist_ok=True)

                            move_all_files(f"{config['paths']['qemu_out_share_path']}/", directory)

                            logger.info("qemu启动失败，进行step4修复时出错")
                            break
                except Exception as e:
                    logger.info(f"modify_after_build_fail出错")
                    logger.error(e)

        if is_success_run == 0:
            if is_have_kernel_in_use:
                # 运行成功，保存当前信息，即保存share文件夹的内容到shares中以驱动名命名的文件夹的success文件夹中
                is_have_kernel_in_use_after_fix = True
                
                directory = Path(f"{config['paths']['qemu_out_shares_path']}/success/{driver_name}")
                directory.mkdir(parents=True, exist_ok=True)
                move_all_files(f"{config['paths']['qemu_out_share_path']}/", directory)
                
                logger.info("qemu编译、启动成功，成功出现Kernel driver in use，建模结束")

                break
            else:
                # 1. 没有出现kernal driver in use，执行step4进行修复
                # 读取out/share下的err_msg.txt
                try:
                    err_msg = read_file(f"{config['paths']['qemu_out_share_path']}/err_msg.txt")
                    logger.info("qemu编译、启动成功，没有出现Kernel driver in use，开始执行step4进行修复")
                    device_code = step4_run(device_code, err_msg, modify_compile_run_retry_cnt)
                except Exception as e:
                    logger.error(e)
                    remove_content_from_file(f"{config['paths']['qemu_hw_fack_pci_path']}/meson.build", meson_contentd)

                    directory = Path(f"{config['paths']['qemu_out_shares_path']}/fail/{driver_name}")
                    directory.mkdir(parents=True, exist_ok=True)

                    move_all_files(f"{config['paths']['qemu_out_share_path']}/", directory)

                    break
        else:
            # 停止运行，保存当前的信息，即保存share文件夹的内容到shares中以驱动名命名的文件夹的err文件夹中，同时删除追加的meson.build内容
            directory = Path(f"{config['paths']['qemu_out_shares_path']}/err/{driver_name}")
            directory.mkdir(parents=True, exist_ok=True)
            move_all_files(f"{config['paths']['qemu_out_share_path']}/", directory)

            remove_content_from_file(f"{config['paths']['qemu_hw_fack_pci_path']}/meson.build", meson_contentd)

            logger.info("qemu编译、启动失败，停止对该设备的建模，转存share文件夹下的信息")

            return 
            
        modify_compile_run_retry_cnt += 1
 
    if is_have_kernel_in_use_after_fix == False:
        # 修复失败，即保存share文件夹的内容到shares中以驱动名命名的文件夹的fail文件夹中，同时删除追加的meson.build内容
        remove_content_from_file(f"{config['paths']['qemu_hw_fack_pci_path']}/meson.build", meson_contentd)
        directory = Path(f"{config['paths']['qemu_out_shares_path']}/fail/{driver_name}")
        directory.mkdir(parents=True, exist_ok=True)

        move_all_files(f"{config['paths']['qemu_out_share_path']}/", directory)

        logger.info("经过修复后，仍然没有出现Kernel driver in use")

    
    logger.info("device_code: %s", device_code)

    with open(f"{config['paths']['device_code_path']}/{driver_name}_pci.c", "w") as f:
        f.write(device_code)

    logger.info(f"{driver_name} device code saved to file 'device_code/{driver_name}_pci.c'")

def main():
    logger.info("设备建模开始")
    
    # 'acenic', 'adm8211', 'aectc', 'amd-xgbe', 'c3xxxvf', 'dt3155', 'ec_bhf', 'f81601', 'genwqe_driver', 'loongson-i2s-pci', 'mf6x4', 'mtk_t7xx', 'ns83820', 'pata_it8213', 'pata_sil680', 'RDC321x Southbridge', 'rvu_af'
    #  'adm8211', 'aectc', 'acenic',   'amd-xgbe', 
    # 'c3xxxvf', 'adm8211', 'acenic', 'amd-xgbe', 'aectc', 'dt3155', 'ec_bhf', 'f81601', 'genwqe_driver', 'loongson-i2s-pci', 'mf6x4', 
    # 'c3xxxvf', 'adm8211', 'acenic', 'amd-xgbe', 'aectc', 'dt3155', 'ec_bhf', 'f81601', 'genwqe_driver', 'loongson-i2s-pci', 'mf6x4', 'mtk_t7xx', 'ns83820', 'pata_it8213', 'pata_sil680', 
    # driver_name_list = ['c3xxxvf', 'adm8211', 'acenic', 'amd-xgbe', 'aectc', 'dt3155', 'ec_bhf', 'f81601', 'genwqe_driver', 'loongson-i2s-pci', 'mf6x4', 'mtk_t7xx', 'ns83820', 'pata_it8213', 'pata_sil680'] 
    #  'he', 'wdt_pci', 'c6xx', 'myrs', '8139too', 'gxt4500', 'gxfb', 'sata_sil24', 'smtcfb', 'ie31200_edac', 'sata_sx4', 'myrb', 'eni', '
    # driver_name_list = ['sof-audio-pci-intel-mtl', 'phantom', 'r852', 'icom', 'isci', 'pch_phub', 'bochs-drm', 'hpilo', 'xhci_hcd', 'pata_efar', 'hisi_hpre', 'ifcvf', 'com20020', 'nvme', 'erdma', 'intel-lpss', 'pata_ninja32', 'c6xxvf', 'amd8111e', '4xxx', 'efct', 'sof-audio-pci-intel-skl', 'aic94xx', 'cb_pcidas', 'iavf', 'sata_nv', 'pm80xx', 'b2c2_flexcop_pci', 'cassini', 'c3xxx', '8139cp', 'acard-ahci', 'ml_ioh_gpio', 'aic79xx', 'sof-audio-pci-intel-tng', 'ata_piix', 'aty128fb', '3c59x', 'e1000', 'lxfb', 'atyfb', 'iosf_mbi_pci', 'dl2k', 'fealnx', 'exar_serial', '8250_lpss', 'bttv', 'e100', 'cdns2-pci', 'intel-qep', 'moxa', 'dc395x', 'pdc_adma', 'cx18', 'bt878', 'pata_atiixp', 'pm2fb', 'compat', 'cx88_audio', 'pcnet32', 'ahci']
    # driver_name_list = ['pata_ninja32', 'c6xxvf', 'amd8111e', '4xxx', 'efct', 'sof-audio-pci-intel-skl', 'aic94xx', 'cb_pcidas', 'iavf', 'sata_nv', 'pm80xx', 'b2c2_flexcop_pci', 'cassini', 'c3xxx', '8139cp', 'acard-ahci', 'ml_ioh_gpio', 'aic79xx', 'sof-audio-pci-intel-tng', 'ata_piix', 'aty128fb', '3c59x', 'e1000', 'lxfb', 'atyfb', 'iosf_mbi_pci', 'dl2k', 'fealnx', 'exar_serial', '8250_lpss', 'bttv', 'e100', 'cdns2-pci', 'intel-qep', 'moxa', 'dc395x', 'pdc_adma', 'cx18', 'bt878', 'pata_atiixp', 'pm2fb', 'compat', 'cx88_audio', 'pcnet32', 'ahci']
    # driver_name_list = ['he', '8139too', 'arcmsr', 'pcwd_pci', 'ata_generic', 'phantom', 'r852', 'cb_pcidas', 'iosf_mbi_pci', 'cdns2-pci']
    # 'wdt_pci', 'gxt4500', 'sata_sil24', 'smtcfb', 'ie31200_edac', 'de2104x', '8250_mid', 'i740fb', 'sm501', 'carminefb', 'mxser', 'hpilo', 
    # driver_name_list = ['sata_nv', 'ml_ioh_gpio', 'fealnx', '8250_lpss', 'bttv', 'bt878', 'cx88_audio', '8139too', 'pcwd_pci', 'iosf_mbi_pci', 'r852', 'bochs-drm', 'intel-lpss', 'sof-audio-pci-intel-mtl', 'sof-audio-pci-intel-skl', 'sof-audio-pci-intel-tng', 'acard-ahci', 'phantom', 'intel-qep']
    # 'cx88_audio', 'bt878', 'intel-qep', 'bttv', '8250_lpss', 'fealnx', 'iosf_mbi_pci', 'sof-audio-pci-intel-tng', 'ml_ioh_gpio', 'acard-ahci', 'sata_nv', 'sof-audio-pci-intel-skl', 'intel-lpss', 'hpilo', 'bochs-drm', 'r852', 'phantom', 'sof-audio-pci-intel-mtl', 'mxser', 'carminefb', 'sm501', 'arkfb', 'i740fb', 'pcwd_pci', '8250_mid', 'de2104x', 'ie31200_edac', 'smtcfb', 'sata_sil24', 'gxt4500', '8139too', 'dt3155', 'ec_bhf', 'aectc', 'RDC321x Southbridge', 'ns83820', 'uli526x', 'myrs',  
    # driver_name_list= ['c6xx', 'gxfb', 'myrb', 'fm10k', 'efa', 'pata_ninja32', 'c6xxvf', '8139cp', 'pdc_adma', 'cx18', 'eni', 'ahci', 'wdt_pci']
    # driver_name_list = ['8139too', 'sata_sil24', 'ie31200_edac', 'de2104x', 'pcwd_pci', 'wdt_pci', 'carminefb', 'phantom', 'r852', 'hpilo', 'sof-audio-pci-intel-tng', 'bt878', 'dt3155', 'aectc', 'RDC321x Southbridge', 'ns83820', 'uli526x', 'c6xx', 'efa', 'c6xxvf']
    driver_name_list = ['dt3155', 'ec_bhf']
    # driver_name_list = ['c6xx']
    # driver_name_list = ['8250_mid', 'pcwd_pci', 'aic7xxx', 'ioatdma', 'cb_pcimdas', 'i740fb', 'fm10k', 'sata_qstor', 'arkfb', 'bna', 'daqboard2000', 'sm501', 'carminefb', 'mxser', 'ata_generic', 'pata_amd', 'efa', 'cobalt']
    # driver_name_list = ['hisi_dma', 'e1000e', 'pata_mpiix', 'arcmsr', 'idxd', 'de2104x', 'ast', 'ddbridge']
    # driver_name_list = ['hisi_dma', 'e1000e', 'pata_mpiix', 'arcmsr', 'idxd', 'de2104x', 'ast', 'ddbridge', '8250_mid', 'pcwd_pci', 'aic7xxx', 'ioatdma', 'cb_pcimdas', 'i740fb', 'fm10k', 'sata_qstor', 'arkfb', 'bna', 'daqboard2000', 'sm501', 'carminefb', 'mxser', 'ata_generic', 'pata_amd', 'efa', 'cobalt', 'sof-audio-pci-intel-mtl', 'phantom', 'r852', 'icom', 'isci', 'pch_phub', 'bochs-drm', 'hpilo', 'xhci_hcd', 'pata_efar', 'hisi_hpre', 'ifcvf', 'com20020', 'nvme', 'erdma', 'intel-lpss', 'pata_ninja32', 'c6xxvf', 'amd8111e', '4xxx', 'efct', 'sof-audio-pci-intel-skl', 'aic94xx', 'cb_pcidas', 'iavf', 'sata_nv', 'pm80xx', 'b2c2_flexcop_pci', 'cassini', 'c3xxx', '8139cp', 'acard-ahci', 'ml_ioh_gpio', 'aic79xx', 'sof-audio-pci-intel-tng', 'ata_piix', 'aty128fb', '3c59x', 'e1000', 'lxfb', 'atyfb', 'iosf_mbi_pci', 'dl2k', 'fealnx', 'exar_serial', '8250_lpss', 'bttv', 'e100', 'cdns2-pci', 'intel-qep', 'moxa', 'dc395x', 'pdc_adma', 'cx18', 'bt878', 'pata_atiixp', 'pm2fb', 'compat', 'cx88_audio', 'pcnet32', 'ahci']
    #river_name_list = ['RDC321x Southbridge', 'snet-vdpa-driver', 'sof-audio-pci-intel-cnl', 'uli526x', 'rvu_af']
    # driver_name_list = ['sof-audio-pci-intel-cnl']
    for driver_name in driver_name_list:
        logger.info("开始建模设备: %s", driver_name)
        try:
            model_device(driver_name=driver_name)
            logger.info("设备建模完成: %s", driver_name)
        except Exception as e:
            # 保存，记录错误信息，并继续进行下一个设备的建模，保存到shares/crash/中

            directory = Path(f"{config['paths']['qemu_out_shares_path']}/crash/{driver_name}")
            directory.mkdir(parents=True, exist_ok=True)

            move_all_files(f"{config['paths']['logs_path']}/{get_app_start_time()}_{get_driver_name()}", directory)

            logger.error("设备建模失败: %s", driver_name)
            logger.error("错误信息: %s", str(e))
    
    logger.info("设备建模结束")
    # for driver_name in driver_name_list:
    #     logger.info("开始建模设备: %s", driver_name)

    #     model_device(driver_name=driver_name)
    #     logger.info("设备建模完成: %s", driver_name)
    
    # logger.info("设备建模结束")

if __name__ == '__main__':
    main()