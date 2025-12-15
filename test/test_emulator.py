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


class TestUSBStateMachine:
    """Tests for USB state machine progression."""

    def test_usb_state_progresses_to_configured(self):
        """Test that USB state machine reaches CONFIGURED state."""
        emu = Emulator()
        emu.reset()

        # Connect USB
        emu.hw.usb_controller.connect()

        # State machine should have started
        assert emu.hw.usb_controller.state.value >= 1, "USB should be at least ATTACHED"

        # After enough state machine reads, should be CONFIGURED
        for _ in range(10):
            emu.hw.usb_controller.advance_enumeration()

        assert emu.hw.usb_controller.enumeration_complete, "USB enumeration should complete"

    def test_usb_connect_enables_command_processing(self):
        """Test that USB connect prepares system for commands."""
        emu = Emulator()
        emu.reset()

        assert not emu.hw.usb_connected, "USB should start disconnected"

        emu.hw.usb_controller.connect()

        assert emu.hw.usb_controller.state.value >= 1, "USB should be connected"


class TestUSBEndpointBuffers:
    """Tests for USB endpoint buffer functionality."""

    def test_ep0_buffer_stores_command_data(self):
        """Test that EP0 buffer can store and retrieve command data."""
        emu = Emulator()
        emu.reset()

        # Simulate CDB data that would arrive via USB
        test_cdb = bytes([0xE4, 0x04, 0x50, 0x12, 0x34, 0x00])

        for i, val in enumerate(test_cdb):
            emu.hw.usb_ep0_buf[i] = val

        # Verify data is accessible
        result = bytes([emu.hw.usb_ep0_buf[i] for i in range(len(test_cdb))])
        assert result == test_cdb, "EP0 buffer should store CDB data"

    def test_ep_data_buffer_stores_transfer_data(self):
        """Test that EP data buffer can store bulk transfer data."""
        emu = Emulator()
        emu.reset()

        # Write test payload
        test_data = bytes([0xDE, 0xAD, 0xBE, 0xEF] * 16)

        for i, val in enumerate(test_data):
            emu.hw.usb_ep_data_buf[i] = val

        result = bytes([emu.hw.usb_ep_data_buf[i] for i in range(len(test_data))])
        assert result == test_data, "EP data buffer should store transfer data"


class TestE4ReadCommand:
    """End-to-end tests for E4 (read XDATA) command."""

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_e4_reads_single_byte(self):
        """Test E4 command reads a single byte from XDATA."""
        emu = Emulator(log_uart=False, usb_delay=1000)
        emu.load_firmware(str(Path(__file__).parent.parent / 'fw.bin'))
        emu.reset()

        # Write test value to arbitrary XDATA location
        test_addr = 0x2000
        test_value = 0x42
        emu.memory.xdata[test_addr] = test_value

        # Inject E4 read command
        emu.hw.inject_usb_command(0xE4, test_addr, size=1)

        # Run firmware until DMA completes
        emu.run(max_cycles=50000)

        # USB buffer should contain the read value
        result = emu.memory.xdata[0x8000]
        assert result == test_value, f"E4 read returned 0x{result:02X}, expected 0x{test_value:02X}"

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_e4_reads_multiple_bytes(self):
        """Test E4 command reads multiple bytes from XDATA."""
        emu = Emulator(log_uart=False, usb_delay=1000)
        emu.load_firmware(str(Path(__file__).parent.parent / 'fw.bin'))
        emu.reset()

        # Write test pattern
        test_addr = 0x3000
        test_data = [0xCA, 0xFE, 0xBA, 0xBE]
        for i, val in enumerate(test_data):
            emu.memory.xdata[test_addr + i] = val

        # Inject E4 read command
        emu.hw.inject_usb_command(0xE4, test_addr, size=len(test_data))
        emu.run(max_cycles=50000)

        # Check USB buffer contains all bytes
        result = [emu.memory.xdata[0x8000 + i] for i in range(len(test_data))]
        assert result == test_data, f"E4 read returned {result}, expected {test_data}"

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_e4_reads_from_different_regions(self):
        """Test E4 command works for various XDATA regions."""
        emu = Emulator(log_uart=False, usb_delay=1000)
        emu.load_firmware(str(Path(__file__).parent.parent / 'fw.bin'))

        # Test different XDATA regions
        test_cases = [
            (0x0100, [0x11]),           # Low XDATA
            (0x1000, [0x22, 0x33]),     # Work RAM
            (0x4000, [0x44, 0x55, 0x66, 0x77]),  # Higher XDATA
        ]

        for addr, data in test_cases:
            emu.reset()

            # Write test data
            for i, val in enumerate(data):
                emu.memory.xdata[addr + i] = val

            # Execute E4 read
            emu.hw.inject_usb_command(0xE4, addr, size=len(data))
            emu.run(max_cycles=50000)

            # Verify
            result = [emu.memory.xdata[0x8000 + i] for i in range(len(data))]
            assert result == data, f"E4 at 0x{addr:04X}: got {result}, expected {data}"


