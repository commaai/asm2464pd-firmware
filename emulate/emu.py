#!/usr/bin/env python3
"""
ASM2464PD Firmware Emulator

Main entry point for emulating the ASM2464PD firmware.
Loads fw.bin and executes it with full peripheral emulation.

Usage:
    python emulate/emu.py [fw.bin] [options]

Options:
    --trace         Enable instruction tracing
    --break ADDR    Set breakpoint at address (hex)
    --max-cycles N  Stop after N cycles
    --help          Show this help
"""

import sys
import os
import argparse
from pathlib import Path

# Add emulate directory to path
sys.path.insert(0, str(Path(__file__).parent))

from cpu import CPU8051
from memory import Memory, MemoryMap
from peripherals import Peripherals
from hardware import HardwareState, create_hardware_hooks
from disasm8051 import INSTRUCTIONS


class Emulator:
    """ASM2464PD Firmware Emulator."""

    def __init__(self, trace: bool = False, log_hw: bool = False,
                 log_uart: bool = True, usb_delay: int = 5000):
        self.memory = Memory()
        self.cpu = CPU8051(
            read_code=self.memory.read_code,
            read_xdata=self.memory.read_xdata,
            write_xdata=self.memory.write_xdata,
            read_idata=self.memory.read_idata,
            write_idata=self.memory.write_idata,
            read_sfr=self.memory.read_sfr,
            write_sfr=self.memory.write_sfr,
            read_bit=self.memory.read_bit,
            write_bit=self.memory.write_bit,
            trace=trace,
        )

        # Hardware emulation (replaces simple stubs)
        self.hw = HardwareState(log_reads=log_hw, log_writes=log_hw, log_uart=log_uart)
        self.hw.usb_connect_delay = usb_delay
        create_hardware_hooks(self.memory, self.hw)

        # Statistics
        self.inst_count = 0
        self.last_pc = 0

        # Debugging: PC trace addresses (set via --trace-pc)
        self.trace_pcs = set()
        self.trace_pc_hits = {}  # Count hits per address

        # Debugging: Watch XDATA addresses
        self.watch_addrs = set()

        # Debugging: PC hit statistics (for analysis)
        self.pc_stats = {}  # PC -> hit count

    def load_firmware(self, path: str):
        """Load firmware binary."""
        with open(path, 'rb') as f:
            data = f.read()
        print(f"Loaded {len(data)} bytes from {path}")
        self.memory.load_firmware(data)

    def reset(self):
        """Reset emulator to initial state."""
        self.memory.reset()
        self.cpu.reset()
        self.inst_count = 0

        # Initialize SP to default
        self.memory.write_sfr(0x81, 0x07)

    def step(self) -> bool:
        """Execute one instruction. Returns False if halted."""
        if self.cpu.halted:
            return False

        self.last_pc = self.cpu.pc
        pc = self.cpu.pc

        # Track PC hit for statistics
        if self.pc_stats is not None:
            self.pc_stats[pc] = self.pc_stats.get(pc, 0) + 1

        # Check for trace PC addresses
        if pc in self.trace_pcs:
            self.trace_pc_hits[pc] = self.trace_pc_hits.get(pc, 0) + 1
            self._trace_pc_hit(pc)

        if self.cpu.trace:
            self._trace_instruction()

        cycles = self.cpu.step()
        self.inst_count += 1
        self.hw.tick(cycles, self.cpu)

        return not self.cpu.halted

    def _trace_pc_hit(self, pc: int):
        """Log when a traced PC is hit."""
        bank = self.memory.read_sfr(0x96) & 1
        hit_count = self.trace_pc_hits[pc]

        # Get instruction for context
        opcode = self.memory.read_code(pc)
        inst_bytes = [opcode]
        inst_len = self._get_inst_length(opcode)
        for i in range(1, inst_len):
            inst_bytes.append(self.memory.read_code((pc + i) & 0xFFFF))
        mnemonic = self._disassemble(inst_bytes)

        # Show CPU state
        a = self.cpu.A
        r7 = self.memory.read_idata(7)

        print(f"[{self.hw.cycles:8d}] PC=0x{pc:04X} bank={bank} hit#{hit_count} "
              f"{mnemonic} A=0x{a:02X} R7=0x{r7:02X}")

    def setup_watch(self, addr: int, name: str = None):
        """
        Setup a watch on an XDATA address to log reads/writes.

        Args:
            addr: XDATA address to watch
            name: Optional name for the address (for logging)
        """
        self.watch_addrs.add(addr)
        watch_name = name or f"0x{addr:04X}"

        # Create hooks that wrap existing functionality
        orig_read = self.memory.xdata_read_hooks.get(addr)
        orig_write = self.memory.xdata_write_hooks.get(addr)

        emu = self  # Capture reference

        def watch_read(a):
            val = orig_read(a) if orig_read else emu.memory.xdata[a]
            pc = emu.cpu.pc
            print(f"[{emu.hw.cycles:8d}] READ  {watch_name} = 0x{val:02X} (PC=0x{pc:04X})")
            return val

        def watch_write(a, v):
            pc = emu.cpu.pc
            old_val = emu.memory.xdata[a]
            print(f"[{emu.hw.cycles:8d}] WRITE {watch_name} = 0x{v:02X} (was 0x{old_val:02X}, PC=0x{pc:04X})")
            if orig_write:
                orig_write(a, v)
            else:
                emu.memory.xdata[a] = v

        self.memory.xdata_read_hooks[addr] = watch_read
        self.memory.xdata_write_hooks[addr] = watch_write

    def run(self, max_cycles: int = None, max_instructions: int = None) -> str:
        """
        Run emulator until halt, breakpoint, or limit reached.

        Returns reason for stopping.
        """
        while True:
            if max_cycles and self.cpu.cycles >= max_cycles:
                return "max_cycles"
            if max_instructions and self.inst_count >= max_instructions:
                return "max_instructions"

            if not self.step():
                if self.cpu.pc in self.cpu.breakpoints:
                    return "breakpoint"
                return "halted"

    def _trace_instruction(self):
        """Print trace of current instruction."""
        pc = self.cpu.pc
        bank = self.memory.read_sfr(0x96) & 1
        opcode = self.memory.read_code(pc)

        # Get instruction bytes
        inst_bytes = [opcode]
        inst_len = self._get_inst_length(opcode)
        for i in range(1, inst_len):
            inst_bytes.append(self.memory.read_code((pc + i) & 0xFFFF))

        # Format instruction
        hex_bytes = ' '.join(f'{b:02X}' for b in inst_bytes)
        mnemonic = self._disassemble(inst_bytes)

        # CPU state
        a = self.cpu.A
        psw = self.cpu.PSW
        sp = self.cpu.SP
        dptr = self.cpu.DPTR

        print(f"[{bank}] {pc:04X}: {hex_bytes:12s} {mnemonic:20s} "
              f"A={a:02X} PSW={psw:02X} SP={sp:02X} DPTR={dptr:04X}")

    def _get_inst_length(self, opcode: int) -> int:
        """Get instruction length in bytes using instruction table."""
        if opcode in INSTRUCTIONS:
            return INSTRUCTIONS[opcode][1]  # Size is second element of tuple
        return 1  # Default for unknown opcodes

    def _disassemble(self, inst_bytes: list) -> str:
        """Simple disassembler for trace output using full instruction set."""
        opcode = inst_bytes[0]

        # Use full instruction table from disasm8051
        if opcode not in INSTRUCTIONS:
            return f"??? ({opcode:02X})"

        mnemonic, size, operand_fmt = INSTRUCTIONS[opcode]

        # Check we have enough bytes
        if len(inst_bytes) < size:
            return f"??? ({opcode:02X})"

        # Format operands
        if operand_fmt is None:
            return mnemonic.upper()

        # Get operand bytes
        operands = inst_bytes[1:size]

        # Handle specific operand formats
        if operand_fmt == 'A':
            return f"{mnemonic.upper()} A"
        elif operand_fmt == 'C':
            return f"{mnemonic.upper()} C"
        elif operand_fmt == 'AB':
            return f"{mnemonic.upper()} AB"
        elif operand_fmt == 'DPTR':
            return f"{mnemonic.upper()} DPTR"
        elif operand_fmt == '@A+DPTR':
            return f"{mnemonic.upper()} @A+DPTR"
        elif operand_fmt == '@A+PC':
            return f"{mnemonic.upper()} @A+PC"

        # Register operands (no extra bytes)
        elif operand_fmt in ('R0', 'R1', 'R2', 'R3', 'R4', 'R5', 'R6', 'R7',
                             '@R0', '@R1', 'A,@R0', 'A,@R1', '@R0,A', '@R1,A'):
            return f"{mnemonic.upper()} {operand_fmt.upper()}"

        # DPTR with immediate 16-bit
        elif operand_fmt == 'DPTR,#data16':
            val = (operands[0] << 8) | operands[1]
            return f"{mnemonic.upper()} DPTR,#{val:04X}"

        # A with immediate byte
        elif operand_fmt == 'A,#data':
            return f"{mnemonic.upper()} A,#{operands[0]:02X}"
        elif operand_fmt == 'A,direct':
            return f"{mnemonic.upper()} A,{operands[0]:02X}h"

        # Direct with various operands
        elif operand_fmt == 'direct':
            return f"{mnemonic.upper()} {operands[0]:02X}h"
        elif operand_fmt == 'direct,A':
            return f"{mnemonic.upper()} {operands[0]:02X}h,A"
        elif operand_fmt == 'direct,#data':
            return f"{mnemonic.upper()} {operands[0]:02X}h,#{operands[1]:02X}"
        elif operand_fmt == 'direct,direct':
            return f"{mnemonic.upper()} {operands[0]:02X}h,{operands[1]:02X}h"
        elif operand_fmt.startswith('direct,R'):
            reg = operand_fmt.split(',')[1]
            return f"{mnemonic.upper()} {operands[0]:02X}h,{reg}"
        elif operand_fmt.startswith('direct,@R'):
            reg = operand_fmt.split(',')[1]
            return f"{mnemonic.upper()} {operands[0]:02X}h,{reg}"
        elif operand_fmt == 'direct,rel':
            rel = operands[1] if operands[1] < 128 else operands[1] - 256
            return f"{mnemonic.upper()} {operands[0]:02X}h,{rel:+d}"

        # Register with immediate or direct
        elif ',' in operand_fmt and operand_fmt.startswith(('R', '@R')):
            reg, rest = operand_fmt.split(',', 1)
            if rest == '#data':
                return f"{mnemonic.upper()} {reg},#{operands[0]:02X}"
            elif rest == 'direct':
                return f"{mnemonic.upper()} {reg},{operands[0]:02X}h"
            elif rest == 'rel':
                rel = operands[0] if operands[0] < 128 else operands[0] - 256
                return f"{mnemonic.upper()} {reg},{rel:+d}"

        # Addresses
        elif operand_fmt == 'addr16':
            addr = (operands[0] << 8) | operands[1]
            return f"{mnemonic.upper()} {addr:04X}h"
        elif operand_fmt == 'addr11':
            high_bits = (opcode >> 5) & 0x07
            addr = (high_bits << 8) | operands[0]
            return f"{mnemonic.upper()} {addr:03X}h"

        # Relative jumps
        elif operand_fmt == 'rel':
            rel = operands[0] if operands[0] < 128 else operands[0] - 256
            return f"{mnemonic.upper()} {rel:+d}"

        # Bit operations
        elif operand_fmt == 'bit':
            return f"{mnemonic.upper()} {operands[0]:02X}h"
        elif operand_fmt == 'bit,C':
            return f"{mnemonic.upper()} {operands[0]:02X}h,C"
        elif operand_fmt == 'C,bit':
            return f"{mnemonic.upper()} C,{operands[0]:02X}h"
        elif operand_fmt == 'C,/bit':
            return f"{mnemonic.upper()} C,/{operands[0]:02X}h"
        elif operand_fmt == 'bit,rel':
            rel = operands[1] if operands[1] < 128 else operands[1] - 256
            return f"{mnemonic.upper()} {operands[0]:02X}h,{rel:+d}"

        # CJNE variants
        elif operand_fmt == 'A,#data,rel':
            rel = operands[1] if operands[1] < 128 else operands[1] - 256
            return f"{mnemonic.upper()} A,#{operands[0]:02X},{rel:+d}"
        elif operand_fmt == 'A,direct,rel':
            rel = operands[1] if operands[1] < 128 else operands[1] - 256
            return f"{mnemonic.upper()} A,{operands[0]:02X}h,{rel:+d}"
        elif operand_fmt.endswith(',#data,rel'):
            reg = operand_fmt.split(',')[0]
            rel = operands[1] if operands[1] < 128 else operands[1] - 256
            return f"{mnemonic.upper()} {reg},#{operands[0]:02X},{rel:+d}"

        # Default: just show mnemonic with hex operands
        operand_str = ','.join(f"{b:02X}h" for b in operands)
        return f"{mnemonic.upper()} {operand_str}"

    def dump_state(self):
        """Print current CPU and memory state."""
        print("\n=== CPU State ===")
        print(f"PC: 0x{self.cpu.pc:04X}  Bank: {self.memory.read_sfr(0x96) & 1}")
        print(f"A:  0x{self.cpu.A:02X}  B: 0x{self.cpu.B:02X}")
        print(f"PSW: 0x{self.cpu.PSW:02X} (CY={int(self.cpu.CY)} AC={int(self.cpu.AC)} OV={int(self.cpu.OV)})")
        print(f"SP: 0x{self.cpu.SP:02X}  DPTR: 0x{self.cpu.DPTR:04X}")
        print(f"Cycles: {self.cpu.cycles}  Instructions: {self.inst_count}")

        # Register banks
        print("\nRegisters:")
        for bank in range(4):
            regs = [self.memory.read_idata(bank * 8 + i) for i in range(8)]
            print(f"  Bank {bank}: " + ' '.join(f'{r:02X}' for r in regs))

        # Stack
        sp = self.cpu.SP
        if sp > 0x07:
            print(f"\nStack (SP=0x{sp:02X}):")
            for i in range(min(8, sp - 0x07)):
                addr = sp - i
                print(f"  0x{addr:02X}: 0x{self.memory.read_idata(addr):02X}")

    def dump_trace_stats(self):
        """Print trace PC hit statistics."""
        if self.trace_pc_hits:
            print("\n=== Trace PC Hits ===")
            for pc in sorted(self.trace_pc_hits.keys()):
                count = self.trace_pc_hits[pc]
                print(f"  0x{pc:04X}: {count} hits")

    def dump_xdata(self, start: int, length: int = 16):
        """Dump XDATA region."""
        print(f"\n=== XDATA 0x{start:04X}-0x{start+length-1:04X} ===")
        for offset in range(0, length, 16):
            addr = start + offset
            hex_str = ' '.join(f'{self.memory.xdata[addr+i]:02X}' for i in range(min(16, length - offset)))
            print(f"  0x{addr:04X}: {hex_str}")

    def dump_registers(self, addrs: list):
        """Dump specific hardware register values."""
        print("\n=== Hardware Registers ===")
        for addr in addrs:
            val = self.hw.regs.get(addr, 0)
            print(f"  0x{addr:04X}: 0x{val:02X}")


