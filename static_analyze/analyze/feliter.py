#!/usr/bin/env python3
# filter_clang_commands.py
import json
import sys
import shlex

def filter_clang_incompatible_options(input_file, output_file):
    """过滤compile_commands.json中的clang不兼容选项"""
    
    with open(input_file, 'r') as f:
        data = json.load(f)
    
    clang_incompatible = [
        '-mstack-protector-guard-symbol=',
        '-mfunction-return=thunk-extern',
        '-fzero-call-used-regs=',
        '-fstrict-flex-arrays=',
        '-fconserve-stack',
        '-fno-var-tracking-assignments',
        '-fno-ipa-sra',
        '-mindirect-branch-register',
        '-mindirect-branch-cs-prefix',
        '--param=',
    ]
    
    for entry in data:
        # Handle the 'command' field (string)
        if 'command' in entry:
            # Split the command into an argument list
            args = shlex.split(entry['command'])
            filtered_args = []
            
            i = 0
            while i < len(args):
                arg = args[i]
                skip = False
                
                # Check whether the option is incompatible
                for incompatible in clang_incompatible:
                    if arg.startswith(incompatible):
                        skip = True
                        break
                
                # Special handling for certain options
                if arg == '-mpreferred-stack-boundary=4':
                    # clang uses different options
                    filtered_args.append('-mstack-alignment=16')
                    skip = True
                elif arg.startswith('-mindirect-branch='):
                    skip = True
                elif arg.startswith('-fcf-protection='):
                    skip = True
                
                if not skip:
                    filtered_args.append(arg)
                i += 1
            
            # Rebuild the command string
            entry['command'] = ' '.join(shlex.quote(arg) for arg in filtered_args)
        
        # Handle the 'arguments' field (list)
        if 'arguments' in entry:
            filtered_args = []
            for arg in entry['arguments']:
                skip = False
                
                for incompatible in clang_incompatible:
                    if arg.startswith(incompatible):
                        skip = True
                        break
                
                if not skip:
                    filtered_args.append(arg)
            
            # Add clang compatibility options
            filtered_args.extend([
                '-Wno-unknown-warning-option',
                '-Wno-everything',
                '-Qunused-arguments',
            ])
            
            entry['arguments'] = filtered_args
    
    with open(output_file, 'w') as f:
        json.dump(data, f, indent=2)
    
    print(f"Filtered compile commands saved to: {output_file}")
    return output_file

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input_compile_commands.json> <output_filtered.json>")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    filter_clang_incompatible_options(input_file, output_file)