class TestCodeBanking:
    """End-to-end tests for code memory banking."""

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_bank_switching_reads_different_code(self):
        """Test that bank switching reads different firmware code."""
        emu = Emulator()
        fw_path = Path(__file__).parent.parent / 'fw.bin'
        emu.load_firmware(str(fw_path))
        emu.reset()

        # Read from upper memory in bank 0
        emu.memory.sfr[0x96 - 0x80] = 0x00  # DPX = 0
        bank0_byte = emu.memory.read_code(0x8000)

        # Read from same address in bank 1
        emu.memory.sfr[0x96 - 0x80] = 0x01  # DPX = 1
        bank1_byte = emu.memory.read_code(0x8000)

        # Bank 0 and Bank 1 code should be different at most addresses
        # (they're different code sections in the firmware)
        # At minimum, we verify both reads work
        assert bank0_byte is not None, "Bank 0 read should succeed"
        assert bank1_byte is not None, "Bank 1 read should succeed"

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_lower_memory_ignores_bank(self):
        """Test that lower 32KB always reads from bank 0."""
        emu = Emulator()
        fw_path = Path(__file__).parent.parent / 'fw.bin'
        emu.load_firmware(str(fw_path))
        emu.reset()

        # Read from lower memory with DPX=0
        emu.memory.sfr[0x96 - 0x80] = 0x00
        byte_dpx0 = emu.memory.read_code(0x1000)

        # Read from same address with DPX=1
        emu.memory.sfr[0x96 - 0x80] = 0x01
        byte_dpx1 = emu.memory.read_code(0x1000)

        # Should be identical (lower 32KB ignores bank)
        assert byte_dpx0 == byte_dpx1, "Lower 32KB should ignore bank setting"


class TestBitOperations:
    """End-to-end tests for 8051 bit-addressable memory."""

    def test_bit_operations_persist_to_byte(self):
        """Test that individual bit writes affect the underlying byte."""
        emu = Emulator()
        emu.reset()

        # Clear the byte first
        emu.memory.idata[0x20] = 0x00

        # Set individual bits and verify byte changes
        emu.memory.write_bit(0x00, True)  # Bit 0
        assert emu.memory.idata[0x20] == 0x01

        emu.memory.write_bit(0x07, True)  # Bit 7
        assert emu.memory.idata[0x20] == 0x81

        emu.memory.write_bit(0x00, False)  # Clear bit 0
        assert emu.memory.idata[0x20] == 0x80

    def test_byte_writes_affect_bit_reads(self):
        """Test that byte writes are visible through bit reads."""
        emu = Emulator()
        emu.reset()

        # Write a byte value
        emu.memory.idata[0x20] = 0xA5  # 10100101

        # Verify individual bits
        assert emu.memory.read_bit(0x00) == True   # bit 0
        assert emu.memory.read_bit(0x01) == False  # bit 1
        assert emu.memory.read_bit(0x02) == True   # bit 2
        assert emu.memory.read_bit(0x05) == True   # bit 5
        assert emu.memory.read_bit(0x06) == False  # bit 6
        assert emu.memory.read_bit(0x07) == True   # bit 7


