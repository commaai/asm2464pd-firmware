#!/usr/bin/env python3
"""
Test round-trip disassembly and reassembly of firmware banks.

This test verifies that the disassembler produces valid assembly that can
be reassembled to identical binaries using SDCC.

Usage:
    pytest test/test_roundtrip.py
    python3 -m pytest test/test_roundtrip.py -v
"""

import os
import sys
import subprocess
import tempfile
from pathlib import Path
import pytest

# Add emulate directory to path for disassembler
sys.path.insert(0, str(Path(__file__).parent.parent / 'emulate'))

from disasm8051 import Disassembler


# Module-level fixture to check for SDCC
@pytest.fixture(scope="module")
def check_sdcc():
    """Verify SDCC is installed."""
    try:
        subprocess.run(['sdas8051', '-h'], capture_output=True, check=False)
    except FileNotFoundError:
        pytest.skip("SDCC not found. Install with: sudo apt-get install sdcc")


def disassemble_bank(data, base_addr, bank_name):
    """
    Disassemble a bank to SDCC assembly.

    Args:
        data: Binary data to disassemble
        base_addr: Base address for this bank
        bank_name: Name of the bank (bank0 or bank1)

    Returns:
        Assembly code as string
    """
    end_addr = base_addr + len(data)

    # First pass: collect all branch targets and instruction addresses
    disasm = Disassembler(data, base_addr, {}, use_raw_branches=False)

    instruction_addrs = set()
    offset = 0
    while offset < len(data):
        addr = base_addr + offset
        instruction_addrs.add(addr)
        _, size, _ = disasm.disassemble_instruction(offset)
        offset += size

    # Perform first pass to collect targets
    disasm.first_pass()

    # Filter targets to only valid instruction addresses within this bank
    valid_branch_targets = {t for t in disasm.branch_targets
                           if base_addr <= t < end_addr and t in instruction_addrs}
    valid_call_targets = disasm.call_targets & instruction_addrs

    # Generate labels
    all_labels = {}
    for target in sorted(valid_branch_targets | valid_call_targets):
        if base_addr <= target < end_addr:
            if target in valid_call_targets:
                all_labels[target] = f'func_{target:04x}'
            else:
                all_labels[target] = f'L_{target:04x}'

    # Create new disassembler with labels
    disasm = Disassembler(data, base_addr, all_labels, use_raw_branches=False,
                          valid_targets=instruction_addrs, bank_end=end_addr)

    # Generate assembly header
    asm_lines = []
    asm_lines.append(f";")
    asm_lines.append(f"; ASM2464PD Firmware - {bank_name.upper()}")
    asm_lines.append(f"; Auto-generated for round-trip test")
    asm_lines.append(f"; Address range: 0x{base_addr:04x}-0x{end_addr:04x}")
    asm_lines.append(f"; Size: {len(data)} bytes")
    asm_lines.append(f";")
    asm_lines.append(f"")
    asm_lines.append(f"\t.module\t{bank_name}")
    asm_lines.append(f"\t.area\tCODE\t(ABS,CODE)")
    asm_lines.append(f"\t.org\t0x{base_addr:04x}")
    asm_lines.append(f"")

    # Disassemble
    offset = 0
    while offset < len(data):
        addr = base_addr + offset

        # Add label if needed
        if addr in all_labels:
            label = all_labels[addr]
            asm_lines.append(f"")
            asm_lines.append(f"{label}:")

        # Disassemble instruction
        instr, size, _ = disasm.disassemble_instruction(offset)

        if instr:
            # Format with hex comment
            hex_bytes = ' '.join(f'{data[offset+i]:02x}' for i in range(size))
            asm_lines.append(f"\t{instr:<40}; {addr:04x}: {hex_bytes}")
        else:
            # Unknown byte - emit as .db
            b = data[offset]
            asm_lines.append(f"\t.db\t0x{b:02x}\t\t\t\t; {addr:04x}: ???")
            size = 1

        offset += size

    return '\n'.join(asm_lines)