def main():
    parser = argparse.ArgumentParser(
        description='ASM2464PD Firmware Emulator',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic run with UART output
  python emu.py fw.bin

  # Trace specific addresses (PD task entry points)
  python emu.py --trace-pc 0x9335 --trace-pc 0xAE89 --trace-pc 0xAF5E

  # Watch memory addresses
  python emu.py --watch 0x0A9D --watch 0x0AE1

  # Debug with hardware logging
  python emu.py --log-hw --trace-pc 0x0322
"""
    )
    parser.add_argument('firmware', nargs='?', default='fw.bin',
                        help='Firmware binary to load (default: fw.bin)')
    parser.add_argument('--trace', '-t', action='store_true',
                        help='Enable full instruction tracing (verbose)')
    parser.add_argument('--trace-pc', action='append', default=[],
                        help='Trace when PC hits address (hex), can repeat')
    parser.add_argument('--watch', '-w', action='append', default=[],
                        help='Watch XDATA address for reads/writes (hex), can repeat')
    parser.add_argument('--break', '-b', dest='breakpoints', action='append',
                        default=[], help='Set breakpoint at address (hex)')
    parser.add_argument('--max-cycles', '-c', type=int, default=10000000,
                        help='Maximum cycles to run (default: 10M)')
    parser.add_argument('--max-inst', '-i', type=int, default=None,
                        help='Maximum instructions to run')
    parser.add_argument('--dump', '-d', action='store_true',
                        help='Dump state on exit')
    parser.add_argument('--log-hw', '-l', action='store_true',
                        help='Log hardware MMIO access')
    parser.add_argument('--no-uart-log', action='store_true',
                        help='Disable UART TX logging (show raw output instead)')
    parser.add_argument('--usb-delay', type=int, default=200000,
                        help='Cycles before USB plug-in event (default: 200000, after init)')
    parser.add_argument('--pd-event', type=str, default=None,
                        choices=['source_cap', 'accept', 'ps_rdy', 'vdm'],
                        help='Trigger PD event at USB connect')
    parser.add_argument('--usb-cmd', type=str, default=None,
                        help='Inject USB command: E4:addr:size (read) or E5:addr:value (write)')
    parser.add_argument('--usb-cmd-delay', type=int, default=1000,
                        help='Cycles after USB connect to inject command (default: 1000)')

    args = parser.parse_args()

    # Find firmware file
    fw_path = args.firmware
    if not os.path.exists(fw_path):
        # Try relative to script directory
        script_dir = Path(__file__).parent.parent
        fw_path = script_dir / args.firmware
        if not fw_path.exists():
            print(f"Error: Cannot find firmware file: {args.firmware}")
            sys.exit(1)

    # Create emulator
    emu = Emulator(trace=args.trace, log_hw=args.log_hw,
                   log_uart=not args.no_uart_log, usb_delay=args.usb_delay)

    # Load firmware
    emu.load_firmware(str(fw_path))

    # Set breakpoints
    for bp in args.breakpoints:
        addr = int(bp, 16)
        emu.cpu.breakpoints.add(addr)
        print(f"Breakpoint set at 0x{addr:04X}")

    # Set trace PC addresses
    for pc_str in args.trace_pc:
        addr = int(pc_str, 16)
        emu.trace_pcs.add(addr)
        print(f"Tracing PC at 0x{addr:04X}")

    # Set watch addresses
    for watch_str in args.watch:
        addr = int(watch_str, 16)
        emu.setup_watch(addr)
        print(f"Watching XDATA at 0x{addr:04X}")

    # Configure PD event if requested
    if args.pd_event:
        pd_events = {
            'source_cap': (0x01, 0x00),   # Source Capabilities
            'accept': (0x03, 0x00),        # Accept
            'ps_rdy': (0x06, 0x00),        # PS_RDY
            'vdm': (0x0F, 0x00),           # VDM
        }
        e40f, e410 = pd_events[args.pd_event]
        emu.hw.regs[0xE40F] = e40f
        emu.hw.regs[0xE410] = e410
        print(f"PD event configured: {args.pd_event} (E40F=0x{e40f:02X}, E410=0x{e410:02X})")

    # Configure USB command injection if requested
    if args.usb_cmd:
        parts = args.usb_cmd.upper().split(':')
        cmd_type = int(parts[0], 16)  # 0xE4 or 0xE5
        addr = int(parts[1], 16)       # XDATA address
        val_or_size = int(parts[2], 16) if len(parts) > 2 else 1  # Value or size

        emu.hw.usb_inject_cmd = (cmd_type, addr, val_or_size)
        emu.hw.usb_inject_delay = args.usb_cmd_delay
        print(f"USB command injection configured: 0x{cmd_type:02X} addr=0x{addr:04X} param=0x{val_or_size:02X}")

    # Reset and run
    emu.reset()
    print(f"Starting execution at PC=0x{emu.cpu.pc:04X}")
    print("-" * 60)

    try:
        reason = emu.run(max_cycles=args.max_cycles, max_instructions=args.max_inst)
        print("-" * 60)
        print(f"Stopped: {reason} at PC=0x{emu.cpu.pc:04X}")
    except KeyboardInterrupt:
        print("\n" + "-" * 60)
        print(f"Interrupted at PC=0x{emu.cpu.pc:04X}")
    except Exception as e:
        print(f"\nError at PC=0x{emu.cpu.pc:04X}: {e}")
        raise

    if args.dump:
        emu.dump_state()

    # Always show trace stats if any PCs were traced
    emu.dump_trace_stats()

    print(f"\nTotal: {emu.inst_count} instructions, {emu.cpu.cycles} cycles")


if __name__ == '__main__':
    main()