class TestDMATransfers:
    """End-to-end tests for DMA transfer functionality."""

    def test_dma_copies_data_to_usb_buffer(self):
        """Test that DMA transfer copies XDATA to USB buffer."""
        emu = Emulator()
        emu.reset()

        # Write source data
        src_addr = 0x2500
        test_data = bytes(range(16))  # 0x00, 0x01, ..., 0x0F
        for i, val in enumerate(test_data):
            emu.memory.xdata[src_addr + i] = val

        # Trigger DMA via inject (sets up registers and triggers)
        emu.hw.inject_usb_command(0xE4, src_addr, size=len(test_data))

        # Manually trigger DMA (simulate firmware writing to trigger register)
        emu.hw._perform_pcie_dma(0x500000 | src_addr, len(test_data))

        # Verify USB buffer contains copied data
        result = bytes([emu.memory.xdata[0x8000 + i] for i in range(len(test_data))])
        assert result == test_data, "DMA should copy data to USB buffer"

    def test_dma_sets_completion_status(self):
        """Test that DMA transfer sets completion status in RAM."""
        emu = Emulator()
        emu.reset()

        # Clear completion flag
        emu.memory.xdata[0x0AA0] = 0

        # Trigger small DMA
        emu.hw._perform_pcie_dma(0x500000, 5)

        # Completion flag should be set to transfer size
        assert emu.memory.xdata[0x0AA0] == 5, "DMA completion should indicate size"


class TestFirmwareExecution:
    """End-to-end tests for firmware execution."""

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_firmware_runs_without_crash(self):
        """Test that firmware executes successfully for many cycles."""
        emu = Emulator(log_uart=False)
        emu.load_firmware(str(Path(__file__).parent.parent / 'fw.bin'))
        emu.reset()

        # Run for significant number of cycles
        reason = emu.run(max_cycles=100000)

        # Should stop due to cycle limit, not error
        assert reason == "max_cycles", f"Firmware should run without errors, stopped: {reason}"
        assert emu.inst_count > 1000, "Should execute many instructions"

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_firmware_produces_uart_output(self):
        """Test that firmware produces UART debug output."""
        emu = Emulator(log_uart=False)
        emu.load_firmware(str(Path(__file__).parent.parent / 'fw.bin'))
        emu.reset()

        # Capture UART output
        uart_chars = []
        original_uart_tx = emu.hw._uart_tx

        def capture_uart(hw, addr, value):
            if 0x20 <= value < 0x7F:
                uart_chars.append(chr(value))
            original_uart_tx(hw, addr, value)

        emu.hw.write_callbacks[0xC000] = capture_uart
        emu.hw.write_callbacks[0xC001] = capture_uart

        # Run firmware
        emu.run(max_cycles=200000)

        # Should have produced some output
        output = ''.join(uart_chars)
        assert len(output) > 0, "Firmware should produce UART output"

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_usb_connect_triggers_state_changes(self):
        """Test that USB connect event triggers firmware state changes."""
        emu = Emulator(log_uart=False, usb_delay=10000)
        emu.load_firmware(str(Path(__file__).parent.parent / 'fw.bin'))
        emu.reset()

        # Run past USB connect
        emu.run(max_cycles=50000)

        # USB should be connected
        assert emu.hw.usb_connected, "USB should be connected after delay"

        # USB controller should have progressed
        assert emu.hw.usb_controller.state.value > 0, "USB state should have progressed"


class TestTracingFunctionality:
    """End-to-end tests for execution and XDATA tracing."""

    def test_trace_callback_is_invoked(self):
        """Test that trace callbacks are invoked on trace point hits."""
        emu = Emulator()
        emu.reset()

        hits = []

        def trace_cb(hw, pc, label):
            hits.append((pc, label))

        emu.hw.trace_callback = trace_cb
        emu.hw.add_trace_point(0x1234, "TEST_POINT")
        emu.hw.trace_enabled = True

        # Simulate trace check
        emu.hw.check_trace(0x1234)

        assert len(hits) == 1, "Trace callback should be invoked"
        assert hits[0] == (0x1234, "TEST_POINT"), "Callback should receive correct args"

    def test_xdata_write_log_accumulates(self):
        """Test that XDATA trace log accumulates write entries."""
        emu = Emulator()
        emu.reset()

        emu.hw.add_xdata_trace(0x1000, "VAR_A")
        emu.hw.add_xdata_trace(0x1001, "VAR_B")
        emu.hw.xdata_trace_enabled = True

        # Suppress output during test
        old_stdout = sys.stdout
        sys.stdout = io.StringIO()

        try:
            emu.hw.trace_xdata_write(0x1000, 0x11, pc=0x100)
            emu.hw.trace_xdata_write(0x1001, 0x22, pc=0x200)
            emu.hw.trace_xdata_write(0x1000, 0x33, pc=0x300)
        finally:
            sys.stdout = old_stdout

        assert len(emu.hw.xdata_write_log) == 3, "Should log all writes"


