from pathlib import Path
import shutil
def append_to_file(file_path, text):
    with open(file_path, 'r', encoding='utf-8') as f:
        if text in f.read():
            return
            
    with open(file_path, 'a', encoding='utf-8') as f:
        f.write(text)

def write_to_file(file_path, text):
    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(text)

def read_file(file_path):
    with open(file_path, 'r', encoding='utf-8', errors="ignore") as f:
        return f.read()

def remove_content_from_file(file_path, text):
    with open(file_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()
        
    with open(file_path, 'w', encoding='utf-8') as f:
        for line in lines:
            if text not in line:
                f.write(line)

def move_all_files(src_dir: str, dst_dir: str):
    src = Path(src_dir)
    dst = Path(dst_dir)

    # 确保目标目录存在
    dst.mkdir(parents=True, exist_ok=True)

    # 遍历源目录中的所有项目（文件和子目录）
    for item in src.iterdir():
        shutil.move(str(item), str(dst / item.name))


if __name__ == '__main__':
    meson_contentd = "system_ss.add(when: 'CONFIG_ADM8211', if_true: files('adm8211_pci.c'))\n"
    remove_content_from_file("/home/user/qemu_8.2_linux_6.7/src/qemu-stable-8.2/hw/fake_pci/meson.build", meson_contentd)
    