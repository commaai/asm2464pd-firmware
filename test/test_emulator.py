#!/usr/bin/env python3
"""
Test emulator functionality for the ASM2464PD firmware.

These tests verify:
- UART TX register writes produce output
- Hardware state management works correctly
- Basic CPU execution functions
- PCIe and timer emulation behavior

Usage:
    pytest test/test_emulator.py -v
"""

import sys
import os
import io
from pathlib import Path
import pytest

# Add emulate directory to path
sys.path.insert(0, str(Path(__file__).parent.parent / 'emulate'))

from emu import Emulator


class TestUARTOutput:
    """Tests for UART output functionality."""

    def test_direct_uart_write_raw(self):
        """Test direct UART register writes with raw output mode."""
        # Create emulator with log_uart=False for raw character output
        emu = Emulator(log_uart=False)
        emu.reset()

        # Capture stdout
        old_stdout = sys.stdout
        captured = io.StringIO()
        sys.stdout = captured

        try:
            # Write test message directly to UART THR (0xC001)
            test_msg = "TEST"
            for ch in test_msg:
                emu.hw.write(0xC001, ord(ch))
        finally:
            sys.stdout = old_stdout

        output = captured.getvalue()
        assert test_msg in output, f"Expected '{test_msg}' in output, got: {repr(output)}"

    def test_uart_output_formatting(self):
        """Test that UART output is properly buffered and formatted when log_uart=True."""
        emu = Emulator(log_uart=True)
        emu.reset()

        # Capture stdout
        old_stdout = sys.stdout
        captured = io.StringIO()
        sys.stdout = captured

        try:
            # Write a message that ends with ']' which triggers flush
            test_chars = "Hello]"
            for ch in test_chars:
                emu.hw.write(0xC001, ord(ch))
        finally:
            sys.stdout = old_stdout

        output = captured.getvalue()
        # With log_uart=True, output should contain [UART] prefix
        assert "[UART]" in output, f"Expected '[UART]' prefix in output, got: {repr(output)}"
        assert "Hello]" in output, f"Expected 'Hello]' in output, got: {repr(output)}"

    def test_uart_newline_handling(self):
        """Test that newlines properly flush the UART buffer."""
        emu = Emulator(log_uart=True)
        emu.reset()

        old_stdout = sys.stdout
        captured = io.StringIO()
        sys.stdout = captured

        try:
            # Write message followed by newline
            for ch in "Line1":
                emu.hw.write(0xC001, ord(ch))
            emu.hw.write(0xC001, 0x0A)  # newline
        finally:
            sys.stdout = old_stdout

        output = captured.getvalue()
        assert "Line1" in output, f"Expected 'Line1' in output, got: {repr(output)}"


class TestHardwareState:
    """Tests for hardware state management."""

    def test_hardware_register_defaults(self):
        """Test that hardware registers have correct default values."""
        emu = Emulator()
        emu.reset()

        # Check critical register defaults
        assert emu.hw.regs.get(0xC009, 0) == 0x60, "UART LSR should be 0x60 (TX empty)"
        assert emu.hw.regs.get(0x9000, 0) == 0x00, "USB status should start at 0x00"
        assert emu.hw.regs.get(0xB480, 0) == 0x00, "PCIe link should start down"

    def test_usb_connect_event(self):
        """Test that USB connect event fires after delay."""
        emu = Emulator(usb_delay=100)  # Short delay for testing
        emu.reset()

        assert not emu.hw.usb_connected, "USB should not be connected initially"

        # Tick past the connect delay
        for _ in range(150):
            emu.hw.tick(1, emu.cpu)

        assert emu.hw.usb_connected, "USB should be connected after delay"
        assert emu.hw.regs.get(0x9000, 0) & 0x80, "USB status bit 7 should be set"

    def test_polling_counters(self):
        """Test that polling counters increment on repeated reads."""
        emu = Emulator()
        emu.reset()

        test_addr = 0xC800
        emu.hw.regs[test_addr] = 0x00

        # Read multiple times
        for _ in range(5):
            emu.hw.read(test_addr)

        assert emu.hw.poll_counts.get(test_addr, 0) >= 5, "Poll count should increment"


