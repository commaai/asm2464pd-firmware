#!/usr/bin/env python3
"""Run fw_tinygrad.bin in emulator, logging all MOVC (flash/ROM data reads)."""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'emulate'))

from emu import Emulator

emu = Emulator(trace=False, log_hw=False, log_uart=False, usb_delay=200000)
emu.load_firmware("fw_tinygrad.bin")
emu.reset()

# Wrap read_code to detect MOVC (non-fetch) reads
original_read_code = emu.memory.read_code
fetch_flag = [True]  # True = normal fetch, False = MOVC data read
cycle_count = [0]

def logging_read_code(addr):
    val = original_read_code(addr)
    if not fetch_flag[0]:
        dpx = emu.memory.sfr[emu.memory.SFR_DPX - 0x80]
        bank = dpx & 1
        # Calculate file offset
        if addr >= 0x8000 and bank == 1:
            file_off = 0xFF6B + (addr - 0x8000)
        else:
            file_off = addr
        print(f"MOVC @ cycle {cycle_count[0]:>8d}  PC=0x{emu.cpu.pc-1:04X}  "
              f"addr=0x{addr:04X} (file 0x{file_off:05X}, bank{bank})  "
              f"val=0x{val:02X}  DPTR=0x{emu.cpu.DPTR:04X}  A_was=0x{(addr - emu.cpu.DPTR) & 0xFF:02X}")
    return val

emu.memory.read_code = logging_read_code
emu.cpu.read_code = logging_read_code

# Patch the CPU's execute to set fetch_flag around MOVC
original_execute = emu.cpu.execute

def patched_execute(opcode):
    if opcode == 0x83 or opcode == 0x93:
        fetch_flag[0] = False
        result = original_execute(opcode)
        fetch_flag[0] = True
        return result
    return original_execute(opcode)

emu.cpu.execute = patched_execute

# Patch step to track cycles
original_step = emu.step

def patched_step():
    result = original_step()
    cycle_count[0] = emu.cpu.cycles
    return result

emu.step = patched_step

print(f"Running fw_tinygrad.bin in emulator (max 10M cycles)...")
print(f"Logging all MOVC (flash data read) instructions")
print(f"=" * 80)

try:
    reason = emu.run(max_cycles=10_000_000)
    print(f"\n{'=' * 80}")
    print(f"Stopped: {reason} after {emu.cpu.cycles} cycles")
except KeyboardInterrupt:
    print(f"\n{'=' * 80}")
    print(f"Interrupted after {emu.cpu.cycles} cycles")
