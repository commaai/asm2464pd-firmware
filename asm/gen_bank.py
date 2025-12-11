#!/usr/bin/env python3
"""
Generate single assembly file for ASM2464PD firmware with real 8051 instructions.
Uses proper labels for all branches within each bank.
"""

import os
import re
import sys
from disasm8051 import Disassembler, INSTRUCTIONS

# Well-known labels from reverse engineering
KNOWN_LABELS = {
    0x0000: ("reset_vector", "Reset vector - jumps to main"),
    0x0003: ("ext0_isr_vector", "External interrupt 0 vector"),
    0x000b: ("timer0_isr_vector", "Timer 0 interrupt vector"),
    0x0013: ("ext1_isr_vector", "External interrupt 1 vector"),
    0x001b: ("timer1_isr_vector", "Timer 1 interrupt vector"),
    0x0023: ("serial_isr_vector", "Serial interrupt vector"),
    0x002b: ("timer2_isr_vector", "Timer 2 interrupt vector"),
    0x030a: ("sys_init", "System initialization"),
    0x0e5b: ("ext0_isr", "External interrupt 0 handler"),
    0x431a: ("main_entry", "Main entry point"),
    0x4352: ("main_loop", "Main processing loop"),
    0x4486: ("timer1_isr", "Timer 1 interrupt handler"),
    0x50db: ("common_handler", "Common handler routine"),
    0x0bfd: ("mul16x16", "16x16 bit multiplication"),
    0x0c9e: ("add32", "32-bit addition"),
    0x0cab: ("sub32", "32-bit subtraction"),
    0x0cb9: ("mul32", "32-bit multiplication"),
    0x0d08: ("or32", "32-bit OR"),
    0x0d15: ("xor32", "32-bit XOR"),
    0x0d46: ("shl32", "32-bit shift left"),
    0x0d78: ("idata_load_dword", "Load 32-bit from IDATA"),
    0x0d84: ("xdata_load_dword", "Load 32-bit from XDATA"),
}


def parse_header_labels(src_dir):
    """Parse function addresses from header files."""
    labels = dict(KNOWN_LABELS)
    if not os.path.exists(src_dir):
        return labels

    for root, dirs, files in os.walk(src_dir):
        for fname in files:
            if fname.endswith('.h') or fname.endswith('.c'):
                fpath = os.path.join(root, fname)
                try:
                    with open(fpath, 'r') as f:
                        content = f.read()
                    for match in re.finditer(r'/\*\s*0x([0-9a-fA-F]+)-0x[0-9a-fA-F]+\s*\*/', content):
                        addr = int(match.group(1), 16)
                        start = max(0, match.start() - 300)
                        preceding = content[start:match.start()]
                        func_match = re.search(r'(\w+)\s*\([^)]*\)\s*;?\s*$', preceding)
                        if func_match and addr not in labels:
                            labels[addr] = (func_match.group(1), f"from {fname}")
                except Exception:
                    pass
    return labels


def find_function_boundaries(data, start_addr, end_addr, call_targets):
    """Find function boundaries based on call targets."""
    boundaries = set()
    boundaries.add(start_addr)
    for target in call_targets:
        if start_addr <= target < end_addr:
            boundaries.add(target)
    return sorted(boundaries)


def collect_targets_for_region(data, start_file, end_file, base_addr):
    """Collect all branch targets and instruction addresses for a region."""
    region_data = data[start_file:end_file]
    disasm = Disassembler(region_data, base_addr, {}, use_raw_branches=False)

    instruction_addrs = set()
    offset = 0
    while offset < len(region_data):
        addr = base_addr + offset
        instruction_addrs.add(addr)
        _, size, _ = disasm.disassemble_instruction(offset)
        offset += size

    return disasm.branch_targets, disasm.call_targets, instruction_addrs


def generate_labels(branch_targets, call_targets, known_labels, base_addr, end_addr):
    """Generate label names for all targets."""
    all_labels = {}

    for addr, info in known_labels.items():
        if base_addr <= addr < end_addr:
            all_labels[addr] = info

    for target in sorted(branch_targets):
        if base_addr <= target < end_addr and target not in all_labels:
            if target in call_targets:
                all_labels[target] = f'func_{target:04x}'
            else:
                all_labels[target] = f'L_{target:04x}'

    return all_labels


def format_line(instr, addr, hex_bytes, comment_col=56):
    """Format an instruction line with aligned comments."""
    line = f'\t{instr}'
    if ';' in instr:
        padding = max(1, comment_col - len(line.expandtabs(8)))
        return f'{line}{" " * padding}[{addr:04x}]\n'
    else:
        padding = max(1, comment_col - len(line.expandtabs(8)))
        return f'{line}{" " * padding}; {addr:04x}: {hex_bytes}\n'


