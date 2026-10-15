import re

# Precompiled regular expressions
COMMENT_PATTERN = re.compile(r'/\*.*?\*/|//.*$', re.DOTALL | re.MULTILINE)
INCLUDE_PATTERN = re.compile(r'#include\s+"([^"]+)"')
NAME_FIELD_PATTERN = re.compile(r'\.name\s*=\s*"([^"]*)"')
NAME_VAR_PATTERN = re.compile(r'\.name\s*=\s*([a-zA-Z_][a-zA-Z0-9_]*)')
DEFINE_STRING_PATTERN = re.compile(r'"([^"]*)"')
VARIABLE_STRING_PATTERN = re.compile(r'=\s*"([^"]*)"')
FUNCTION_PATTERN = re.compile(r'\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\(')
STRUCT_PATTERN = re.compile(r'\b(struct|enum|union)\s+([a-zA-Z_][a-zA-Z0-9_]*)')
TYPEDEF_PATTERN = re.compile(r'\b([a-zA-Z_][a-zA-Z0-9_]*_t)\b')
MACRO_PATTERN = re.compile(r'\b([A-Z_][A-Z0-9_]*)\b(?!\s*\()')
ARRAY_PATTERN = re.compile(r'\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\[')
ARRAY_SIZE_PATTERN = re.compile(r'ARRAY_SIZE\s*\(\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*\)')
FUNC_PTR_PATTERN = re.compile(r'(?:->|\.)\s*[a-zA-Z_][a-zA-Z0-9_]*\s*=\s*&?\s*([a-zA-Z_][a-zA-Z0-9_]*)')
PCI_DRIVER_PATTERN = re.compile(r'pci_(register|unregister)_driver\s*\(\s*&?\s*\b([a-zA-Z_][a-zA-Z0-9_]*)\b')

# Set of C keywords
C_KEYWORDS = {
    'auto', 'break', 'case', 'char', 'const', 'continue', 'default',
    'do', 'double', 'else', 'enum', 'extern', 'float', 'for', 'goto',
    'if', 'int', 'long', 'register', 'return', 'short', 'signed',
    'sizeof', 'static', 'struct', 'switch', 'typedef', 'union',
    'unsigned', 'void', 'volatile', 'while', 'NULL', 'true', 'false'
}

# Symbol type mapping
SYMBOL_TYPE_MAPPING = {
    'define': '宏定义',
    'enum': 'enum定义',
    'enum_typedef': 'enum-typedef定义',
    'func': 'func定义',
    'struct': 'struct定义',
    'struct_typedef': 'struct-typedef定义',
    'struct_init': 'struct-init定义',
    'var': 'var声明'
}

# File mapping configuration
FILE_MAPPINGS = {
    'define': '../linux/define.jsonl',
    'enum': '../linux/enum.jsonl',
    'enum_typedef': '../linux/enum-typedef.jsonl',
    'func': '../linux/func.jsonl',
    'struct': '../linux/struct.jsonl',
    'struct_typedef': '../linux/struct-typedef.jsonl',
    'struct_init': '../linux/struct-init.jsonl',
    'var': '../linux/var.jsonl'
}

# Output directory
OUTPUT_DIR = "my_pci_driver"

# Maximum number of iterations (to prevent infinite loops)
MAX_ITERATIONS = 100000