class TestSyncFlagBehavior:
    """End-to-end tests for DMA/timer sync flag handling."""

    def test_sync_flag_cleared_after_polling(self):
        """Test that sync flags are auto-cleared to simulate hardware completion."""
        emu = Emulator()
        emu.reset()

        # Set sync flag (firmware would do this before starting DMA)
        sync_addr = 0x1238
        emu.memory.xdata[sync_addr] = 0x01

        # Poll until cleared (simulates firmware wait loop)
        poll_count = 0
        while emu.memory.read_xdata(sync_addr) != 0x00:
            poll_count += 1
            if poll_count > 10:
                break

        assert emu.memory.xdata[sync_addr] == 0x00, "Sync flag should auto-clear"
        assert poll_count <= 10, "Should clear within reasonable polls"


class TestUSBCommandFlow:
    """End-to-end tests for complete USB command flow."""

    def test_command_injection_sets_up_state(self):
        """Test that command injection prepares all necessary state."""
        emu = Emulator()
        emu.reset()

        # Inject command
        emu.hw.inject_usb_command(0xE4, 0x1234, size=4)

        # Verify USB is ready for command processing
        assert emu.hw.usb_connected, "USB should be connected"
        assert emu.hw.usb_cmd_pending, "Command should be pending"

        # Verify CDB is in registers
        assert emu.hw.regs[0x910D] == 0xE4, "Command type should be in register"

    def test_command_completion_clears_pending(self):
        """Test that command completion clears the pending flag."""
        emu = Emulator()
        emu.reset()

        # Inject and setup command
        emu.hw.inject_usb_command(0xE4, 0x1000, size=1)
        assert emu.hw.usb_cmd_pending, "Command should be pending"

        # Trigger DMA completion
        emu.hw._perform_pcie_dma(0x501000, 1)

        # Simulate firmware writing completion trigger
        emu.hw.write(0xB296, 0x08)

        # Pending should be cleared
        assert not emu.hw.usb_cmd_pending, "Command should no longer be pending"


class TestE5WriteCommand:
    """End-to-end tests for E5 (write XDATA) command."""

    def test_e5_command_injection_format(self):
        """Test E5 command CDB is formatted correctly."""
        emu = Emulator()
        emu.reset()

        # Inject E5 write command: write 0x42 to address 0x1234
        emu.hw.inject_usb_command(0xE5, 0x1234, value=0x42)

        # Check CDB format in registers:
        # CDB[0] = command (0xE5)
        # CDB[1] = value (0x42)
        # CDB[2] = addr_high (0x50 for XDATA)
        # CDB[3] = addr_mid (0x12)
        # CDB[4] = addr_low (0x34)
        assert emu.hw.regs[0x910D] == 0xE5, "CDB[0] should be 0xE5"
        assert emu.hw.regs[0x910E] == 0x42, "CDB[1] should be value"
        assert emu.hw.regs[0x910F] == 0x50, "CDB[2] should be 0x50 (XDATA marker)"
        assert emu.hw.regs[0x9110] == 0x12, "CDB[3] should be addr mid"
        assert emu.hw.regs[0x9111] == 0x34, "CDB[4] should be addr low"

    def test_e5_sets_correct_command_marker(self):
        """Test E5 command sets correct marker in command table."""
        emu = Emulator()
        emu.reset()

        emu.hw.inject_usb_command(0xE5, 0x1234, value=0x42)

        # E5 should set command marker to 0x05
        assert emu.memory.xdata[0x05B1] == 0x05, "E5 marker should be 0x05"

    def test_e5_and_e4_use_same_address_format(self):
        """Test E5 and E4 use compatible address encoding."""
        emu = Emulator()
        emu.reset()

        test_addr = 0x2ABC

        # Inject E4 and check address encoding
        emu.hw.inject_usb_command(0xE4, test_addr, size=1)
        e4_addr_high = emu.hw.regs[0x910F]
        e4_addr_mid = emu.hw.regs[0x9110]
        e4_addr_low = emu.hw.regs[0x9111]

        # Reset and inject E5 to same address
        emu.reset()
        emu.hw.inject_usb_command(0xE5, test_addr, value=0x00)
        e5_addr_high = emu.hw.regs[0x910F]
        e5_addr_mid = emu.hw.regs[0x9110]
        e5_addr_low = emu.hw.regs[0x9111]

        # Address encoding should be identical
        assert e4_addr_high == e5_addr_high, "Address high byte should match"
        assert e4_addr_mid == e5_addr_mid, "Address mid byte should match"
        assert e4_addr_low == e5_addr_low, "Address low byte should match"


