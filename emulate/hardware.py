"""
ASM2464PD Hardware Emulation

This module provides realistic hardware emulation for the ASM2464PD,
handling polling loops, status registers, and peripheral state machines.
"""

from typing import TYPE_CHECKING, Dict, Set, Optional, Callable
from dataclasses import dataclass, field
import struct

if TYPE_CHECKING:
    from memory import Memory


@dataclass
class HardwareState:
    """
    Complete hardware state for ASM2464PD emulation.

    This class tracks the state of all hardware peripherals and provides
    realistic responses to firmware polling.
    """

    # Logging
    log_reads: bool = False
    log_writes: bool = False
    logged_addrs: Set[int] = field(default_factory=set)

    # Cycle counter for timing-based responses
    cycles: int = 0

    # Hardware initialization stages
    init_stage: int = 0

    # PCIe/USB link state
    pcie_link_up: bool = False
    usb_connected: bool = False

    # Polling counters - track how many times an address is polled
    poll_counts: Dict[int, int] = field(default_factory=dict)

    # Auto-ready threshold - after N polls, return ready
    auto_ready_threshold: int = 10

    # Register values - direct storage for MMIO
    regs: Dict[int, int] = field(default_factory=dict)

    # Callbacks for specific addresses
    read_callbacks: Dict[int, Callable[['HardwareState', int], int]] = field(default_factory=dict)
    write_callbacks: Dict[int, Callable[['HardwareState', int, int], None]] = field(default_factory=dict)

    def __post_init__(self):
        """Initialize default register values."""
        self._init_defaults()
        self._setup_callbacks()

    def _init_defaults(self):
        """Set default register values for hardware ready state."""
        # Power management - powered on
        self.regs[0x92C0] = 0x81  # Power enable
        self.regs[0x92C1] = 0x03  # Clocks enabled
        self.regs[0x92C5] = 0x04  # PHY powered
        self.regs[0x92E0] = 0x02  # Power domain

        # USB controller - connected
        self.regs[0x9000] = 0x80  # USB connected
        self.regs[0x90E0] = 0x02  # USB3 speed
        self.regs[0x9105] = 0xFF  # PHY active

        # NVMe controller - ready
        self.regs[0xC412] = 0x02  # NVMe ready
        self.regs[0xC520] = 0x80  # Link ready

        # PCIe - link up
        self.regs[0xB296] = 0x02  # Complete status
        self.regs[0xB401] = 0x01  # Tunnel enabled
        self.regs[0xB480] = 0x01  # Link up

        # DMA - ready
        self.regs[0xC8D6] = 0x04  # DMA done

        # Flash - not busy
        self.regs[0xC8A9] = 0x00  # CSR - not busy
        self.regs[0xC8B8] = 0x00  # Flash/DMA status - bit 0 clear (not busy)

        # UART - TX ready
        self.regs[0xC009] = 0x60  # LSR - TX empty

        # Timer CSRs - not expired
        self.regs[0xCC11] = 0x00
        self.regs[0xCC17] = 0x00
        self.regs[0xCC1D] = 0x00
        self.regs[0xCC23] = 0x00

        # CPU/System - ready states
        self.regs[0xCC33] = 0x04  # CPU exec status

        # Bank-selected registers (0x12xx)
        # These are PCIe extended registers accessed through bank switching
        self.regs[0x1235] = 0x00
        # 0x1238: JB ACC.0 loops WHILE bit 0 set, so 0x00 = ready (bit 0 clear)
        self.regs[0x1238] = 0x00

        # Interrupt status - no pending
        self.regs[0xC800] = 0x00
        self.regs[0xC802] = 0x00
        self.regs[0xC806] = 0x00
        # PCIe/NVMe interrupt status - set bit 6 to trigger UART debug output
        self.regs[0xC80A] = 0x40  # Bit 6 set for UART debug output

        # Various status registers that get polled
        self.regs[0xE795] = 0x01  # Flash ready
        self.regs[0xE7E3] = 0x80  # PHY link ready
        self.regs[0xB238] = 0x00  # Link trigger not busy

        # PHY Extended registers
        self.regs[0xC6B3] = 0x30  # PHY status - bits 4,5 set (main loop waits for these)

        # Debug registers - force debug output for UART testing
        # REG_CMD_CTRL_E40F bit 7: trigger debug output handler
        self.regs[0xE40F] = 0x80  # Bit 7 set to trigger debug_output_handler

        # REG_CMD_CTRL_E410: debug status auxiliary
        self.regs[0xE410] = 0x00  # No secondary debug flags

    def _setup_callbacks(self):
        """Setup special read/write callbacks."""
        # UART TX - capture output
        self.write_callbacks[0xC000] = self._uart_tx
        self.write_callbacks[0xC001] = self._uart_tx

        # PCIe status - auto-complete after trigger
        self.read_callbacks[0xB296] = self._pcie_status_read
        self.write_callbacks[0xB254] = self._pcie_trigger_write
        self.write_callbacks[0xB296] = self._pcie_status_write

        # Flash CSR - auto-complete
        self.read_callbacks[0xC8A9] = self._flash_csr_read
        self.write_callbacks[0xC8AA] = self._flash_cmd_write

        # DMA status - auto-complete
        self.read_callbacks[0xC8D6] = self._dma_status_read

        # Flash/DMA busy registers - auto-clear after polling
        self.read_callbacks[0xC8B8] = self._busy_reg_read

        # System interrupt status - clear bit 0 after read (timer event)
        self.read_callbacks[0xC806] = self._int_system_read

        # Timer CSRs
        for addr in [0xCC11, 0xCC17, 0xCC1D, 0xCC23]:
            self.read_callbacks[addr] = self._timer_csr_read
            self.write_callbacks[addr] = self._timer_csr_write

        # Bank-selected trigger registers (0x12xx) - auto-clear bit 0 on read
        # Firmware writes trigger value, then polls until hardware clears it
        for addr in range(0x1200, 0x1300):
            self.read_callbacks[addr] = self._bank_reg_read
            self.write_callbacks[addr] = self._bank_reg_write

    def _uart_tx(self, hw: 'HardwareState', addr: int, value: int):
        """Handle UART transmit."""
        # Echo to stdout
        try:
            ch = chr(value) if 0x20 <= value < 0x7F or value in (0x0A, 0x0D) else '.'
            print(ch, end='', flush=True)
        except:
            pass

    def _int_system_read(self, hw: 'HardwareState', addr: int) -> int:
        """Handle system interrupt status read."""
        value = self.regs.get(addr, 0)

        # Clear bit 0 (timer event) after being read
        # This simulates the interrupt controller clearing the flag
        if value & 0x01:
            self.regs[addr] = value & ~0x01

        return value

    def _pcie_status_read(self, hw: 'HardwareState', addr: int) -> int:
        """PCIe status - return complete after trigger."""
        return 0x02  # Always complete

    def _pcie_trigger_write(self, hw: 'HardwareState', addr: int, value: int):
        """PCIe trigger - set complete status."""
        self.regs[0xB296] = 0x02

    def _pcie_status_write(self, hw: 'HardwareState', addr: int, value: int):
        """PCIe status write - clear bits."""
        self.regs[addr] &= ~value

    def _flash_csr_read(self, hw: 'HardwareState', addr: int) -> int:
        """Flash CSR - always not busy."""
        return 0x00

    def _flash_cmd_write(self, hw: 'HardwareState', addr: int, value: int):
        """Flash command - immediate complete."""
        self.regs[0xC8A9] = 0x00  # Clear busy

    def _dma_status_read(self, hw: 'HardwareState', addr: int) -> int:
        """DMA status - always done."""
        return 0x04  # Done flag

    def _busy_reg_read(self, hw: 'HardwareState', addr: int) -> int:
        """
        Busy register read - auto-clear bit 0 after polling.

        Pattern: firmware writes 1 to start operation, polls until bit 0 clears.
        """
        count = self.poll_counts.get(addr, 0)
        value = self.regs.get(addr, 0)

        # After 3 polls, clear bit 0 (operation complete)
        if count >= 3 and (value & 0x01):
            value &= ~0x01
            self.regs[addr] = value

        return value

    def _timer_csr_read(self, hw: 'HardwareState', addr: int) -> int:
        """Timer CSR read - auto-expire after polling."""
        count = self.poll_counts.get(addr, 0)
        value = self.regs.get(addr, 0)

        # If timer is enabled (bit 0) and we've polled enough, set expired (bit 1)
        if (value & 0x01) and count >= 3:
            value |= 0x02  # Set expired bit
            self.regs[addr] = value

        return value

    def _timer_csr_write(self, hw: 'HardwareState', addr: int, value: int):
        """Timer CSR write - handle clear and reset poll count."""
        if value & 0x04:  # Clear flag
            value &= ~0x02  # Clear expired bit
        self.regs[addr] = value
        self.poll_counts[addr] = 0  # Reset poll count

    def _bank_reg_read(self, hw: 'HardwareState', addr: int) -> int:
        """
        Bank register read - auto-clear trigger bits after a few polls.

        This implements the "write trigger, poll until clear" pattern.
        Hardware clears trigger bits after operation completes.
        """
        count = self.poll_counts.get(addr, 0)
        value = self.regs.get(addr, 0)

        # After 3 polls, clear bit 0 (trigger complete)
        if count >= 3 and (value & 0x01):
            value &= ~0x01
            self.regs[addr] = value

        return value

    def _bank_reg_write(self, hw: 'HardwareState', addr: int, value: int):
        """Bank register write - store value and reset poll count."""
        self.regs[addr] = value
        # Reset poll count when written (new trigger)
        self.poll_counts[addr] = 0

    def read(self, addr: int) -> int:
        """Read from hardware register."""
        addr &= 0xFFFF

        # Track polling
        self.poll_counts[addr] = self.poll_counts.get(addr, 0) + 1

        # Check for callback
        if addr in self.read_callbacks:
            value = self.read_callbacks[addr](self, addr)
        elif addr in self.regs:
            value = self.regs[addr]
        else:
            # Unknown register - return 0 by default, or auto-ready after threshold
            if self.poll_counts[addr] >= self.auto_ready_threshold:
                value = 0xFF  # Return all-ones to satisfy most ready checks
            else:
                value = 0x00

        if self.log_reads and addr not in self.logged_addrs:
            print(f"[HW] Read  0x{addr:04X} = 0x{value:02X} (poll #{self.poll_counts[addr]})")
            if self.poll_counts[addr] >= 5:
                self.logged_addrs.add(addr)  # Stop logging after 5 polls

        return value

    def write(self, addr: int, value: int):
        """Write to hardware register."""
        addr &= 0xFFFF
        value &= 0xFF

        if self.log_writes:
            print(f"[HW] Write 0x{addr:04X} = 0x{value:02X}")

        # Check for callback
        if addr in self.write_callbacks:
            self.write_callbacks[addr](self, addr, value)
        else:
            self.regs[addr] = value

    def tick(self, cycles: int, cpu=None):
        """Advance hardware state by cycles."""
        self.cycles += cycles

        # Advance init stage after enough cycles
        if self.init_stage == 0 and self.cycles > 1000:
            self.init_stage = 1
            self.pcie_link_up = True
            self.regs[0x1238] = 0x01  # Link ready

        if self.init_stage == 1 and self.cycles > 5000:
            self.init_stage = 2
            self.usb_connected = True

        # Simulate timer interrupts every ~1000 cycles
        if self.cycles % 1000 == 0 and self.cycles > 0:
            # Set bit 0 in system interrupt status register (timer event)
            self.regs[0xC806] |= 0x01

            # Trigger Timer 0 interrupt in CPU if connected
            if cpu and hasattr(cpu, '_timer0_pending'):
                cpu._timer0_pending = True