def disassemble_region(f, data, start_file, end_file, base_addr, known_labels, region_name):
    """Disassemble a region and write to file."""
    region_data = data[start_file:end_file]
    region_size = end_file - start_file
    end_addr = base_addr + region_size

    print(f"  {region_name}: 0x{base_addr:04x}-0x{end_addr:04x} ({region_size} bytes)")

    # First pass: collect targets
    branch_targets, call_targets, instruction_addrs = collect_targets_for_region(
        data, start_file, end_file, base_addr)

    # Generate labels for valid targets
    valid_branch_targets = {t for t in branch_targets
                           if base_addr <= t < end_addr and t in instruction_addrs}
    valid_call_targets = call_targets & instruction_addrs
    all_labels = generate_labels(valid_branch_targets, valid_call_targets,
                                 known_labels, base_addr, end_addr)

    func_boundaries = find_function_boundaries(data, base_addr, end_addr, valid_call_targets)

    # Create disassembler
    disasm = Disassembler(region_data, base_addr, all_labels, use_raw_branches=False,
                          valid_targets=instruction_addrs, bank_end=end_addr)

    # Write region header
    f.write(f'\n; {"=" * 70}\n')
    f.write(f'; {region_name}: 0x{base_addr:04x}-0x{end_addr:04x}\n')
    f.write(f'; {"=" * 70}\n\n')
    f.write(f'\t.area\tCODE_{region_name.upper()}\t(ABS,CODE)\n')
    f.write(f'\t.org\t0x{base_addr:04x}\n\n')

    # Disassemble
    offset = 0
    while offset < len(region_data):
        addr = base_addr + offset

        # Function separator
        if addr in func_boundaries and addr in call_targets:
            f.write(f'\n; {"=" * 60}\n')
            f.write(f'; Function: 0x{addr:04x}\n')
            f.write(f'; {"=" * 60}\n')

        # Label
        if addr in all_labels:
            label = all_labels[addr]
            if isinstance(label, tuple):
                name, desc = label
                f.write(f'\n; {desc}\n')
                f.write(f'{name}:\n')
            else:
                f.write(f'\n{label}:\n')

        # Instruction
        instr, size, _ = disasm.disassemble_instruction(offset)
        if instr:
            hex_bytes = ' '.join(f'{region_data[offset+i]:02x}' for i in range(size))
            f.write(format_line(instr, addr, hex_bytes))
        else:
            b = region_data[offset]
            f.write(format_line(f'.db\t#0x{b:02x}', addr, f'{b:02x}'))
            size = 1

        offset += size

    return len(all_labels)


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    fw_path = os.path.join(project_root, 'fw.bin')
    src_dir = os.path.join(project_root, 'src')
    bank0_path = os.path.join(script_dir, 'bank0.asm')
    bank1_path = os.path.join(script_dir, 'bank1.asm')

    if len(sys.argv) > 1:
        fw_path = sys.argv[1]

    with open(fw_path, 'rb') as f:
        data = f.read()

    print(f"Firmware: {len(data)} bytes (0x{len(data):05x})")

    # Parse known labels
    known_labels = parse_header_labels(src_dir)
    print(f"Known labels: {len(known_labels)}")

    # Bank 1 labels need address translation (file addr 0x10000+ -> exec addr 0x8000+)
    bank1_labels = {}
    for addr, info in known_labels.items():
        if addr >= 0x10000:
            mapped = 0x8000 + (addr - 0x10000)
            bank1_labels[mapped] = info

    # Generate Bank 0
    print(f"\nGenerating bank0.asm...")
    bank0_size = min(0x10000, len(data))
    with open(bank0_path, 'w') as f:
        f.write(""";
; ASM2464PD Firmware - Bank 0
; Address range: 0x0000-0x{:04x}
; Size: {} bytes
;
; Build with: ./build.sh
;

\t.module\tbank0
""".format(bank0_size - 1, bank0_size))
        labels0 = disassemble_region(f, data, 0, bank0_size, 0, known_labels, "bank0")

    print(f"Generated: bank0.asm ({labels0} labels)")

    # Generate Bank 1
    labels1 = 0
    if len(data) > 0x10000:
        bank1_size = len(data) - 0x10000
        print(f"\nGenerating bank1.asm...")
        with open(bank1_path, 'w') as f:
            f.write(""";
; ASM2464PD Firmware - Bank 1
; File offset: 0x10000-0x{:05x}
; Execution address: 0x8000-0x{:04x}
; Size: {} bytes
;
; Build with: ./build.sh
;

\t.module\tbank1
""".format(len(data) - 1, 0x8000 + bank1_size - 1, bank1_size))
            labels1 = disassemble_region(f, data, 0x10000, len(data), 0x8000,
                                         bank1_labels, "bank1")
        print(f"Generated: bank1.asm ({labels1} labels)")

    print(f"\nTotal: {labels0 + labels1} labels")
    print("Run ./build.sh to build and verify.")


if __name__ == '__main__':
    main()