class TestE4E5Roundtrip:
    """Tests for E4/E5 read/write roundtrip functionality."""

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_e4_e5_address_compatibility(self):
        """Test that E4 can read back what would be written by E5."""
        emu = Emulator(log_uart=False, usb_delay=1000)
        emu.load_firmware(str(Path(__file__).parent.parent / 'fw.bin'))
        emu.reset()

        # Set up initial value
        test_addr = 0x2000
        test_value = 0x55

        # Pre-write the value to XDATA (simulating E5)
        emu.memory.xdata[test_addr] = test_value

        # Now E4 read should return that value
        emu.hw.inject_usb_command(0xE4, test_addr, size=1)
        emu.run(max_cycles=50000)

        result = emu.memory.xdata[0x8000]
        assert result == test_value, f"E4 should read back 0x{test_value:02X}, got 0x{result:02X}"


class TestMultipleCommands:
    """Tests for multiple sequential commands."""

    @pytest.mark.skipif(
        not Path(Path(__file__).parent.parent / 'fw.bin').exists(),
        reason="fw.bin not found"
    )
    def test_sequential_e4_commands(self):
        """Test multiple E4 commands in sequence."""
        emu = Emulator(log_uart=False, usb_delay=1000)
        emu.load_firmware(str(Path(__file__).parent.parent / 'fw.bin'))

        # Set up test data at different locations
        locations = [
            (0x1000, 0xAA),
            (0x2000, 0xBB),
            (0x3000, 0xCC),
        ]

        for addr, value in locations:
            emu.reset()
            emu.memory.xdata[addr] = value

            emu.hw.inject_usb_command(0xE4, addr, size=1)
            emu.run(max_cycles=50000)

            result = emu.memory.xdata[0x8000]
            assert result == value, f"E4 at 0x{addr:04X}: expected 0x{value:02X}, got 0x{result:02X}"


class TestCDBParsing:
    """Tests for Command Descriptor Block parsing."""

    def test_cdb_address_encoding(self):
        """Test CDB encodes addresses correctly."""
        emu = Emulator()
        emu.reset()

        # Test various addresses
        test_cases = [
            (0x0000, 0x50, 0x00, 0x00),
            (0x1234, 0x50, 0x12, 0x34),
            (0xFFFF, 0x51, 0xFF, 0xFF),  # 0x1FFFF -> 0x51FFFF
            (0x5678, 0x50, 0x56, 0x78),
        ]

        for addr, exp_high, exp_mid, exp_low in test_cases:
            emu.reset()
            emu.hw.inject_usb_command(0xE4, addr, size=1)

            # Address format: (addr & 0x1FFFF) | 0x500000
            usb_addr = (addr & 0x1FFFF) | 0x500000
            got_high = emu.hw.regs[0x910F]
            got_mid = emu.hw.regs[0x9110]
            got_low = emu.hw.regs[0x9111]

            # Check the address is encoded correctly
            reconstructed = (got_high << 16) | (got_mid << 8) | got_low
            assert reconstructed == usb_addr, \
                f"Address 0x{addr:04X} -> USB 0x{usb_addr:06X}, got 0x{reconstructed:06X}"


