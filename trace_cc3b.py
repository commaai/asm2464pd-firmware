#!/usr/bin/env python3
"""
Trace writes to CC3B and related phase2 registers in stock firmware.

Research script: loads fw_tinygrad.bin into the emulator and watches
for writes to key MMIO addresses to understand firmware init flow.
"""

import sys
import os

# Add emulate directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'emulate'))

from emu import Emulator

# Addresses to watch
WATCH_ADDRS = {
    0xCC3B: "CC3B_CPU_CTRL2",
    0xCC3F: "CC3F_PRE_INIT_CTRL",
    0xE764: "E764_PHY_TIMER",
    0xE762: "E762_LINK_STATUS",
    0xB450: "B450_LTSSM",
}

# PC addresses to trace (phase2 and related entry points)
TRACE_PCS = {
    0x9078: "phase2_entry",
    0x0322: "pd_task_0322",
    0x0E33: "isr_ext0",
    0x202A: "main_loop_usb",
}

def main():
    # Create emulator - suppress UART noise, no USB delay
    emu = Emulator(trace=False, log_hw=False, log_uart=False, usb_delay=999999999)

    # Load stock firmware
    fw_path = os.path.join(os.path.dirname(__file__), 'fw_tinygrad.bin')
    print(f"Loading firmware: {fw_path}")
    emu.load_firmware(fw_path)

    # Collected log entries: (cycle, event_type, addr_name, value, old_value, pc, bank)
    log = []

    # Setup watches on all target addresses
    for addr, name in WATCH_ADDRS.items():
        # We need custom hooks that capture the info we need
        orig_read = emu.memory.xdata_read_hooks.get(addr)
        orig_write = emu.memory.xdata_write_hooks.get(addr)

        def make_hooks(a, n, o_read, o_write):
            def watch_read(address):
                val = o_read(address) if o_read else emu.hw.regs.get(address, emu.memory.xdata[address])
                pc = emu.cpu.pc
                bank = emu.memory.read_sfr(0x96) & 1
                log.append((emu.hw.cycles, 'READ', n, val, None, pc, bank))
                return val

            def watch_write(address, v):
                pc = emu.cpu.pc
                bank = emu.memory.read_sfr(0x96) & 1
                old_val = emu.hw.regs.get(address, emu.memory.xdata[address])
                log.append((emu.hw.cycles, 'WRITE', n, v, old_val, pc, bank))
                if o_write:
                    o_write(address, v)
                else:
                    # For MMIO regs, write to both regs dict and xdata
                    emu.hw.regs[address] = v
                    emu.memory.xdata[address] = v

            return watch_read, watch_write

        rd, wr = make_hooks(addr, name, orig_read, orig_write)
        emu.memory.xdata_read_hooks[addr] = rd
        emu.memory.xdata_write_hooks[addr] = wr

    # Setup PC trace points
    for pc_addr, label in TRACE_PCS.items():
        emu.trace_pcs.add(pc_addr)

    # Reset and run
    emu.reset()
    print(f"Starting execution at PC=0x{emu.cpu.pc:04X}")
    print(f"Running for up to 50M cycles...")
    print("-" * 80)

    MAX_CYCLES = 50_000_000
    reason = emu.run(max_cycles=MAX_CYCLES)

    print("-" * 80)
    print(f"Stopped: {reason} at PC=0x{emu.cpu.pc:04X}")
    print(f"Total: {emu.inst_count} instructions, {emu.cpu.cycles} cycles")
    print()

    # ===== Analysis =====
    # Filter writes to CC3B
    cc3b_writes = [(c, et, n, v, ov, pc, bk) for c, et, n, v, ov, pc, bk in log
                   if n == "CC3B_CPU_CTRL2" and et == "WRITE"]
    # All writes
    all_writes = [(c, et, n, v, ov, pc, bk) for c, et, n, v, ov, pc, bk in log
                  if et == "WRITE"]

    print("=" * 80)
    print("ALL WRITES TO WATCHED ADDRESSES (chronological)")
    print("=" * 80)
    print(f"{'Cycle':>10s}  {'Address':<20s}  {'Value':>6s}  {'Old':>6s}  {'PC':>8s}  {'Bank':>4s}")
    print("-" * 80)
    for cycle, _, name, val, old, pc, bank in all_writes:
        old_str = f"0x{old:02X}" if old is not None else "  --"
        print(f"{cycle:10d}  {name:<20s}  0x{val:02X}    {old_str}    0x{pc:04X}    {bank}")

    print()
    print("=" * 80)
    print(f"CC3B WRITES SPECIFICALLY ({len(cc3b_writes)} writes)")
    print("=" * 80)
    for cycle, _, name, val, old, pc, bank in cc3b_writes:
        old_str = f"0x{old:02X}" if old is not None else "  --"
        print(f"  Cycle {cycle:10d}: CC3B = 0x{val:02X} (was {old_str}) written by PC=0x{pc:04X} bank={bank}")

    # CC3B value sequence
    if cc3b_writes:
        print()
        print("CC3B value sequence:", " -> ".join(f"0x{v:02X}" for _, _, _, v, _, _, _ in cc3b_writes))

    # E764 writes
    e764_writes = [(c, et, n, v, ov, pc, bk) for c, et, n, v, ov, pc, bk in log
                   if n == "E764_PHY_TIMER" and et == "WRITE"]
    print()
    print(f"E764 (PHY_TIMER) WRITES ({len(e764_writes)} writes):")
    for cycle, _, name, val, old, pc, bank in e764_writes:
        old_str = f"0x{old:02X}" if old is not None else "  --"
        print(f"  Cycle {cycle:10d}: E764 = 0x{val:02X} (was {old_str}) written by PC=0x{pc:04X} bank={bank}")

    # E762 writes
    e762_writes = [(c, et, n, v, ov, pc, bk) for c, et, n, v, ov, pc, bk in log
                   if n == "E762_LINK_STATUS" and et == "WRITE"]
    print()
    print(f"E762 (LINK_STATUS) WRITES ({len(e762_writes)} writes):")
    for cycle, _, name, val, old, pc, bank in e762_writes:
        old_str = f"0x{old:02X}" if old is not None else "  --"
        print(f"  Cycle {cycle:10d}: E762 = 0x{val:02X} (was {old_str}) written by PC=0x{pc:04X} bank={bank}")

    # B450 writes
    b450_writes = [(c, et, n, v, ov, pc, bk) for c, et, n, v, ov, pc, bk in log
                   if n == "B450_LTSSM" and et == "WRITE"]
    print()
    print(f"B450 (LTSSM) WRITES ({len(b450_writes)} writes):")
    for cycle, _, name, val, old, pc, bank in b450_writes:
        old_str = f"0x{old:02X}" if old is not None else "  --"
        print(f"  Cycle {cycle:10d}: B450 = 0x{val:02X} (was {old_str}) written by PC=0x{pc:04X} bank={bank}")

    # CC3F writes
    cc3f_writes = [(c, et, n, v, ov, pc, bk) for c, et, n, v, ov, pc, bk in log
                   if n == "CC3F_PRE_INIT_CTRL" and et == "WRITE"]
    print()
    print(f"CC3F (PRE_INIT_CTRL) WRITES ({len(cc3f_writes)} writes):")
    for cycle, _, name, val, old, pc, bank in cc3f_writes:
        old_str = f"0x{old:02X}" if old is not None else "  --"
        print(f"  Cycle {cycle:10d}: CC3F = 0x{val:02X} (was {old_str}) written by PC=0x{pc:04X} bank={bank}")

    # Phase2 trace
    print()
    print("=" * 80)
    print("PC TRACE HITS")
    print("=" * 80)
    if emu.trace_pc_hits:
        for pc_addr in sorted(emu.trace_pc_hits.keys()):
            label = TRACE_PCS.get(pc_addr, "???")
            count = emu.trace_pc_hits[pc_addr]
            print(f"  0x{pc_addr:04X} ({label}): {count} hits")
    else:
        print("  No traced PCs were hit!")

    phase2_hit = 0x9078 in emu.trace_pc_hits
    print()
    print(f"Phase2 (0x9078) reached: {'YES' if phase2_hit else 'NO'}")

    # Also show reads of CC3B to understand what's consuming the value
    cc3b_reads = [(c, et, n, v, ov, pc, bk) for c, et, n, v, ov, pc, bk in log
                  if n == "CC3B_CPU_CTRL2" and et == "READ"]
    if cc3b_reads:
        print()
        print(f"CC3B READS ({len(cc3b_reads)} reads, showing first 50):")
        for cycle, _, name, val, _, pc, bank in cc3b_reads[:50]:
            print(f"  Cycle {cycle:10d}: CC3B read = 0x{val:02X} by PC=0x{pc:04X} bank={bank}")
        if len(cc3b_reads) > 50:
            print(f"  ... and {len(cc3b_reads) - 50} more reads")


if __name__ == '__main__':
    main()
