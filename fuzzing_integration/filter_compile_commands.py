#!/usr/bin/env python3
"""
Filter compile_commands.json to remove arguments not supported by clang-14.
This script reads a compile_commands.json file, filters out unsupported
arguments, and writes a filtered version.
"""

import json
import sys
import os
import argparse

# Arguments known to be unsupported by clang-14
# Based on error messages from the analyzer
UNSUPPORTED_ARGS = {
    '-mstack-protector-guard-symbol=__ref_stack_chk_guard',
    '-mfunction-return=thunk-extern',
    '-fzero-call-used-regs=used-gpr',
    '-fstrict-flex-arrays=3',
    '-mharden-sls=all',
    # Add other unsupported arguments as needed
}

def filter_arguments(args):
    """Filter out unsupported arguments from the arguments list."""
    filtered = []
    i = 0
    while i < len(args):
        arg = args[i]
        
        # Skip unsupported arguments
        if arg in UNSUPPORTED_ARGS:
            i += 1
            continue
            
        # Also check for arguments with values that might be unsupported
        # For example, if we see '-mllvm' followed by certain options
        if arg == '-mllvm':
            # Keep -mllvm and its value, but we could filter specific values if needed
            if i + 1 < len(args):
                filtered.append(arg)
                filtered.append(args[i + 1])
                i += 2
            else:
                filtered.append(arg)
                i += 1
        else:
            filtered.append(arg)
            i += 1
    
    return filtered

def filter_compile_commands(input_file, output_file):
    """Filter compile_commands.json and write to output file."""
    try:
        with open(input_file, 'r') as f:
            data = json.load(f)
    except (json.JSONDecodeError, IOError) as e:
        print(f"Error reading {input_file}: {e}", file=sys.stderr)
        return False
    
    filtered_count = 0
    total_count = 0
    
    for entry in data:
        total_count += 1
        if 'arguments' in entry:
            original_len = len(entry['arguments'])
            entry['arguments'] = filter_arguments(entry['arguments'])
            filtered_count += original_len - len(entry['arguments'])
        elif 'command' in entry:
            # Some compile_commands.json use 'command' string instead of 'arguments' array
            # We would need to parse the command string, but for now we'll just keep it
            pass
    
    try:
        with open(output_file, 'w') as f:
            json.dump(data, f, indent=2)
    except IOError as e:
        print(f"Error writing {output_file}: {e}", file=sys.stderr)
        return False
    
    print(f"Filtered {filtered_count} unsupported arguments from {total_count} entries")
    print(f"Filtered compile_commands.json written to {output_file}")
    return True

def main():
    parser = argparse.ArgumentParser(
        description='Filter compile_commands.json to remove arguments not supported by clang-14'
    )
    parser.add_argument('input', help='Input compile_commands.json file')
    parser.add_argument('-o', '--output', default='compile_commands_filtered.json',
                       help='Output filtered file (default: compile_commands_filtered.json)')
    parser.add_argument('--list-unsupported', action='store_true',
                       help='List unsupported arguments and exit')
    
    args = parser.parse_args()
    
    if args.list_unsupported:
        print("Unsupported arguments (will be filtered out):")
        for arg in sorted(UNSUPPORTED_ARGS):
            print(f"  {arg}")
        return 0
    
    if not os.path.exists(args.input):
        print(f"Input file not found: {args.input}", file=sys.stderr)
        return 1
    
    success = filter_compile_commands(args.input, args.output)
    return 0 if success else 1

if __name__ == '__main__':
    sys.exit(main())