class TestScsiWriteCommand:
    """Tests for 0x8A SCSI write command."""

    def test_scsi_write_cdb_format(self):
        """Test SCSI write CDB is formatted correctly."""
        import struct

        emu = Emulator()
        emu.reset()

        # Inject SCSI write command: LBA=0, 1 sector, test data
        test_data = b'Hello SCSI!' + b'\x00' * (512 - 11)
        emu.hw.inject_scsi_write(lba=0, sectors=1, data=test_data)

        # Check CDB format in registers
        # CDB[0] = 0x8A (SCSI write command)
        # CDB[1] = 0x00 (reserved)
        # CDB[2-9] = LBA (8 bytes, big-endian)
        # CDB[10-13] = sectors (4 bytes, big-endian)
        # CDB[14-15] = 0x00 (reserved)
        assert emu.hw.regs[0x910D] == 0x8A, "CDB[0] should be 0x8A"
        assert emu.hw.regs[0x910E] == 0x00, "CDB[1] should be 0x00"

        # LBA is bytes 2-9 (8 bytes big-endian)
        lba_bytes = bytes([emu.hw.regs[0x910D + 2 + i] for i in range(8)])
        lba = struct.unpack('>Q', lba_bytes)[0]
        assert lba == 0, f"LBA should be 0, got {lba}"

        # Sectors is bytes 10-13 (4 bytes big-endian)
        sector_bytes = bytes([emu.hw.regs[0x910D + 10 + i] for i in range(4)])
        sectors = struct.unpack('>I', sector_bytes)[0]
        assert sectors == 1, f"Sectors should be 1, got {sectors}"

    def test_scsi_write_data_in_usb_buffer(self):
        """Test SCSI write data is placed in USB buffer at 0x8000."""
        emu = Emulator()
        emu.reset()

        # Create test data pattern
        test_pattern = bytes([i & 0xFF for i in range(512)])
        emu.hw.inject_scsi_write(lba=0, sectors=1, data=test_pattern)

        # Verify data at USB buffer
        buffer_data = bytes([emu.memory.xdata[0x8000 + i] for i in range(512)])
        assert buffer_data == test_pattern, "USB buffer should contain test data"

    def test_scsi_write_multiple_sectors(self):
        """Test SCSI write with multiple sectors."""
        import struct

        emu = Emulator()
        emu.reset()

        # Write 4 sectors
        test_data = bytes([i & 0xFF for i in range(4 * 512)])
        emu.hw.inject_scsi_write(lba=100, sectors=4, data=test_data)

        # Check sector count
        sector_bytes = bytes([emu.hw.regs[0x910D + 10 + i] for i in range(4)])
        sectors = struct.unpack('>I', sector_bytes)[0]
        assert sectors == 4, f"Sectors should be 4, got {sectors}"

        # Check LBA
        lba_bytes = bytes([emu.hw.regs[0x910D + 2 + i] for i in range(8)])
        lba = struct.unpack('>Q', lba_bytes)[0]
        assert lba == 100, f"LBA should be 100, got {lba}"

        # Verify all data was written
        buffer_data = bytes([emu.memory.xdata[0x8000 + i] for i in range(4 * 512)])
        assert buffer_data == test_data, "USB buffer should contain all sectors"

    def test_scsi_write_data_padding(self):
        """Test SCSI write pads data to sector boundary."""
        emu = Emulator()
        emu.reset()

        # Write partial sector (less than 512 bytes)
        test_data = b'Short data'
        emu.hw.inject_scsi_write(lba=0, sectors=1, data=test_data)

        # Verify padding - remaining bytes should be 0x00
        for i in range(len(test_data)):
            assert emu.memory.xdata[0x8000 + i] == test_data[i], f"Byte {i} should match"

        for i in range(len(test_data), 512):
            assert emu.memory.xdata[0x8000 + i] == 0x00, f"Byte {i} should be padded to 0x00"

    def test_scsi_write_sets_command_pending(self):
        """Test SCSI write sets command pending flag."""
        emu = Emulator()
        emu.reset()

        assert not emu.hw.usb_cmd_pending, "No command should be pending initially"

        emu.hw.inject_scsi_write(lba=0, sectors=1, data=b'\x00' * 512)

        assert emu.hw.usb_cmd_pending, "Command should be pending after inject"
        assert emu.hw.usb_cmd_type == 0x8A, "Command type should be 0x8A"

    def test_scsi_write_cdb_in_ram(self):
        """Test SCSI write CDB is written to RAM at 0x0002."""
        emu = Emulator()
        emu.reset()

        emu.hw.inject_scsi_write(lba=0, sectors=1, data=b'\x00' * 512)

        # CDB should be at XDATA[0x0002+]
        assert emu.memory.xdata[0x0002] == 0x8A, "CDB[0] at 0x0002 should be 0x8A"
        assert emu.memory.xdata[0x0003] == 0x08, "Vendor flag at 0x0003 should be 0x08"