class TestEmulatorExecution:
    """Tests for basic emulator execution."""

    def test_emulator_reset(self):
        """Test that emulator resets to clean state."""
        emu = Emulator()
        emu.reset()

        assert emu.cpu.pc == 0x0000, "PC should be 0 after reset"
        assert emu.inst_count == 0, "Instruction count should be 0"
        assert emu.cpu.cycles == 0, "Cycle count should be 0"

    def test_memory_read_write(self):
        """Test basic XDATA memory operations."""
        emu = Emulator()
        emu.reset()

        # Test XDATA write and read (low memory, not hardware registers)
        test_addr = 0x1000
        test_value = 0x42

        emu.memory.write_xdata(test_addr, test_value)
        result = emu.memory.read_xdata(test_addr)

        assert result == test_value, f"Expected 0x{test_value:02X}, got 0x{result:02X}"

    def test_idata_memory(self):
        """Test IDATA (internal RAM) operations."""
        emu = Emulator()
        emu.reset()

        # Test IDATA write and read
        test_addr = 0x30
        test_value = 0x55

        emu.memory.write_idata(test_addr, test_value)
        result = emu.memory.read_idata(test_addr)

        assert result == test_value, f"Expected 0x{test_value:02X}, got 0x{result:02X}"

    def test_sfr_operations(self):
        """Test SFR (Special Function Register) operations."""
        emu = Emulator()
        emu.reset()

        # Test SP (Stack Pointer) - SFR 0x81
        emu.memory.write_sfr(0x81, 0x50)
        result = emu.memory.read_sfr(0x81)

        assert result == 0x50, f"Expected SP=0x50, got 0x{result:02X}"

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_firmware_load(self):
        """Test that firmware loads correctly."""
        emu = Emulator()
        emu.reset()

        fw_path = Path(__file__).parent.parent / 'fw.bin'
        emu.load_firmware(str(fw_path))

        # Check that code memory has data
        first_byte = emu.memory.read_code(0x0000)
        assert first_byte != 0x00 or emu.memory.read_code(0x0001) != 0x00, \
            "Firmware should have non-zero bytes at start"

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_firmware_execution_cycles(self):
        """Test that firmware executes for specified cycles."""
        emu = Emulator()
        emu.reset()

        fw_path = Path(__file__).parent.parent / 'fw.bin'
        emu.load_firmware(str(fw_path))

        # Run for a limited number of cycles
        max_cycles = 1000
        reason = emu.run(max_cycles=max_cycles)

        assert reason == "max_cycles", f"Expected stop reason 'max_cycles', got '{reason}'"
        assert emu.cpu.cycles >= max_cycles, f"Should have run at least {max_cycles} cycles"


class TestPCIeEmulation:
    """Tests for PCIe hardware emulation."""

    def test_pcie_trigger_completion(self):
        """Test that PCIe trigger sets completion bits."""
        emu = Emulator()
        emu.reset()

        # Trigger PCIe operation
        emu.hw.write(0xB254, 0x01)

        # Check completion status
        status = emu.hw.read(0xB296)
        assert status & 0x06, "PCIe completion bits should be set after trigger"

    def test_pcie_status_polling(self):
        """Test that PCIe status sets bits after polling."""
        emu = Emulator()
        emu.reset()

        # Initial read should have bits clear or not all set
        initial = emu.hw.read(0xB296)

        # Poll multiple times
        for _ in range(10):
            status = emu.hw.read(0xB296)

        # After polling, completion bits should be set
        assert status & 0x06, "PCIe completion bits should be set after polling"


class TestUSBVendorCommands:
    """Tests for USB vendor command emulation."""

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_e4_read_xdata(self):
        """Test E4 read command returns correct XDATA values."""
        emu = Emulator(log_uart=False, usb_delay=1000)
        emu.load_firmware(str(Path(__file__).parent.parent / 'fw.bin'))
        emu.reset()

        # Write test data to XDATA
        test_addr = 0x1000
        test_data = [0xDE, 0xAD, 0xBE, 0xEF]
        for i, val in enumerate(test_data):
            emu.memory.xdata[test_addr + i] = val

        # Inject E4 read command
        emu.hw.inject_usb_command(0xE4, test_addr, size=len(test_data))

        # Run until DMA completes
        emu.run(max_cycles=50000)

        # Verify data was copied to USB buffer at 0x8000
        result = [emu.memory.xdata[0x8000 + i] for i in range(len(test_data))]
        assert result == test_data, f"E4 read returned {result}, expected {test_data}"

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_e4_read_different_addresses(self):
        """Test E4 read works for various XDATA addresses."""
        emu = Emulator(log_uart=False, usb_delay=1000)
        emu.load_firmware(str(Path(__file__).parent.parent / 'fw.bin'))

        test_cases = [
            (0x0100, [0x11, 0x22]),
            (0x2000, [0xAA, 0xBB, 0xCC, 0xDD]),
            (0x5000, [0x01]),
        ]

        for addr, data in test_cases:
            emu.reset()

            # Write test data
            for i, val in enumerate(data):
                emu.memory.xdata[addr + i] = val

            # Inject E4 read command
            emu.hw.inject_usb_command(0xE4, addr, size=len(data))
            emu.run(max_cycles=50000)

            # Verify result
            result = [emu.memory.xdata[0x8000 + i] for i in range(len(data))]
            assert result == data, f"E4 read at 0x{addr:04X} returned {result}, expected {data}"


class TestTimerEmulation:
    """Tests for timer hardware emulation."""

    def test_timer_csr_ready_bit(self):
        """Test that timer CSR sets ready bit after polling."""
        emu = Emulator()
        emu.reset()

        timer_addr = 0xCC11  # Timer 0 CSR

        # Poll the timer CSR
        for _ in range(5):
            value = emu.hw.read(timer_addr)

        # Ready bit (bit 1) should be set after polling
        assert value & 0x02, "Timer ready bit should be set after polling"

    def test_timer_dma_status(self):
        """Test timer/DMA status register completion."""
        emu = Emulator()
        emu.reset()

        dma_status_addr = 0xCC89

        # Poll the status
        for _ in range(5):
            value = emu.hw.read(dma_status_addr)

        # Complete bit should be set
        assert value & 0x02, "Timer/DMA complete bit should be set after polling"


if __name__ == "__main__":
    pytest.main([__file__, '-v'])