def assemble_with_sdcc(asm_code, output_dir, bank_name, base_addr):
    """
    Assemble code with SDCC and convert to binary.

    Args:
        asm_code: Assembly source code
        output_dir: Directory for output files
        bank_name: Name of the bank
        base_addr: Base address for the bank

    Returns:
        Binary data as bytes
    """
    asm_file = output_dir / f"{bank_name}.asm"
    rel_file = output_dir / f"{bank_name}.rel"
    ihx_file = output_dir / f"{bank_name}.ihx"

    # Write assembly to file
    with open(asm_file, 'w') as f:
        f.write(asm_code)

    # Assemble with sdas8051
    print(f"  Assembling {bank_name}...")
    result = subprocess.run(
        ['sdas8051', '-plosgff', str(rel_file), str(asm_file)],
        capture_output=True,
        text=True
    )

    if result.returncode != 0:
        print(f"Assembler error for {bank_name}:")
        print(result.stdout)
        print(result.stderr)
        raise RuntimeError(f"Assembly failed for {bank_name}")

    # Link to Intel HEX
    print(f"  Linking {bank_name}...")
    result = subprocess.run(
        ['sdld', '-i', str(ihx_file), str(rel_file)],
        capture_output=True,
        text=True
    )

    if result.returncode != 0:
        print(f"Linker error for {bank_name}:")
        print(result.stdout)
        print(result.stderr)
        raise RuntimeError(f"Linking failed for {bank_name}")

    # Convert Intel HEX to binary
    print(f"  Converting to binary...")
    binary_data = bytearray(65536 if bank_name == 'bank0' else 32768)

    with open(ihx_file, 'r') as f:
        base_addr_ext = 0
        for line in f:
            if not line.startswith(':'):
                continue
            line = line.strip()
            n = int(line[1:3], 16)
            addr = int(line[3:7], 16)
            rec_type = int(line[7:9], 16)

            if rec_type == 0:  # Data record
                full_addr = base_addr_ext + addr - base_addr
                for i in range(n):
                    if 0 <= full_addr + i < len(binary_data):
                        binary_data[full_addr + i] = int(line[9 + i*2:11 + i*2], 16)
            elif rec_type == 2:  # Extended segment address
                base_addr_ext = int(line[9:13], 16) << 4
            elif rec_type == 4:  # Extended linear address
                base_addr_ext = int(line[9:13], 16) << 16

    return bytes(binary_data)


def compare_binaries(original, rebuilt, name):
    """
    Compare two binaries and report differences.

    Args:
        original: Original binary data
        rebuilt: Rebuilt binary data
        name: Name for reporting

    Returns:
        True if identical, False otherwise
    """
    min_len = min(len(original), len(rebuilt))

    # Find differences
    differences = []
    for i in range(min_len):
        if original[i] != rebuilt[i]:
            differences.append((i, original[i], rebuilt[i]))

    # Check size
    if len(original) != len(rebuilt):
        print(f"✗ {name}: Size mismatch!")
        print(f"  Original: {len(original)} bytes")
        print(f"  Rebuilt:  {len(rebuilt)} bytes")
        return False

    # Report results
    if not differences:
        print(f"✓ {name}: Byte-for-byte identical ({len(original)} bytes)")
        return True
    else:
        print(f"✗ {name}: {len(differences)} bytes differ")
        for i, (addr, orig, rebuilt) in enumerate(differences[:10]):
            print(f"  0x{addr:05x}: expected 0x{orig:02x}, got 0x{rebuilt:02x}")
        if len(differences) > 10:
            print(f"  ... and {len(differences) - 10} more")
        return False


def _test_bank_roundtrip(bank_path, bank_name, base_addr):
    """
    Internal helper for round-trip disassembly/reassembly of a bank.

    Args:
        bank_path: Path to the bank binary
        bank_name: Name of the bank (bank0 or bank1)
        base_addr: Base address for this bank (0x0000 for bank0, 0x8000 for bank1)

    Returns:
        True if test passes, False otherwise
    """
    print(f"\n{'='*60}")
    print(f"Testing {bank_name}")
    print(f"{'='*60}")

    # Load original binary
    with open(bank_path, 'rb') as f:
        original_data = f.read()

    print(f"Loaded: {len(original_data)} bytes from {bank_path}")

    # Disassemble
    print(f"Disassembling...")
    asm_code = disassemble_bank(original_data, base_addr, bank_name)

    # Count instructions
    asm_lines = [l for l in asm_code.split('\n') if l.strip() and not l.strip().startswith(';')]
    print(f"Generated: {len(asm_lines)} lines of assembly")

    # Reassemble
    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir = Path(tmpdir)
        print(f"Reassembling with SDCC...")
        rebuilt_data = assemble_with_sdcc(asm_code, tmpdir, bank_name, base_addr)

    # Trim to original size
    rebuilt_data = rebuilt_data[:len(original_data)]

    # Compare
    print(f"\nComparing binaries...")
    result = compare_binaries(original_data, rebuilt_data, bank_name)

    return result


def test_bank0_roundtrip(check_sdcc):
    """Test round-trip disassembly and reassembly of bank0."""
    project_root = Path(__file__).parent.parent
    bank0_path = project_root / 'bank0.bin'

    if not bank0_path.exists():
        pytest.skip(f"{bank0_path} not found")

    result = _test_bank_roundtrip(bank0_path, 'bank0', 0x0000)
    assert result, "Bank0 round-trip failed: binaries do not match"


def test_bank1_roundtrip(check_sdcc):
    """Test round-trip disassembly and reassembly of bank1."""
    project_root = Path(__file__).parent.parent
    bank1_path = project_root / 'bank1.bin'

    if not bank1_path.exists():
        pytest.skip(f"{bank1_path} not found")

    result = _test_bank_roundtrip(bank1_path, 'bank1', 0x8000)
    assert result, "Bank1 round-trip failed: binaries do not match"


if __name__ == '__main__':
    # Allow running as script for backwards compatibility
    pytest.main([__file__, '-v'])