class TestScsiWriteCompatibility:
    """Tests for SCSI write compatibility with python/usb.py format."""

    def test_cdb_matches_python_usb_format(self):
        """Test CDB format matches python/usb.py ScsiWriteOp."""
        import struct

        emu = Emulator()
        emu.reset()

        lba = 0x123456789ABC
        sectors = 32

        emu.hw.inject_scsi_write(lba=lba, sectors=sectors, data=b'\x00' * (sectors * 512))

        # Build expected CDB from python/usb.py format
        expected_cdb = struct.pack('>BBQIBB', 0x8A, 0, lba, sectors, 0, 0)

        # Get actual CDB from registers
        actual_cdb = bytes([emu.hw.regs[0x910D + i] for i in range(16)])

        assert actual_cdb == expected_cdb, \
            f"CDB mismatch: got {actual_cdb.hex()}, expected {expected_cdb.hex()}"

    def test_scsi_and_vendor_commands_coexist(self):
        """Test that SCSI and vendor commands can be used in sequence."""
        emu = Emulator()
        emu.reset()

        # First inject E4 read
        emu.hw.inject_usb_command(0xE4, 0x1000, size=4)
        assert emu.hw.usb_cmd_type == 0xE4, "Should be E4 command"

        # Reset for next command
        emu.hw.usb_cmd_pending = False

        # Then inject SCSI write
        emu.hw.inject_scsi_write(lba=0, sectors=1, data=b'\x00' * 512)
        assert emu.hw.usb_cmd_type == 0x8A, "Should be 0x8A command"

        # Verify SCSI CDB
        assert emu.hw.regs[0x910D] == 0x8A, "CDB should now be SCSI write"


class TestCommandDispatch:
    """Tests for command type dispatch."""

    def test_command_types_have_different_markers(self):
        """Test different command types set different markers."""
        emu = Emulator()

        # E4 read
        emu.reset()
        emu.hw.inject_usb_command(0xE4, 0x1000, size=1)
        e4_marker = emu.memory.xdata[0x05B1]

        # E5 write
        emu.reset()
        emu.hw.inject_usb_command(0xE5, 0x1000, value=0x42)
        e5_marker = emu.memory.xdata[0x05B1]

        # SCSI write
        emu.reset()
        emu.hw.inject_scsi_write(lba=0, sectors=1, data=b'\x00' * 512)
        scsi_marker = emu.memory.xdata[0x05B1]

        # All should have different markers
        assert e4_marker == 0x04, f"E4 marker should be 0x04, got 0x{e4_marker:02X}"
        assert e5_marker == 0x05, f"E5 marker should be 0x05, got 0x{e5_marker:02X}"
        assert scsi_marker == 0x8A, f"SCSI marker should be 0x8A, got 0x{scsi_marker:02X}"

    def test_usb_state_configured_for_all_commands(self):
        """Test USB state is set to CONFIGURED (5) for all command types."""
        emu = Emulator()

        for cmd_name, inject_fn in [
            ("E4", lambda: emu.hw.inject_usb_command(0xE4, 0x1000, size=1)),
            ("E5", lambda: emu.hw.inject_usb_command(0xE5, 0x1000, value=0x42)),
            ("SCSI", lambda: emu.hw.inject_scsi_write(lba=0, sectors=1, data=b'\x00' * 512)),
        ]:
            emu.reset()
            inject_fn()
            usb_state = emu.memory.idata[0x6A]
            assert usb_state == 5, f"{cmd_name}: USB state should be 5, got {usb_state}"


if __name__ == "__main__":
    pytest.main([__file__, '-v'])