def create_hardware_hooks(memory: 'Memory', hw: HardwareState):
    """
    Register hardware hooks with memory system.

    This replaces the simple peripheral stubs with full hardware emulation.
    """

    # MMIO regions that need hardware emulation
    mmio_ranges = [
        (0x1200, 0x1300),   # Bank-selected registers
        (0x9000, 0x9400),   # USB Interface
        (0x92C0, 0x9300),   # Power Management
        (0xB200, 0xB900),   # PCIe Passthrough
        (0xC000, 0xC100),   # UART
        (0xC200, 0xC300),   # Link/PHY Control
        (0xC400, 0xC600),   # NVMe Interface
        (0xC600, 0xC700),   # PHY Extended
        (0xC800, 0xC900),   # Interrupt/DMA/Flash
        (0xCA00, 0xCB00),   # CPU Mode
        (0xCC00, 0xCD00),   # Timer/CPU Control
        (0xCE00, 0xCF00),   # SCSI DMA
        (0xE300, 0xE400),   # PHY Debug
        (0xE400, 0xE500),   # Command Engine
        (0xE600, 0xE700),   # Debug/Interrupt Extended
        (0xE700, 0xE800),   # System Status
        (0xEC00, 0xED00),   # NVMe Event
    ]

    def make_read_hook(hw_ref):
        def hook(addr):
            return hw_ref.read(addr)
        return hook

    def make_write_hook(hw_ref):
        def hook(addr, value):
            hw_ref.write(addr, value)
        return hook

    read_hook = make_read_hook(hw)
    write_hook = make_write_hook(hw)

    for start, end in mmio_ranges:
        for addr in range(start, end):
            memory.xdata_read_hooks[addr] = read_hook
            memory.xdata_write_hooks[addr] = write_hook


# Known polling addresses and their expected ready values
POLLING_READY_VALUES = {
    0x1238: 0x00,  # PCIe link ready (bit 0 CLEAR = ready)
    0x9000: 0x80,  # USB connected (bit 7)
    0x9105: 0xFF,  # PHY active
    0xB238: 0x00,  # Link trigger not busy
    0xB296: 0x02,  # PCIe complete
    0xC412: 0x02,  # NVMe ready
    0xC520: 0x80,  # NVMe link ready
    0xC8A9: 0x00,  # Flash not busy
    0xC8D6: 0x04,  # DMA done
    0xCC33: 0x04,  # CPU exec status
    0xE795: 0x01,  # Flash ready
    0xE7E3: 0x80,  # PHY link ready
}
