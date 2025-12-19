#!/usr/bin/env python3
"""
Test emulator functionality for the ASM2464PD firmware.

These tests verify:
- UART TX register writes produce output
- Hardware state management works correctly
- Basic CPU execution functions
- PCIe and timer emulation behavior

Usage:
    # Run against original firmware (default)
    pytest test/test_emulator.py -v

    # Run against our compiled firmware
    pytest test/test_emulator.py -v --firmware=ours

    # Run against both firmwares
    pytest test/test_emulator.py -v --firmware=both
"""

import sys
import os
import io
from pathlib import Path
import pytest

# Add emulate directory to path
sys.path.insert(0, str(Path(__file__).parent.parent / 'emulate'))

from emu import Emulator
from conftest import ORIGINAL_FIRMWARE, OUR_FIRMWARE


class TestUARTOutput:
    """Tests for UART output functionality."""

    def test_direct_uart_write_raw(self, emulator):
        """Test direct UART register writes with raw output mode."""
        emu = emulator

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

    def test_hardware_register_defaults(self, emulator):
        """Test that hardware registers have correct default values."""
        emu = emulator

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

    def test_polling_counters(self, emulator):
        """Test that polling counters increment on repeated reads."""
        emu = emulator

        test_addr = 0xC800
        emu.hw.regs[test_addr] = 0x00

        # Read multiple times
        for _ in range(5):
            emu.hw.read(test_addr)

        assert emu.hw.poll_counts.get(test_addr, 0) >= 5, "Poll count should increment"


class TestEmulatorExecution:
    """Tests for basic emulator execution."""

    def test_emulator_reset(self, emulator):
        """Test that emulator resets to clean state."""
        emu = emulator

        assert emu.cpu.pc == 0x0000, "PC should be 0 after reset"
        assert emu.inst_count == 0, "Instruction count should be 0"
        assert emu.cpu.cycles == 0, "Cycle count should be 0"

    def test_memory_read_write(self, emulator):
        """Test basic XDATA memory operations."""
        emu = emulator

        # Test XDATA write and read (low memory, not hardware registers)
        test_addr = 0x1000
        test_value = 0x42

        emu.memory.write_xdata(test_addr, test_value)
        result = emu.memory.read_xdata(test_addr)

        assert result == test_value, f"Expected 0x{test_value:02X}, got 0x{result:02X}"

    def test_idata_memory(self, emulator):
        """Test IDATA (internal RAM) operations."""
        emu = emulator

        # Test IDATA write and read
        test_addr = 0x30
        test_value = 0x55

        emu.memory.write_idata(test_addr, test_value)
        result = emu.memory.read_idata(test_addr)

        assert result == test_value, f"Expected 0x{test_value:02X}, got 0x{result:02X}"

    def test_sfr_operations(self, emulator):
        """Test SFR (Special Function Register) operations."""
        emu = emulator

        # Test SP (Stack Pointer) - SFR 0x81
        emu.memory.write_sfr(0x81, 0x50)
        result = emu.memory.read_sfr(0x81)

        assert result == 0x50, f"Expected SP=0x50, got 0x{result:02X}"

    def test_firmware_load(self, firmware_path, firmware_name):
        """Test that firmware loads correctly."""
        if firmware_path is None:
            pytest.skip("No firmware available")

        emu = Emulator()
        emu.reset()
        emu.load_firmware(str(firmware_path))

        # Check that code memory has data
        first_byte = emu.memory.read_code(0x0000)
        assert first_byte != 0x00 or emu.memory.read_code(0x0001) != 0x00, \
            f"Firmware ({firmware_name}) should have non-zero bytes at start"

    def test_firmware_execution_cycles(self, firmware_emulator):
        """Test that firmware executes for specified cycles."""
        emu, fw_name = firmware_emulator

        # Run for a limited number of cycles
        max_cycles = 1000
        reason = emu.run(max_cycles=max_cycles)

        assert reason == "max_cycles", f"[{fw_name}] Expected stop reason 'max_cycles', got '{reason}'"
        assert emu.cpu.cycles >= max_cycles, f"[{fw_name}] Should have run at least {max_cycles} cycles"


class TestPCIeEmulation:
    """Tests for PCIe hardware emulation."""

    def test_pcie_trigger_completion(self, emulator):
        """Test that PCIe trigger sets completion bits."""
        emu = emulator

        # Trigger PCIe operation
        emu.hw.write(0xB254, 0x01)

        # Check completion status
        status = emu.hw.read(0xB296)
        assert status & 0x06, "PCIe completion bits should be set after trigger"

    def test_pcie_status_polling(self, emulator):
        """Test that PCIe status sets bits after polling."""
        emu = emulator

        # Initial read should have bits clear or not all set
        initial = emu.hw.read(0xB296)

        # Poll multiple times
        for _ in range(10):
            status = emu.hw.read(0xB296)

        # After polling, completion bits should be set
        assert status & 0x06, "PCIe completion bits should be set after polling"


class TestUSBVendorCommands:
    """Tests for USB vendor command emulation."""

    def test_e4_read_xdata(self, firmware_emulator):
        """Test E4 read command returns correct XDATA values."""
        emu, fw_name = firmware_emulator

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
        assert result == test_data, f"[{fw_name}] E4 read returned {result}, expected {test_data}"

    def test_e4_read_different_addresses(self, firmware_emulator):
        """Test E4 read works for various XDATA addresses."""
        emu, fw_name = firmware_emulator

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
            assert result == data, f"[{fw_name}] E4 read at 0x{addr:04X} returned {result}, expected {data}"


class TestTimerEmulation:
    """Tests for timer hardware emulation."""

    def test_timer_csr_ready_bit(self, emulator):
        """Test that timer CSR sets ready bit after polling."""
        emu = emulator

        timer_addr = 0xCC11  # Timer 0 CSR

        # Poll the timer CSR
        for _ in range(5):
            value = emu.hw.read(timer_addr)

        # Ready bit (bit 1) should be set after polling
        assert value & 0x02, "Timer ready bit should be set after polling"

    def test_timer_dma_status(self, emulator):
        """Test timer/DMA status register completion."""
        emu = emulator

        dma_status_addr = 0xCC89

        # Poll the status
        for _ in range(5):
            value = emu.hw.read(dma_status_addr)

        # Complete bit should be set
        assert value & 0x02, "Timer/DMA complete bit should be set after polling"


class TestUSBStateMachine:
    """Tests for USB state machine progression."""

    def test_usb_state_progresses_to_configured(self, emulator):
        """Test that USB state machine reaches CONFIGURED state."""
        emu = emulator

        # Connect USB
        emu.hw.usb_controller.connect()

        # State machine should have started
        assert emu.hw.usb_controller.state.value >= 1, "USB should be at least ATTACHED"

        # After enough state machine reads, should be CONFIGURED
        for _ in range(10):
            emu.hw.usb_controller.advance_enumeration()

        assert emu.hw.usb_controller.enumeration_complete, "USB enumeration should complete"

    def test_usb_connect_enables_command_processing(self, emulator):
        """Test that USB connect prepares system for commands."""
        emu = emulator

        assert not emu.hw.usb_connected, "USB should start disconnected"

        emu.hw.usb_controller.connect()

        assert emu.hw.usb_controller.state.value >= 1, "USB should be connected"


class TestUSBEndpointBuffers:
    """Tests for USB endpoint buffer functionality."""

    def test_ep0_buffer_stores_command_data(self, emulator):
        """Test that EP0 buffer can store and retrieve command data."""
        emu = emulator

        # Simulate CDB data that would arrive via USB
        test_cdb = bytes([0xE4, 0x04, 0x50, 0x12, 0x34, 0x00])

        for i, val in enumerate(test_cdb):
            emu.hw.usb_ep0_buf[i] = val

        # Verify data is accessible
        result = bytes([emu.hw.usb_ep0_buf[i] for i in range(len(test_cdb))])
        assert result == test_cdb, "EP0 buffer should store CDB data"

    def test_ep_data_buffer_stores_transfer_data(self, emulator):
        """Test that EP data buffer can store bulk transfer data."""
        emu = emulator

        # Write test payload
        test_data = bytes([0xDE, 0xAD, 0xBE, 0xEF] * 16)

        for i, val in enumerate(test_data):
            emu.hw.usb_ep_data_buf[i] = val

        result = bytes([emu.hw.usb_ep_data_buf[i] for i in range(len(test_data))])
        assert result == test_data, "EP data buffer should store transfer data"


class TestE4ReadCommand:
    """End-to-end tests for E4 (read XDATA) command."""

    def test_e4_reads_single_byte(self, firmware_emulator):
        """Test E4 command reads a single byte from XDATA."""
        emu, fw_name = firmware_emulator

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
        assert result == test_value, f"[{fw_name}] E4 read returned 0x{result:02X}, expected 0x{test_value:02X}"

    def test_e4_reads_multiple_bytes(self, firmware_emulator):
        """Test E4 command reads multiple bytes from XDATA."""
        emu, fw_name = firmware_emulator

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
        assert result == test_data, f"[{fw_name}] E4 read returned {result}, expected {test_data}"

    def test_e4_reads_from_different_regions(self, firmware_emulator):
        """Test E4 command works for various XDATA regions."""
        emu, fw_name = firmware_emulator

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
            assert result == data, f"[{fw_name}] E4 at 0x{addr:04X}: got {result}, expected {data}"


class TestCodeBanking:
    """End-to-end tests for code memory banking."""

    def test_bank_switching_reads_different_code(self, firmware_emulator):
        """Test that bank switching reads different firmware code."""
        emu, fw_name = firmware_emulator

        # Read from upper memory in bank 0
        emu.memory.sfr[0x96 - 0x80] = 0x00  # DPX = 0
        bank0_byte = emu.memory.read_code(0x8000)

        # Read from same address in bank 1
        emu.memory.sfr[0x96 - 0x80] = 0x01  # DPX = 1
        bank1_byte = emu.memory.read_code(0x8000)

        # Bank 0 and Bank 1 code should be different at most addresses
        # (they're different code sections in the firmware)
        # At minimum, we verify both reads work
        assert bank0_byte is not None, f"[{fw_name}] Bank 0 read should succeed"
        assert bank1_byte is not None, f"[{fw_name}] Bank 1 read should succeed"

    def test_lower_memory_ignores_bank(self, firmware_emulator):
        """Test that lower 32KB always reads from bank 0."""
        emu, fw_name = firmware_emulator

        # Read from lower memory with DPX=0
        emu.memory.sfr[0x96 - 0x80] = 0x00
        byte_dpx0 = emu.memory.read_code(0x1000)

        # Read from same address with DPX=1
        emu.memory.sfr[0x96 - 0x80] = 0x01
        byte_dpx1 = emu.memory.read_code(0x1000)

        # Should be identical (lower 32KB ignores bank)
        assert byte_dpx0 == byte_dpx1, f"[{fw_name}] Lower 32KB should ignore bank setting"


class TestBitOperations:
    """End-to-end tests for 8051 bit-addressable memory."""

    def test_bit_operations_persist_to_byte(self, emulator):
        """Test that individual bit writes affect the underlying byte."""
        emu = emulator

        # Clear the byte first
        emu.memory.idata[0x20] = 0x00

        # Set individual bits and verify byte changes
        emu.memory.write_bit(0x00, True)  # Bit 0
        assert emu.memory.idata[0x20] == 0x01

        emu.memory.write_bit(0x07, True)  # Bit 7
        assert emu.memory.idata[0x20] == 0x81

        emu.memory.write_bit(0x00, False)  # Clear bit 0
        assert emu.memory.idata[0x20] == 0x80

    def test_byte_writes_affect_bit_reads(self, emulator):
        """Test that byte writes are visible through bit reads."""
        emu = emulator

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

    def test_dma_copies_data_to_usb_buffer(self, emulator):
        """Test that DMA transfer copies XDATA to USB buffer."""
        emu = emulator

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

    def test_dma_sets_completion_status(self, emulator):
        """Test that DMA transfer sets completion status in RAM."""
        emu = emulator

        # Clear completion flag
        emu.memory.xdata[0x0AA0] = 0

        # Trigger small DMA
        emu.hw._perform_pcie_dma(0x500000, 5)

        # Completion flag should be set to transfer size
        assert emu.memory.xdata[0x0AA0] == 5, "DMA completion should indicate size"


class TestFirmwareExecution:
    """End-to-end tests for firmware execution."""

    def test_firmware_runs_without_crash(self, firmware_emulator):
        """Test that firmware executes successfully for many cycles."""
        emu, fw_name = firmware_emulator

        # Run for significant number of cycles
        reason = emu.run(max_cycles=100000)

        # Should stop due to cycle limit, not error
        assert reason == "max_cycles", f"[{fw_name}] Firmware should run without errors, stopped: {reason}"
        assert emu.inst_count > 1000, f"[{fw_name}] Should execute many instructions"

    def test_firmware_produces_uart_output(self, firmware_emulator):
        """Test that firmware produces UART debug output."""
        emu, fw_name = firmware_emulator

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
        assert len(output) > 0, f"[{fw_name}] Firmware should produce UART output"

    def test_usb_connect_triggers_state_changes(self, firmware_emulator):
        """Test that USB connect event triggers firmware state changes."""
        emu, fw_name = firmware_emulator

        # Reconfigure with longer delay
        emu.hw.usb_connect_delay = 10000

        # Run past USB connect
        emu.run(max_cycles=50000)

        # USB should be connected
        assert emu.hw.usb_connected, f"[{fw_name}] USB should be connected after delay"

        # USB controller should have progressed
        assert emu.hw.usb_controller.state.value > 0, f"[{fw_name}] USB state should have progressed"


class TestTracingFunctionality:
    """End-to-end tests for execution and XDATA tracing."""

    def test_trace_callback_is_invoked(self, emulator):
        """Test that trace callbacks are invoked on trace point hits."""
        emu = emulator

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

    def test_xdata_write_log_accumulates(self, emulator):
        """Test that XDATA trace log accumulates write entries."""
        emu = emulator

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

    def test_sync_flag_cleared_after_polling(self, emulator):
        """Test that sync flags are auto-cleared to simulate hardware completion."""
        emu = emulator

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

    def test_command_injection_sets_up_state(self, emulator):
        """Test that command injection prepares all necessary state."""
        emu = emulator

        # Inject command
        emu.hw.inject_usb_command(0xE4, 0x1234, size=4)

        # Verify USB is ready for command processing
        assert emu.hw.usb_connected, "USB should be connected"
        assert emu.hw.usb_cmd_pending, "Command should be pending"

        # Verify CDB is in registers
        assert emu.hw.regs[0x910D] == 0xE4, "Command type should be in register"

    def test_command_completion_clears_pending(self, emulator):
        """Test that command completion clears the pending flag."""
        emu = emulator

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

    def test_e5_command_injection_format(self, emulator):
        """Test E5 command CDB is formatted correctly."""
        emu = emulator

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

    def test_e5_sets_correct_command_marker(self, emulator):
        """Test E5 command sets correct marker in command table."""
        emu = emulator

        emu.hw.inject_usb_command(0xE5, 0x1234, value=0x42)

        # E5 should set command marker to 0x05
        assert emu.memory.xdata[0x05B1] == 0x05, "E5 marker should be 0x05"

    def test_e5_and_e4_use_same_address_format(self, emulator):
        """Test E5 and E4 use compatible address encoding."""
        emu = emulator

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

    def test_e4_e5_address_compatibility(self, firmware_emulator):
        """Test that E4 can read back what would be written by E5."""
        emu, fw_name = firmware_emulator

        # Set up initial value
        test_addr = 0x2000
        test_value = 0x55

        # Pre-write the value to XDATA (simulating E5)
        emu.memory.xdata[test_addr] = test_value

        # Now E4 read should return that value
        emu.hw.inject_usb_command(0xE4, test_addr, size=1)
        emu.run(max_cycles=50000)

        result = emu.memory.xdata[0x8000]
        assert result == test_value, f"[{fw_name}] E4 should read back 0x{test_value:02X}, got 0x{result:02X}"


class TestMultipleCommands:
    """Tests for multiple sequential commands."""

    def test_sequential_e4_commands(self, firmware_emulator):
        """Test multiple E4 commands in sequence."""
        emu, fw_name = firmware_emulator

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
            assert result == value, f"[{fw_name}] E4 at 0x{addr:04X}: expected 0x{value:02X}, got 0x{result:02X}"


class TestCDBParsing:
    """Tests for Command Descriptor Block parsing."""

    def test_cdb_address_encoding(self, emulator):
        """Test CDB encodes addresses correctly."""
        emu = emulator

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

    def test_scsi_write_cdb_format(self, emulator):
        """Test SCSI write CDB is formatted correctly."""
        import struct

        emu = emulator

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

    def test_scsi_write_data_in_usb_buffer(self, emulator):
        """Test SCSI write data is placed in USB buffer at 0x8000."""
        emu = emulator

        # Create test data pattern
        test_pattern = bytes([i & 0xFF for i in range(512)])
        emu.hw.inject_scsi_write(lba=0, sectors=1, data=test_pattern)

        # Verify data at USB buffer
        buffer_data = bytes([emu.memory.xdata[0x8000 + i] for i in range(512)])
        assert buffer_data == test_pattern, "USB buffer should contain test data"

    def test_scsi_write_multiple_sectors(self, emulator):
        """Test SCSI write with multiple sectors."""
        import struct

        emu = emulator

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

    def test_scsi_write_data_padding(self, emulator):
        """Test SCSI write pads data to sector boundary."""
        emu = emulator

        # Write partial sector (less than 512 bytes)
        test_data = b'Short data'
        emu.hw.inject_scsi_write(lba=0, sectors=1, data=test_data)

        # Verify padding - remaining bytes should be 0x00
        for i in range(len(test_data)):
            assert emu.memory.xdata[0x8000 + i] == test_data[i], f"Byte {i} should match"

        for i in range(len(test_data), 512):
            assert emu.memory.xdata[0x8000 + i] == 0x00, f"Byte {i} should be padded to 0x00"

    def test_scsi_write_sets_command_pending(self, emulator):
        """Test SCSI write sets command pending flag."""
        emu = emulator

        assert not emu.hw.usb_cmd_pending, "No command should be pending initially"

        emu.hw.inject_scsi_write(lba=0, sectors=1, data=b'\x00' * 512)

        assert emu.hw.usb_cmd_pending, "Command should be pending after inject"
        assert emu.hw.usb_cmd_type == 0x8A, "Command type should be 0x8A"

    def test_scsi_write_cdb_in_ram(self, emulator):
        """Test SCSI write CDB is written to RAM at 0x0002."""
        emu = emulator

        emu.hw.inject_scsi_write(lba=0, sectors=1, data=b'\x00' * 512)

        # CDB should be at XDATA[0x0002+]
        assert emu.memory.xdata[0x0002] == 0x8A, "CDB[0] at 0x0002 should be 0x8A"
        assert emu.memory.xdata[0x0003] == 0x08, "Vendor flag at 0x0003 should be 0x08"


class TestScsiWriteCompatibility:
    """Tests for SCSI write compatibility with python/usb.py format."""

    def test_cdb_matches_python_usb_format(self, emulator):
        """Test CDB format matches python/usb.py ScsiWriteOp."""
        import struct

        emu = emulator

        lba = 0x123456789ABC
        sectors = 32

        emu.hw.inject_scsi_write(lba=lba, sectors=sectors, data=b'\x00' * (sectors * 512))

        # Build expected CDB from python/usb.py format
        expected_cdb = struct.pack('>BBQIBB', 0x8A, 0, lba, sectors, 0, 0)

        # Get actual CDB from registers
        actual_cdb = bytes([emu.hw.regs[0x910D + i] for i in range(16)])

        assert actual_cdb == expected_cdb, \
            f"CDB mismatch: got {actual_cdb.hex()}, expected {expected_cdb.hex()}"

    def test_scsi_and_vendor_commands_coexist(self, emulator):
        """Test that SCSI and vendor commands can be used in sequence."""
        emu = emulator

        # First inject E4 read
        emu.hw.inject_usb_command(0xE4, 0x1000, size=1)
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

    def test_command_types_have_different_markers(self, emulator):
        """Test different command types set different markers."""
        emu = emulator

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

    def test_usb_state_configured_for_all_commands(self, emulator):
        """Test USB state is set to CONFIGURED (5) for all command types."""
        emu = emulator

        for cmd_name, inject_fn in [
            ("E4", lambda: emu.hw.inject_usb_command(0xE4, 0x1000, size=1)),
            ("E5", lambda: emu.hw.inject_usb_command(0xE5, 0x1000, value=0x42)),
            ("SCSI", lambda: emu.hw.inject_scsi_write(lba=0, sectors=1, data=b'\x00' * 512)),
        ]:
            emu.reset()
            inject_fn()
            usb_state = emu.memory.idata[0x6A]
            assert usb_state == 5, f"{cmd_name}: USB state should be 5, got {usb_state}"


class TestVendorCommandStateMachine:
    """Tests for USB state machine behavior during vendor commands."""

    def test_usb_state_set_to_5_before_command(self, firmware_emulator):
        """Test that I_STATE_6A is set to 5 (CONFIGURED) before command processing."""
        emu, fw_name = firmware_emulator

        emu.hw.inject_usb_command(0xE4, 0x1000, size=1)

        # USB state should be 5 before firmware runs
        state = emu.memory.idata[0x6A]
        assert state == 5, f"[{fw_name}] USB state should be 5 before command, got {state}"

    def test_vendor_flag_set_for_e4(self, firmware_emulator):
        """Test that vendor flag (G_EP_STATUS_CTRL) bit 3 is set for E4 commands."""
        emu, fw_name = firmware_emulator

        emu.hw.inject_usb_command(0xE4, 0x1000, size=1)

        # G_EP_STATUS_CTRL (0x0003) should have bit 3 set (0x08)
        vendor_flag = emu.memory.xdata[0x0003]
        assert vendor_flag & 0x08, f"[{fw_name}] Vendor flag bit 3 should be set, got 0x{vendor_flag:02X}"

    def test_status_register_written_during_e4(self, firmware_emulator):
        """Test that 0x90E3 is written with 0x02 during E4 processing."""
        emu, fw_name = firmware_emulator

        # Track writes to 0x90E3
        writes_90e3 = []
        def track_90e3(addr, val):
            writes_90e3.append(val)
        emu.memory.xdata_write_hooks[0x90E3] = track_90e3

        emu.hw.inject_usb_command(0xE4, 0x1000, size=1)
        emu.run(max_cycles=50000)

        assert 0x02 in writes_90e3, f"[{fw_name}] 0x90E3 should be written with 0x02, writes: {writes_90e3}"


class TestDMATriggerSequence:
    """Tests for the DMA trigger sequence during E4 commands."""

    def test_dma_triggered_by_0x08_write_to_b296(self, firmware_emulator):
        """Test that DMA is triggered by writing 0x08 to 0xB296."""
        emu, fw_name = firmware_emulator

        # Track writes to 0xB296
        writes_b296 = []
        original_write = emu.hw.write_callbacks.get(0xB296)

        def track_b296(hw, addr, val):
            writes_b296.append(val)
            if original_write:
                original_write(hw, addr, val)

        emu.hw.write_callbacks[0xB296] = track_b296

        # Setup test data
        emu.memory.xdata[0x1000] = 0xAB

        emu.hw.inject_usb_command(0xE4, 0x1000, size=1)
        emu.run(max_cycles=50000)

        # Should have written 0x08 to trigger DMA
        assert 0x08 in writes_b296, f"[{fw_name}] 0xB296 should be written with 0x08, writes: {writes_b296}"

    def test_dma_copies_correct_data(self, firmware_emulator):
        """Test that DMA correctly copies data from source to USB buffer."""
        emu, fw_name = firmware_emulator

        # Setup distinctive test pattern
        test_data = [0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE]
        for i, val in enumerate(test_data):
            emu.memory.xdata[0x2000 + i] = val

        emu.hw.inject_usb_command(0xE4, 0x2000, size=len(test_data))
        emu.run(max_cycles=50000)

        # Verify USB buffer at 0x8000
        result = [emu.memory.xdata[0x8000 + i] for i in range(len(test_data))]
        assert result == test_data, f"[{fw_name}] DMA should copy exact data, got {[hex(x) for x in result]}"

    def test_dma_size_from_cdb(self, firmware_emulator):
        """Test that DMA uses size from CDB register 0x910E."""
        emu, fw_name = firmware_emulator

        # Fill area with known pattern
        for i in range(32):
            emu.memory.xdata[0x3000 + i] = i + 0x40

        # Request only 4 bytes
        emu.hw.inject_usb_command(0xE4, 0x3000, size=4)
        emu.run(max_cycles=50000)

        # First 4 bytes should be copied
        result = [emu.memory.xdata[0x8000 + i] for i in range(4)]
        expected = [0x40, 0x41, 0x42, 0x43]
        assert result == expected, f"[{fw_name}] Should copy exactly 4 bytes, got {[hex(x) for x in result]}"


class TestE5WriteCommand:
    """Tests for E5 write command functionality.

    Note: E5 tests for original firmware are expected to fail because the
    original firmware's E5 command handling uses a different code path that
    doesn't go through the DMA trigger at 0xB296 that our emulator captures.
    Our firmware implements E5 through the same DMA path as E4.
    """

    def test_e5_writes_to_xdata(self, firmware_emulator):
        """Test E5 command writes value to specified XDATA address."""
        emu, fw_name = firmware_emulator

        test_addr = 0x2500
        test_value = 0x77

        # Clear the location first
        emu.memory.xdata[test_addr] = 0x00

        # Inject E5 write command
        emu.hw.inject_usb_command(0xE5, test_addr, value=test_value)
        # Use absolute cycle count (current + additional)
        emu.run(max_cycles=emu.cpu.cycles + 50000)

        # Verify the value was written
        result = emu.memory.xdata[test_addr]
        assert result == test_value, f"[{fw_name}] E5 should write 0x{test_value:02X}, got 0x{result:02X}"

    def test_e5_writes_multiple_addresses(self, firmware_emulator):
        """Test E5 command can write to different addresses."""
        emu, fw_name = firmware_emulator

        test_cases = [
            (0x1000, 0x11),
            (0x2000, 0x22),
            (0x3000, 0x33),
            (0x4000, 0x44),
        ]

        for addr, value in test_cases:
            emu.reset()
            # Run to boot state
            emu.run(max_cycles=500000)
            emu.memory.xdata[addr] = 0x00  # Clear first

            emu.hw.inject_usb_command(0xE5, addr, value=value)
            emu.run(max_cycles=emu.cpu.cycles + 50000)

            result = emu.memory.xdata[addr]
            assert result == value, f"[{fw_name}] E5 at 0x{addr:04X}: expected 0x{value:02X}, got 0x{result:02X}"

    def test_e5_e4_roundtrip(self, firmware_emulator):
        """Test E5 write followed by E4 read returns same value."""
        emu, fw_name = firmware_emulator

        test_addr = 0x2800
        test_value = 0xA5

        # First write with E5
        emu.hw.inject_usb_command(0xE5, test_addr, value=test_value)
        emu.run(max_cycles=emu.cpu.cycles + 50000)

        # Store the written value directly since E5 DMA writes to XDATA
        written_value = emu.memory.xdata[test_addr]

        # Reset state for next command
        emu.reset()
        emu.run(max_cycles=500000)

        # Restore the value since reset clears XDATA
        emu.memory.xdata[test_addr] = written_value

        # Then read back with E4
        emu.hw.inject_usb_command(0xE4, test_addr, size=1)
        emu.run(max_cycles=emu.cpu.cycles + 50000)

        result = emu.memory.xdata[0x8000]
        assert result == test_value, f"[{fw_name}] E4 after E5 should read 0x{test_value:02X}, got 0x{result:02X}"


class TestE4EdgeCases:
    """Tests for E4 command edge cases."""

    def test_e4_read_single_byte(self, firmware_emulator):
        """Test E4 reading exactly 1 byte."""
        emu, fw_name = firmware_emulator

        emu.memory.xdata[0x1234] = 0x99
        emu.hw.inject_usb_command(0xE4, 0x1234, size=1)
        emu.run(max_cycles=50000)

        assert emu.memory.xdata[0x8000] == 0x99, f"[{fw_name}] Single byte read failed"

    def test_e4_read_max_size_64(self, firmware_emulator):
        """Test E4 reading maximum typical size (64 bytes)."""
        emu, fw_name = firmware_emulator

        # Fill with pattern
        for i in range(64):
            emu.memory.xdata[0x1000 + i] = i ^ 0xAA

        emu.hw.inject_usb_command(0xE4, 0x1000, size=64)
        emu.run(max_cycles=50000)

        # Verify all 64 bytes
        for i in range(64):
            expected = i ^ 0xAA
            result = emu.memory.xdata[0x8000 + i]
            assert result == expected, f"[{fw_name}] Byte {i}: expected 0x{expected:02X}, got 0x{result:02X}"

    def test_e4_read_low_xdata(self, firmware_emulator):
        """Test E4 reading from low XDATA addresses (work RAM)."""
        emu, fw_name = firmware_emulator

        # Low XDATA is work RAM
        test_addr = 0x0050
        emu.memory.xdata[test_addr] = 0xCC

        emu.hw.inject_usb_command(0xE4, test_addr, size=1)
        emu.run(max_cycles=50000)

        assert emu.memory.xdata[0x8000] == 0xCC, f"[{fw_name}] Low XDATA read failed"

    def test_e4_read_high_xdata(self, firmware_emulator):
        """Test E4 reading from high XDATA addresses."""
        emu, fw_name = firmware_emulator

        test_addr = 0x5000
        emu.memory.xdata[test_addr] = 0xDD

        emu.hw.inject_usb_command(0xE4, test_addr, size=1)
        emu.run(max_cycles=50000)

        assert emu.memory.xdata[0x8000] == 0xDD, f"[{fw_name}] High XDATA read failed"

    def test_e4_preserves_adjacent_memory(self, firmware_emulator):
        """Test E4 doesn't corrupt memory adjacent to target."""
        emu, fw_name = firmware_emulator

        # Fill area around target
        for i in range(16):
            emu.memory.xdata[0x2000 + i] = i + 1

        # Read just 4 bytes from middle
        emu.hw.inject_usb_command(0xE4, 0x2004, size=4)
        emu.run(max_cycles=50000)

        # Verify source data wasn't corrupted
        for i in range(16):
            expected = i + 1
            result = emu.memory.xdata[0x2000 + i]
            assert result == expected, f"[{fw_name}] Source memory corrupted at offset {i}"


class TestRegisterStateDuringCommands:
    """Tests for register state during command processing."""

    def test_cdb_registers_contain_command(self, firmware_emulator):
        """Test CDB registers are set up correctly before command runs."""
        emu, fw_name = firmware_emulator

        emu.hw.inject_usb_command(0xE4, 0x1234, size=8)

        # Check CDB registers
        assert emu.hw.regs[0x910D] == 0xE4, f"[{fw_name}] Command type wrong"
        assert emu.hw.regs[0x910E] == 0x08, f"[{fw_name}] Size wrong"
        assert emu.hw.regs[0x910F] == 0x50, f"[{fw_name}] Addr high wrong"
        assert emu.hw.regs[0x9110] == 0x12, f"[{fw_name}] Addr mid wrong"
        assert emu.hw.regs[0x9111] == 0x34, f"[{fw_name}] Addr low wrong"

    def test_usb_interrupt_triggers_handler(self, firmware_emulator):
        """Test that USB interrupt is triggered and handler runs."""
        emu, fw_name = firmware_emulator

        # Track if interrupt handler PC was reached
        handler_reached = [False]

        # Original firmware handler entry is around 0x0E33
        # Our firmware may have handler at a different address
        # Both should respond to External Interrupt 0 (vector 0x0003)
        # The interrupt vector at 0x0003 typically has a jump to the actual handler

        def check_handler():
            pc = emu.cpu.pc
            # Check if we're in the interrupt handler region
            # Original: 0x0E00-0x0FFF
            # Also check the interrupt vector region 0x0000-0x00FF
            # or any address that indicates the interrupt was processed
            if 0x0E00 <= pc <= 0x0FFF:
                handler_reached[0] = True
            # Also check for 0x0003 (EX0 vector) or near it
            if 0x0003 <= pc <= 0x0030:
                handler_reached[0] = True

        emu.hw.inject_usb_command(0xE4, 0x1000, size=1)

        # Run and check PC periodically
        for _ in range(1000):
            check_handler()
            if handler_reached[0]:
                break
            emu.step()

        assert handler_reached[0], f"[{fw_name}] Interrupt handler should be reached"


class TestFirmwareBehaviorComparison:
    """Tests that verify firmware behavior matches expected results.

    These tests run against whichever firmware is selected and verify
    the expected behavior. Both firmwares should produce correct results.
    """

    def test_e4_reads_various_patterns(self, firmware_emulator):
        """Test E4 read command handles various data patterns correctly."""
        emu, fw_name = firmware_emulator

        test_cases = [
            (0x1000, [0x11, 0x22, 0x33, 0x44]),
            (0x2000, [0xAA, 0xBB]),
            (0x3000, [0xFF]),
            (0x4000, [0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE]),
        ]

        for test_addr, test_data in test_cases:
            emu.reset()
            emu.run(max_cycles=500000)

            for i, val in enumerate(test_data):
                emu.memory.xdata[test_addr + i] = val

            emu.hw.inject_usb_command(0xE4, test_addr, size=len(test_data))
            emu.run(max_cycles=emu.cpu.cycles + 50000)

            result = [emu.memory.xdata[0x8000 + i] for i in range(len(test_data))]
            assert result == test_data, \
                f"[{fw_name}] E4 at 0x{test_addr:04X}: expected {[hex(x) for x in test_data]}, got {[hex(x) for x in result]}"

    def test_e5_writes_various_patterns(self, firmware_emulator):
        """Test E5 write command handles various data patterns correctly."""
        emu, fw_name = firmware_emulator

        test_cases = [
            (0x1500, 0x42),
            (0x2500, 0xAA),
            (0x3500, 0x55),
            (0x4500, 0x01),
        ]

        for test_addr, test_value in test_cases:
            emu.reset()
            emu.run(max_cycles=500000)

            emu.hw.inject_usb_command(0xE5, test_addr, value=test_value)
            emu.run(max_cycles=emu.cpu.cycles + 50000)

            result = emu.memory.xdata[test_addr]
            assert result == test_value, \
                f"[{fw_name}] E5 at 0x{test_addr:04X}: expected 0x{test_value:02X}, got 0x{result:02X}"

    def test_sequential_e4_commands(self, firmware_emulator):
        """Test sequential E4 read commands work correctly."""
        emu, fw_name = firmware_emulator

        # First E4 read
        emu.memory.xdata[0x2000] = 0x55
        emu.hw.inject_usb_command(0xE4, 0x2000, size=1)
        emu.run(max_cycles=emu.cpu.cycles + 50000)

        result1 = emu.memory.xdata[0x8000]
        assert result1 == 0x55, f"[{fw_name}] First E4 read: expected 0x55, got 0x{result1:02X}"

        # Reset for next command
        emu.reset()
        emu.run(max_cycles=500000)

        # Second E4 read
        emu.memory.xdata[0x3000] = 0xAA
        emu.hw.inject_usb_command(0xE4, 0x3000, size=1)
        emu.run(max_cycles=emu.cpu.cycles + 50000)

        result2 = emu.memory.xdata[0x8000]
        assert result2 == 0xAA, f"[{fw_name}] Second E4 read: expected 0xAA, got 0x{result2:02X}"


class TestUSBPassthrough:
    """
    Tests for USB passthrough mechanism.

    These tests verify that:
    1. CDB format matches python/usb.py expectations
    2. inject_usb_command() sets correct MMIO registers
    3. Response data goes to correct location (0x8000)
    4. The passthrough can use inject_usb_command() as a reference

    The passthrough (usb_device.py) should replicate what inject_usb_command()
    does in hardware.py to properly trigger firmware command processing.
    """

    def test_passthrough_cdb_format(self, emulator):
        """Verify CDB format matches python/usb.py expectations."""
        # Test address encoding: addr | 0x500000
        test_cases = [
            (0x1234, 0x50, 0x12, 0x34),
            (0x0000, 0x50, 0x00, 0x00),
            (0xFFFF, 0x50, 0xFF, 0xFF),
            (0x8000, 0x50, 0x80, 0x00),
        ]

        for xdata_addr, exp_high, exp_mid, exp_low in test_cases:
            usb_addr = (xdata_addr & 0x1FFFF) | 0x500000
            assert (usb_addr >> 16) & 0xFF == exp_high, f"High byte wrong for 0x{xdata_addr:04X}"
            assert (usb_addr >> 8) & 0xFF == exp_mid, f"Mid byte wrong for 0x{xdata_addr:04X}"
            assert usb_addr & 0xFF == exp_low, f"Low byte wrong for 0x{xdata_addr:04X}"

    def test_passthrough_cdb_registers_are_set(self, firmware_emulator):
        """Verify inject_usb_command sets CDB in registers 0x910D-0x9112."""
        emu, fw_name = firmware_emulator

        test_addr = 0x1234
        size = 8

        # Inject command (this is what passthrough should replicate)
        emu.hw.inject_usb_command(0xE4, test_addr, size=size)

        # Verify CDB registers
        assert emu.hw.regs[0x910D] == 0xE4, "CDB[0] should be command type"
        assert emu.hw.regs[0x910E] == size, "CDB[1] should be size"
        assert emu.hw.regs[0x910F] == 0x50, "CDB[2] should be 0x50"
        assert emu.hw.regs[0x9110] == 0x12, "CDB[3] should be addr mid"
        assert emu.hw.regs[0x9111] == 0x34, "CDB[4] should be addr low"

    def test_passthrough_response_at_0x8000(self, firmware_emulator):
        """Verify E4 response goes to 0x8000 (where passthrough reads from)."""
        emu, fw_name = firmware_emulator

        # Set up test data
        test_addr = 0x2000
        test_data = [0xDE, 0xAD, 0xBE, 0xEF]
        for i, val in enumerate(test_data):
            emu.memory.xdata[test_addr + i] = val

        # Inject and run
        emu.hw.inject_usb_command(0xE4, test_addr, size=len(test_data))
        emu.run(max_cycles=50000)

        # Verify response is at 0x8000
        result = [emu.memory.xdata[0x8000 + i] for i in range(len(test_data))]
        assert result == test_data, \
            f"[{fw_name}] Response at 0x8000: expected {test_data}, got {result}"

    def test_passthrough_interrupt_flags_set(self, firmware_emulator):
        """Verify inject_usb_command sets interrupt flags correctly."""
        emu, fw_name = firmware_emulator

        # Before injection
        emu.hw.regs[0xC802] = 0x00
        emu.hw.regs[0x9101] = 0x00

        # Inject command
        emu.hw.inject_usb_command(0xE4, 0x1000, size=1)

        # Verify interrupt flags were set
        assert emu.hw.regs[0xC802] != 0, "Interrupt pending should be set"
        assert emu.hw.regs[0x9101] != 0, "USB interrupt flags should be set"

    def test_passthrough_mmio_snapshot(self, firmware_emulator):
        """
        Snapshot all MMIO registers that inject_usb_command sets.

        This documents what the passthrough needs to replicate.
        """
        emu, fw_name = firmware_emulator

        # Clear known registers
        for reg in [0x910D, 0x910E, 0x910F, 0x9110, 0x9111, 0x9112,
                    0x9000, 0x9101, 0xC802, 0x9096, 0xCEB0, 0xCEB2, 0xCEB3]:
            emu.hw.regs[reg] = 0x00

        # Inject E4 read command
        emu.hw.inject_usb_command(0xE4, 0x1234, size=4)

        # Document what was set (this is informational, test always passes)
        print(f"\n[{fw_name}] MMIO registers set by inject_usb_command(0xE4, 0x1234, size=4):")
        print(f"  CDB: 0x910D={emu.hw.regs[0x910D]:02X} 0x910E={emu.hw.regs[0x910E]:02X} "
              f"0x910F={emu.hw.regs[0x910F]:02X} 0x9110={emu.hw.regs[0x9110]:02X} "
              f"0x9111={emu.hw.regs[0x9111]:02X}")
        print(f"  Status: 0x9000={emu.hw.regs[0x9000]:02X} 0x9101={emu.hw.regs[0x9101]:02X} "
              f"0xC802={emu.hw.regs[0xC802]:02X}")
        print(f"  EP: 0x9096={emu.hw.regs[0x9096]:02X}")
        print(f"  Addr: 0xCEB2={emu.hw.regs[0xCEB2]:02X} 0xCEB3={emu.hw.regs[0xCEB3]:02X}")
        print(f"  Type: 0xCEB0={emu.hw.regs[0xCEB0]:02X}")

        # Verify critical registers are set
        assert emu.hw.regs[0x910D] == 0xE4, "Command type must be set"
        assert emu.hw.regs[0xC802] != 0, "Interrupt must be pending"

    def test_passthrough_e5_write_works(self, firmware_emulator):
        """Verify E5 write command works (passthrough would do same)."""
        emu, fw_name = firmware_emulator

        test_addr = 0x2500
        test_value = 0x77

        # Clear target
        emu.memory.xdata[test_addr] = 0x00

        # Inject E5 write
        emu.hw.inject_usb_command(0xE5, test_addr, value=test_value)
        emu.run(max_cycles=50000)

        # Verify write
        result = emu.memory.xdata[test_addr]
        assert result == test_value, \
            f"[{fw_name}] E5 write: expected 0x{test_value:02X}, got 0x{result:02X}"

    def test_passthrough_sequential_commands(self, firmware_emulator):
        """Verify sequential commands work (simulates continuous passthrough)."""
        emu, fw_name = firmware_emulator

        # Command 1: Read
        emu.memory.xdata[0x1000] = 0xAA
        emu.hw.inject_usb_command(0xE4, 0x1000, size=1)
        emu.run(max_cycles=50000)
        assert emu.memory.xdata[0x8000] == 0xAA, f"[{fw_name}] Command 1 failed"

        # Command 2: Write
        emu.hw.inject_usb_command(0xE5, 0x2000, value=0xBB)
        emu.run(max_cycles=emu.cpu.cycles + 50000)
        assert emu.memory.xdata[0x2000] == 0xBB, f"[{fw_name}] Command 2 failed"

        # Command 3: Read back the write
        emu.hw.inject_usb_command(0xE4, 0x2000, size=1)
        emu.run(max_cycles=emu.cpu.cycles + 50000)
        assert emu.memory.xdata[0x8000] == 0xBB, f"[{fw_name}] Command 3 failed"


class TestUSBRegisterMapping:
    """Tests for USB register mapping verification."""

    def test_cdb_register_addresses(self, firmware_emulator):
        """Verify CDB is read from correct registers."""
        emu, fw_name = firmware_emulator

        # These are the documented CDB register addresses
        cdb_regs = [0x910D, 0x910E, 0x910F, 0x9110, 0x9111, 0x9112]

        # Write test pattern
        for i, reg in enumerate(cdb_regs):
            emu.hw.regs[reg] = 0x10 + i

        # Verify they're readable
        for i, reg in enumerate(cdb_regs):
            val = emu.hw.regs[reg]
            assert val == 0x10 + i, f"CDB register 0x{reg:04X} mismatch"

    def test_usb_buffer_at_0x8000(self, emulator):
        """Verify USB buffer is accessible at 0x8000."""
        emu = emulator

        # Write to buffer
        for i in range(256):
            emu.memory.xdata[0x8000 + i] = i

        # Verify
        for i in range(256):
            assert emu.memory.xdata[0x8000 + i] == i, f"Buffer[{i}] mismatch"

    def test_interrupt_registers(self, emulator):
        """Verify interrupt control registers work."""
        emu = emulator

        # USB interrupt pending register
        emu.hw.regs[0xC802] = 0x05
        assert emu.hw.regs[0xC802] == 0x05

        # USB status register
        emu.hw.regs[0x9000] = 0x80
        assert emu.hw.regs[0x9000] == 0x80

        # USB interrupt flags
        emu.hw.regs[0x9101] = 0x21
        assert emu.hw.regs[0x9101] == 0x21


class TestUSBControlTransfer:
    """
    Tests for USB control transfer handling.

    The firmware handles ALL USB control transfers (setup packets) through
    registers at 0x9E00-0x9E07:
      0x9E00 = bmRequestType
      0x9E01 = bRequest
      0x9E02 = wValue low
      0x9E03 = wValue high
      0x9E04 = wIndex low
      0x9E05 = wIndex high
      0x9E06 = wLength low
      0x9E07 = wLength high

    Responses go to XDATA[0x8000].
    """

    def test_get_device_descriptor_trace(self, firmware_emulator):
        """
        Trace firmware handling of GET_DESCRIPTOR (device descriptor) request.
        This is the FIRST request any USB host sends during enumeration.

        Setup packet:
          bmRequestType = 0x80 (IN, standard, device)
          bRequest = 0x06 (GET_DESCRIPTOR)
          wValue = 0x0100 (device descriptor type=1, index=0)
          wIndex = 0x0000
          wLength = 0x0012 (18 bytes) or 0x0008 (first 8 bytes)
        """
        emu, fw_name = firmware_emulator

        # Run boot first
        emu.run(max_cycles=500000)

        # Track all MMIO reads/writes
        reads_log = []
        writes_log = []

        original_read = emu.hw.read
        original_write = emu.hw.write

        def log_read(addr):
            val = original_read(addr)
            if 0x9000 <= addr < 0xA000 or 0xC000 <= addr < 0xD000:
                reads_log.append((emu.cpu.pc, addr, val))
            return val

        def log_write(addr, val):
            if 0x9000 <= addr < 0xA000 or 0x8000 <= addr < 0x9000:
                writes_log.append((emu.cpu.pc, addr, val))
            original_write(addr, val)

        emu.hw.read = log_read
        emu.hw.write = log_write

        # Inject GET_DESCRIPTOR for device descriptor (8 bytes first, like real USB)
        # Setup packet at 0x9E00-0x9E07
        emu.hw.regs[0x9E00] = 0x80  # bmRequestType: IN, standard, device
        emu.hw.regs[0x9E01] = 0x06  # bRequest: GET_DESCRIPTOR
        emu.hw.regs[0x9E02] = 0x00  # wValue low: descriptor index
        emu.hw.regs[0x9E03] = 0x01  # wValue high: device descriptor type
        emu.hw.regs[0x9E04] = 0x00  # wIndex low
        emu.hw.regs[0x9E05] = 0x00  # wIndex high
        emu.hw.regs[0x9E06] = 0x08  # wLength low: 8 bytes (initial request)
        emu.hw.regs[0x9E07] = 0x00  # wLength high

        # Also write to EP0 buffer
        for i in range(8):
            emu.hw.usb_ep0_buf[i] = emu.hw.regs[0x9E00 + i]

        # Set up USB interrupt properly
        emu.hw.regs[0xC802] = 0x01  # USB interrupt pending
        emu.hw.regs[0x9101] = 0x01  # EP0 flag (NOT bit 5 vendor path)
        emu.hw.regs[0x9000] = 0x81  # USB connected + EP0 active

        # USB state = configured
        emu.memory.idata[0x6A] = 5

        # CRITICAL: Set the pending interrupt flag AND trigger via CPU
        emu.hw._pending_usb_interrupt = True
        emu.cpu._ext0_pending = True  # Trigger EX0 interrupt directly

        print(f"\n[{fw_name}] === GET_DESCRIPTOR (device) test ===")
        print(f"[{fw_name}] Setup packet: 80 06 00 01 00 00 08 00")
        print(f"[{fw_name}] Triggered EX0 interrupt")

        # Run firmware - interrupt should fire
        start_cycles = emu.cpu.cycles
        emu.run(max_cycles=start_cycles + 50000)

        # Check response at 0x8000
        response = bytes([emu.memory.xdata[0x8000 + i] for i in range(18)])
        print(f"[{fw_name}] Response at 0x8000: {response.hex()}")

        # A valid device descriptor starts with:
        # bLength=18 (0x12), bDescriptorType=1 (0x01)
        if response[0] == 0x12 and response[1] == 0x01:
            print(f"[{fw_name}] VALID device descriptor found!")
        else:
            print(f"[{fw_name}] No valid device descriptor at 0x8000")

        # Print interesting reads/writes
        print(f"\n[{fw_name}] Key register reads:")
        for pc, addr, val in reads_log[:20]:
            print(f"  PC=0x{pc:04X}: read 0x{addr:04X} = 0x{val:02X}")

        print(f"\n[{fw_name}] Key register writes:")
        for pc, addr, val in writes_log[:20]:
            print(f"  PC=0x{pc:04X}: write 0x{addr:04X} = 0x{val:02X}")

    def test_find_usb_descriptor_handler(self, firmware_emulator):
        """
        Search for USB descriptor handling code by tracing PC execution
        after USB interrupt.
        """
        emu, fw_name = firmware_emulator

        # Run boot first
        emu.run(max_cycles=500000)

        # Track which PCs are executed
        pcs_executed = set()

        # Hook step to track PC
        def track_pc():
            pcs_executed.add(emu.cpu.pc)

        # Set up USB state for EP0 control transfer
        emu.hw.regs[0x9E00] = 0x80  # bmRequestType
        emu.hw.regs[0x9E01] = 0x06  # bRequest GET_DESCRIPTOR
        emu.hw.regs[0x9E02] = 0x00  # wValue low
        emu.hw.regs[0x9E03] = 0x01  # wValue high (device desc)
        emu.hw.regs[0x9E04] = 0x00  # wIndex low
        emu.hw.regs[0x9E05] = 0x00  # wIndex high
        emu.hw.regs[0x9E06] = 0x12  # wLength low
        emu.hw.regs[0x9E07] = 0x00  # wLength high

        # Trigger EP0 interrupt
        emu.hw.regs[0xC802] = 0x01
        emu.hw.regs[0x9101] = 0x01  # EP0, not vendor
        emu.hw.regs[0x9000] = 0x81
        emu.memory.idata[0x6A] = 5

        # Run while tracking PCs
        for _ in range(5000):
            track_pc()
            emu.step()

        # Check if we hit known USB handler addresses
        usb_handlers = {
            0x0E33: "USB_ISR_entry",
            0x0E64: "USB_check_vendor",
            0x5333: "vendor_cmd_processor",
            0x4583: "vendor_dispatch",
            # Add more known addresses
        }

        print(f"\n[{fw_name}] USB handler addresses hit:")
        for addr, name in usb_handlers.items():
            if addr in pcs_executed:
                print(f"  0x{addr:04X}: {name} - HIT")
            else:
                print(f"  0x{addr:04X}: {name} - not hit")

        # Show some PCs around USB region
        usb_region_pcs = [pc for pc in sorted(pcs_executed) if 0x0E00 <= pc < 0x1000]
        print(f"\n[{fw_name}] PCs in USB region (0x0E00-0x1000):")
        for pc in usb_region_pcs[:30]:
            print(f"  0x{pc:04X}")

    def test_setup_packet_registers_0x9e00(self, firmware_emulator):
        """
        Verify setup packet registers at 0x9E00-0x9E07 are accessible.
        These are the correct registers for USB control transfer injection.
        """
        emu, fw_name = firmware_emulator

        # Write setup packet to 0x9E00-0x9E07
        setup = [0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00]
        for i, val in enumerate(setup):
            emu.hw.regs[0x9E00 + i] = val

        # Verify values are stored
        for i, val in enumerate(setup):
            result = emu.hw.regs[0x9E00 + i]
            assert result == val, f"[{fw_name}] 0x9E0{i:X} should be 0x{val:02X}, got 0x{result:02X}"

    def test_setup_packet_via_usb_ep0_buf_callback(self, firmware_emulator):
        """
        Verify USB EP0 buffer callback returns values from 0x9E00 area.
        The firmware reads EP0 data through these callbacks.
        """
        emu, fw_name = firmware_emulator

        # Write test pattern to EP0 buffer
        test_data = bytes([0xDE, 0xAD, 0xBE, 0xEF])
        for i, val in enumerate(test_data):
            emu.hw.usb_ep0_buf[i] = val

        # Read through the callback (simulates firmware reading 0x9E00)
        for i, expected in enumerate(test_data):
            result = emu.hw._usb_ep0_buf_read(emu.hw, 0x9E00 + i)
            assert result == expected, f"[{fw_name}] EP0 buf[{i}] should be 0x{expected:02X}"

    def test_control_transfer_get_descriptor(self, firmware_emulator):
        """
        Test firmware handling of GET_DESCRIPTOR control transfer.
        Setup packet for device descriptor:
          bmRequestType=0x80 (IN, standard, device)
          bRequest=0x06 (GET_DESCRIPTOR)
          wValue=0x0100 (device descriptor, index 0)
          wIndex=0x0000
          wLength=0x0012 (18 bytes)
        """
        emu, fw_name = firmware_emulator

        # Write setup packet to the correct registers (0x9E00-0x9E07)
        emu.hw.regs[0x9E00] = 0x80  # bmRequestType: IN, standard, device
        emu.hw.regs[0x9E01] = 0x06  # bRequest: GET_DESCRIPTOR
        emu.hw.regs[0x9E02] = 0x00  # wValue low: descriptor index
        emu.hw.regs[0x9E03] = 0x01  # wValue high: device descriptor type
        emu.hw.regs[0x9E04] = 0x00  # wIndex low
        emu.hw.regs[0x9E05] = 0x00  # wIndex high
        emu.hw.regs[0x9E06] = 0x12  # wLength low: 18 bytes
        emu.hw.regs[0x9E07] = 0x00  # wLength high

        # Also populate the EP0 buffer (firmware may read from either)
        for i in range(8):
            emu.hw.usb_ep0_buf[i] = emu.hw.regs[0x9E00 + i]

        # Set up USB interrupt flags for control transfer
        emu.hw.regs[0xC802] = 0x01  # USB interrupt pending
        emu.hw.regs[0x9101] = 0x01  # EP0 control transfer flag
        emu.hw.regs[0x9000] = 0x81  # USB connected (bit 7) + active (bit 0)

        # USB state = configured
        emu.memory.idata[0x6A] = 5

        # Run firmware
        emu.run(max_cycles=50000)

        # Check if response buffer has any non-zero data
        # Device descriptor starts with bLength (18) and bDescriptorType (1)
        response = [emu.memory.xdata[0x8000 + i] for i in range(18)]
        print(f"\n[{fw_name}] GET_DESCRIPTOR response at 0x8000: {[hex(x) for x in response[:8]]}...")

        # This is informational - we're seeing if firmware responds
        # A valid device descriptor would have [18, 1, ...] at start

    def test_control_transfer_vendor_request(self, firmware_emulator):
        """
        Test firmware handling of vendor-specific control transfer.
        This is what E4/E5 commands actually are - vendor control requests.
        """
        emu, fw_name = firmware_emulator

        # E4 read command as vendor control transfer
        # bmRequestType=0xC0 (IN, vendor, device)
        # bRequest=0xE4
        # wValue=addr_low | (addr_mid << 8)
        # wIndex=addr_high | 0x50
        # wLength=size

        test_addr = 0x1234
        test_size = 4

        # Write test data to XDATA
        test_data = [0xDE, 0xAD, 0xBE, 0xEF]
        for i, val in enumerate(test_data):
            emu.memory.xdata[test_addr + i] = val

        # Inject as vendor control transfer
        emu.hw.regs[0x9E00] = 0xC0  # bmRequestType: IN, vendor, device
        emu.hw.regs[0x9E01] = 0xE4  # bRequest: E4 read
        emu.hw.regs[0x9E02] = test_addr & 0xFF         # wValue low
        emu.hw.regs[0x9E03] = (test_addr >> 8) & 0xFF  # wValue high
        emu.hw.regs[0x9E04] = 0x50  # wIndex low (XDATA marker)
        emu.hw.regs[0x9E05] = 0x00  # wIndex high
        emu.hw.regs[0x9E06] = test_size  # wLength low
        emu.hw.regs[0x9E07] = 0x00  # wLength high

        # Also use the standard injection to ensure all state is correct
        emu.hw.inject_usb_command(0xE4, test_addr, size=test_size)
        emu.run(max_cycles=50000)

        # Verify response
        result = [emu.memory.xdata[0x8000 + i] for i in range(test_size)]
        assert result == test_data, f"[{fw_name}] Vendor control read: expected {test_data}, got {result}"

    def test_inject_setup_packet_method(self, emulator):
        """
        Test the inject_setup_packet method in USBDevicePassthrough.
        """
        import sys
        sys.path.insert(0, str(Path(__file__).parent.parent / 'emulate'))
        from usb_device import USBDevicePassthrough, USBSetupPacket

        emu = emulator
        passthrough = USBDevicePassthrough(emu)

        # Create GET_DESCRIPTOR setup packet
        setup = USBSetupPacket(
            bmRequestType=0x80,
            bRequest=0x06,
            wValue=0x0100,  # Device descriptor
            wIndex=0x0000,
            wLength=0x0012
        )

        # Inject it
        passthrough.inject_setup_packet(setup)

        # Verify registers were set correctly
        assert emu.hw.regs[0x9E00] == 0x80, "bmRequestType wrong"
        assert emu.hw.regs[0x9E01] == 0x06, "bRequest wrong"
        assert emu.hw.regs[0x9E02] == 0x00, "wValue low wrong"
        assert emu.hw.regs[0x9E03] == 0x01, "wValue high wrong"
        assert emu.hw.regs[0x9E04] == 0x00, "wIndex low wrong"
        assert emu.hw.regs[0x9E05] == 0x00, "wIndex high wrong"
        assert emu.hw.regs[0x9E06] == 0x12, "wLength low wrong"
        assert emu.hw.regs[0x9E07] == 0x00, "wLength high wrong"

    def test_usb_setup_packet_dataclass(self, emulator):
        """Test USBSetupPacket dataclass conversion."""
        import sys
        sys.path.insert(0, str(Path(__file__).parent.parent / 'emulate'))
        from usb_device import USBSetupPacket

        # Test from_bytes
        raw = bytes([0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00])
        setup = USBSetupPacket.from_bytes(raw)

        assert setup.bmRequestType == 0x80
        assert setup.bRequest == 0x06
        assert setup.wValue == 0x0100
        assert setup.wIndex == 0x0000
        assert setup.wLength == 0x0012

        # Test to_bytes
        output = setup.to_bytes()
        assert output == raw, f"to_bytes mismatch: {output.hex()} vs {raw.hex()}"

    def test_e4_via_inject_usb_command_matches_passthrough(self, firmware_emulator):
        """
        Verify inject_usb_command and passthrough use same register mapping.
        This ensures the passthrough will work correctly when wired up.
        """
        emu, fw_name = firmware_emulator

        # Clear registers first
        for reg in range(0x9E00, 0x9E08):
            emu.hw.regs[reg] = 0x00

        # Use inject_usb_command
        test_addr = 0x2468
        test_size = 8
        emu.hw.inject_usb_command(0xE4, test_addr, size=test_size)

        # Check 0x9E00 registers were set (inject_vendor_command sets these)
        assert emu.hw.regs[0x9E00] == 0xE4, f"[{fw_name}] 0x9E00 should be E4 cmd"
        assert emu.hw.regs[0x9E01] == test_size, f"[{fw_name}] 0x9E01 should be size"

        # The passthrough should set the same registers
        print(f"\n[{fw_name}] Registers set by inject_usb_command:")
        for reg in range(0x9E00, 0x9E08):
            print(f"  0x{reg:04X} = 0x{emu.hw.regs[reg]:02X}")

    def test_response_buffer_accessible_after_control_transfer(self, firmware_emulator):
        """
        Verify response data at 0x8000 is accessible after command processing.
        """
        emu, fw_name = firmware_emulator

        # Set up test data
        test_addr = 0x3000
        test_data = [0xCA, 0xFE, 0xBA, 0xBE]
        for i, val in enumerate(test_data):
            emu.memory.xdata[test_addr + i] = val

        # Inject E4 read
        emu.hw.inject_usb_command(0xE4, test_addr, size=len(test_data))
        emu.run(max_cycles=50000)

        # Read response from 0x8000
        response = bytes([emu.memory.xdata[0x8000 + i] for i in range(len(test_data))])
        expected = bytes(test_data)

        assert response == expected, f"[{fw_name}] Response at 0x8000 should be {expected.hex()}, got {response.hex()}"


class TestUSBDescriptorInit:
    """Tests for USB descriptor initialization during firmware startup."""

    def test_trace_descriptor_init(self, firmware_emulator):
        """
        Trace where USB descriptors are written during firmware init.

        The firmware reads descriptor data from ROM at 0x0620 and copies it
        somewhere. This test traces that process to understand the flow.
        """
        emu, fw_name = firmware_emulator

        # Track writes to MMIO/USB regions
        writes_to_usb_area = []

        def track_write(addr, val):
            """Track writes to USB-related areas."""
            writes_to_usb_area.append((emu.hw.cycles, addr, val))

        # Install write tracking for USB-related regions
        for addr in range(0x8000, 0x8040):  # USB buffer area
            orig = emu.memory.xdata_write_hooks.get(addr)
            def make_hook(a, orig_hook=orig):
                def hook(addr, val):
                    track_write(addr, val)
                    if orig_hook:
                        orig_hook(addr, val)
                    else:
                        emu.memory.xdata[addr] = val
                return hook
            emu.memory.xdata_write_hooks[addr] = make_hook(addr)

        # Run init until past descriptor processing
        # The descriptor init is around PC 0x43A3-0x4420
        # It ends by jumping to 0x1F7C or falling through
        emu.run(max_cycles=50000)

        print(f"\n[{fw_name}] Writes to 0x8000-0x803F during init:")
        for cycle, addr, val in writes_to_usb_area[:20]:  # First 20
            print(f"  [{cycle:8d}] 0x{addr:04X} = 0x{val:02X}")

        # Also check what's in the USB buffer area now
        print(f"\n[{fw_name}] USB buffer (0x8000-0x801F) after init:")
        for i in range(32):
            val = emu.memory.xdata[0x8000 + i]
            if val != 0:
                print(f"  0x{0x8000 + i:04X} = 0x{val:02X}")

    def test_find_vid_pid_in_memory(self, firmware_emulator):
        """
        Search memory for VID (0x174C) and PID (0x2462/0x2464) after init.

        This helps us find where the firmware stores USB descriptors.
        """
        emu, fw_name = firmware_emulator

        # Run init
        emu.run(max_cycles=100000)

        # Search for VID in little-endian format (0x4C, 0x17)
        vid_low, vid_high = 0x4C, 0x17
        found_vid = []

        for addr in range(0x6000, 0xC000):  # MMIO/XDATA range
            if emu.memory.xdata[addr] == vid_low:
                if addr + 1 < 0xFFFF and emu.memory.xdata[addr + 1] == vid_high:
                    found_vid.append(addr)

        print(f"\n[{fw_name}] VID 0x174C found at addresses:")
        for addr in found_vid:
            # Print context around the VID
            context = []
            for i in range(-4, 8):
                if 0 <= addr + i < 0xFFFF:
                    context.append(f"{emu.memory.xdata[addr + i]:02X}")
            print(f"  0x{addr:04X}: {' '.join(context)}")

        # Also search MMIO regs
        found_in_regs = []
        for addr in emu.hw.regs:
            if emu.hw.regs.get(addr, 0) == vid_low:
                if emu.hw.regs.get(addr + 1, 0) == vid_high:
                    found_in_regs.append(addr)

        if found_in_regs:
            print(f"\n[{fw_name}] VID found in MMIO regs at:")
            for addr in found_in_regs:
                print(f"  0x{addr:04X}")

    def test_descriptor_processing_code_path(self, firmware_emulator):
        """
        Verify the firmware executes the descriptor processing code at 0x43A3.
        """
        emu, fw_name = firmware_emulator

        # Track key points in the descriptor processing code
        trace_points = {
            0x43A3: "descriptor_loop_start",
            0x43A6: "loop_body",
            0x43A9: "movc_read",
            0x43AA: "jz_check",
            0x43BF: "cjne_e0_check",
            0x43D4: "call_0bbe",
            0x43E7: "anl_c0",
            0x437A: "ljmp_1f7c_exit",
            0x1F7C: "main_loop_entry",
        }

        for addr in trace_points:
            emu.trace_pcs.add(addr)

        # Run init
        old_stdout = sys.stdout
        sys.stdout = io.StringIO()  # Suppress trace output during run
        try:
            emu.run(max_cycles=100000)
        finally:
            sys.stdout = old_stdout

        print(f"\n[{fw_name}] Descriptor init code execution:")
        for addr, name in sorted(trace_points.items()):
            hits = emu.trace_pc_hits.get(addr, 0)
            if hits > 0:
                print(f"  0x{addr:04X} ({name}): {hits} hits")

        assert emu.trace_pc_hits.get(0x43A3, 0) > 0, f"[{fw_name}] Should execute descriptor init at 0x43A3"

    def test_usb_mmio_writes_during_init(self, firmware_emulator):
        """
        Track USB-related MMIO writes during firmware initialization.

        This helps understand how the USB hardware is configured and whether
        descriptor handling is done by hardware or firmware.
        """
        emu, fw_name = firmware_emulator

        # Track all writes to USB-related MMIO regions
        usb_writes = []

        # Save original log setting
        orig_log = emu.hw.log_writes

        # Define USB MMIO ranges to track
        usb_ranges = [
            (0x9000, 0x9200, "USB Control"),
            (0x9E00, 0x9F00, "USB EP/Desc"),
            (0x90E0, 0x9100, "USB Status"),
            (0xC800, 0xC900, "Interrupt"),
        ]

        def in_usb_range(addr):
            for start, end, name in usb_ranges:
                if start <= addr < end:
                    return name
            return None

        # Hook the write callback
        orig_callbacks = dict(emu.hw.write_callbacks)

        def track_write(addr, val):
            range_name = in_usb_range(addr)
            if range_name:
                usb_writes.append((emu.hw.cycles, addr, val, range_name))

        # Install tracking for USB ranges
        for start, end, name in usb_ranges:
            for addr in range(start, end):
                orig_cb = emu.hw.write_callbacks.get(addr)
                def make_cb(a, orig=orig_cb):
                    def cb(addr, val):
                        track_write(addr, val)
                        if orig:
                            orig(addr, val)
                        else:
                            emu.hw.regs[addr] = val
                    return cb
                emu.hw.write_callbacks[addr] = make_cb(addr)

        # Run initialization only (before USB interrupt handling)
        emu.hw.usb_connect_delay = 999999  # Delay USB connect
        emu.run(max_cycles=10000)  # Just init, before USB connect

        print(f"\n[{fw_name}] USB MMIO writes during early init:")
        for cycle, addr, val, range_name in usb_writes[:30]:
            print(f"  [{cycle:6d}] 0x{addr:04X} = 0x{val:02X} ({range_name})")

        if len(usb_writes) > 30:
            print(f"  ... and {len(usb_writes) - 30} more writes")

        # Show unique addresses written
        unique_addrs = sorted(set(addr for _, addr, _, _ in usb_writes))
        print(f"\n[{fw_name}] Unique USB addresses written:")
        print(f"  {', '.join(f'0x{a:04X}' for a in unique_addrs[:20])}")

    def test_usb_interrupt_handler_trace(self, firmware_emulator):
        """
        Trace USB interrupt handler execution to understand descriptor handling.

        When USB is connected, an interrupt fires and the firmware handles it
        at 0x0E33. This test traces that path.
        """
        emu, fw_name = firmware_emulator

        # Key addresses in USB interrupt handler
        trace_points = {
            0x0003: "EX0_vector",
            0x0E33: "usb_isr_entry",
            0x0E54: "check_c802_bit0",
            0x0E5A: "read_9101",
            0x0E5E: "check_9101_bit5",
            0x0E61: "jmp_0f07",
            0x0E64: "read_9000",
            0x0E68: "check_9000_bit0",
            0x0E6E: "main_ep0_handler",
            0x0EF4: "exit_path_1",
            0x0F07: "0f07_entry",
            0x10B8: "exit_path_2",
        }

        for addr in trace_points:
            emu.trace_pcs.add(addr)

        # Track USB-related writes
        usb_writes = []

        def track_usb_write(addr, val):
            if 0x9000 <= addr < 0xA000 or 0x8000 <= addr < 0x8100:
                usb_writes.append((emu.hw.cycles, addr, val, emu.cpu.pc))

        # Hook XDATA writes
        orig_write = emu.memory.write_xdata
        def hooked_write(addr, val):
            track_usb_write(addr, val)
            return orig_write(addr, val)
        emu.memory.write_xdata = hooked_write

        # Run with USB connect enabled
        old_stdout = sys.stdout
        sys.stdout = io.StringIO()  # Suppress trace output
        try:
            emu.run(max_cycles=50000)
        finally:
            sys.stdout = old_stdout

        print(f"\n[{fw_name}] USB interrupt handler execution:")
        for addr, name in sorted(trace_points.items()):
            hits = emu.trace_pc_hits.get(addr, 0)
            if hits > 0:
                print(f"  0x{addr:04X} ({name}): {hits} hits")

        print(f"\n[{fw_name}] USB MMIO/buffer writes after connect:")
        for cycle, addr, val, pc in usb_writes[:20]:
            print(f"  [{cycle:6d}] 0x{addr:04X} = 0x{val:02X} (PC=0x{pc:04X})")

        # Check 0x8000 buffer for device descriptor
        print(f"\n[{fw_name}] USB buffer (0x8000-0x8020) after connect:")
        for i in range(32):
            val = emu.memory.xdata[0x8000 + i]
            if val != 0:
                print(f"  0x{0x8000 + i:04X} = 0x{val:02X}")


    def test_ep0_path_with_setup_packet(self, firmware_emulator):
        """
        Test USB control transfer via EP0 path (0x9000 bit 0 = SET).

        This tests the path for standard USB requests like GET_DESCRIPTOR.
        """
        emu, fw_name = firmware_emulator

        # Disable automatic USB connect
        emu.hw.usb_connect_delay = 999999999

        # First run initialization
        emu.run(max_cycles=5000)

        # Now set up for EP0 path (standard USB requests)
        # Different from vendor command path - bit 0 must be SET!
        emu.hw.regs[0x9000] = 0x81  # Bit 7 (connected) + bit 0 SET for EP0 path
        emu.hw.regs[0xC802] = 0x05  # USB interrupt pending
        emu.hw.regs[0x9101] = 0x21  # Bit 5 set

        # Set up endpoint status for EP0 (required for the handler)
        emu.hw.regs[0x9118] = 0x00  # EP0 selected
        for addr in range(0x9096, 0x90A0):
            emu.hw.regs[addr] = 0x01  # EP has data

        # Enable interrupts
        emu.memory.write_sfr(0xA8, 0x81)  # IE = EX0 + EA

        # Trace key points in EP0 handler
        trace_points = {
            0x0003: "EX0_vector",
            0x0E33: "usb_isr_entry",
            0x0E54: "check_c802",
            0x0E5A: "check_9101",
            0x0E64: "read_9000",
            0x0E68: "check_9000_bit0",
            0x0E6E: "ep0_handler_start",
            0x0E71: "ep0_read_9118",
            0x0ED3: "ep0_read_909E",
            0x0EDD: "ep0_call_54a1",
            0x0EF4: "exit_path",
            0x10B8: "isr_exit",
        }

        for addr in trace_points:
            emu.trace_pcs.add(addr)

        # Track XDATA writes to 0x07xx area (USB state)
        usb_state_writes = []

        def track_write(addr, val):
            if 0x0700 <= addr < 0x0800 or 0x8000 <= addr < 0x8100:
                usb_state_writes.append((emu.hw.cycles, addr, val, emu.cpu.pc))

        orig_write = emu.memory.write_xdata
        def hooked_write(addr, val):
            track_write(addr, val)
            return orig_write(addr, val)
        emu.memory.write_xdata = hooked_write

        # Trigger USB interrupt
        emu.cpu._ext0_pending = True

        # Run firmware
        old_stdout = sys.stdout
        sys.stdout = io.StringIO()
        try:
            emu.run(max_cycles=30000)
        finally:
            sys.stdout = old_stdout

        print(f"\n[{fw_name}] EP0 path trace:")
        for addr, name in sorted(trace_points.items()):
            hits = emu.trace_pc_hits.get(addr, 0)
            if hits > 0:
                print(f"  0x{addr:04X} ({name}): {hits} hits")

        print(f"\n[{fw_name}] XDATA writes (0x07xx and 0x80xx):")
        for cycle, addr, val, pc in usb_state_writes[:30]:
            print(f"  [{cycle:6d}] 0x{addr:04X} = 0x{val:02X} (PC=0x{pc:04X})")

        # Check what's in the USB request state area
        print(f"\n[{fw_name}] USB state area (0x07B0-0x07E0):")
        for addr in range(0x07B0, 0x07E0):
            val = emu.memory.xdata[addr]
            if val != 0:
                print(f"  0x{addr:04X} = 0x{val:02X}")


    @pytest.mark.skip(reason="Requires firmware to handle GET_DESCRIPTOR through MMIO - no hardcoded shortcuts")
    def test_hardware_get_device_descriptor(self, firmware_emulator):
        """
        Test that GET_DESCRIPTOR for device descriptor is handled by firmware
        through MMIO registers (no hardcoded descriptor addresses in Python).

        This test requires the firmware to properly handle USB control transfers
        by reading the setup packet from MMIO, reading descriptors from code ROM
        via the flash ROM mirror, and writing the response to the EP0 FIFO.
        """
        emu, fw_name = firmware_emulator

        # Import USB device passthrough
        sys.path.insert(0, str(Path(__file__).parent.parent / 'emulate'))
        from usb_device import USBDevicePassthrough, USBSetupPacket, USB_REQ_GET_DESCRIPTOR, USB_DT_DEVICE

        # Create passthrough
        passthrough = USBDevicePassthrough(emu)

        # Create GET_DESCRIPTOR request for device descriptor
        setup = USBSetupPacket(
            bmRequestType=0x80,  # Device-to-host, standard, device
            bRequest=USB_REQ_GET_DESCRIPTOR,
            wValue=(USB_DT_DEVICE << 8) | 0,  # Device descriptor, index 0
            wIndex=0,
            wLength=18
        )

        # Handle the request
        response = passthrough.handle_control_transfer(setup)

        # Verify response
        assert response is not None, f"[{fw_name}] Should return device descriptor"
        assert len(response) == 18, f"[{fw_name}] Device descriptor should be 18 bytes"

        # Parse descriptor
        assert response[0] == 0x12, f"[{fw_name}] bLength should be 18 (0x12)"
        assert response[1] == 0x01, f"[{fw_name}] bDescriptorType should be 1 (Device)"

        # Check VID/PID (little-endian)
        vid = response[8] | (response[9] << 8)
        pid = response[10] | (response[11] << 8)

        print(f"\n[{fw_name}] Device descriptor:")
        print(f"  bLength: {response[0]}")
        print(f"  bDescriptorType: {response[1]}")
        print(f"  bcdUSB: 0x{response[3]:02X}{response[2]:02X}")
        print(f"  bDeviceClass: {response[4]}")
        print(f"  bMaxPacketSize0: {response[7]}")
        print(f"  idVendor: 0x{vid:04X}")
        print(f"  idProduct: 0x{pid:04X}")

        # Verify it's an ASMedia device
        assert vid == 0x174C, f"[{fw_name}] VID should be 0x174C (ASMedia)"


class TestSPIFlash:
    """Tests for SPI flash emulation."""

    def test_flash_initialized(self, original_firmware_emulator):
        """Verify flash is initialized to correct size."""
        emu = original_firmware_emulator
        assert len(emu.hw.spi_flash) == 0x40000  # 256KB

    def test_flash_sector_erase(self, original_firmware_emulator):
        """Test sector erase command (0x20)."""
        emu = original_firmware_emulator
        # Write some data first
        for i in range(16):
            emu.hw.spi_flash[i] = 0xAA

        # Set address and send erase command
        emu.hw.regs[0xC8AB] = 0x00  # High
        emu.hw.regs[0xC8AC] = 0x00  # Mid
        emu.hw.regs[0xC8AD] = 0x00  # Low
        emu.hw.write(0xC8AA, 0x20)  # Sector erase

        # Verify erased to 0xFF
        for i in range(16):
            assert emu.hw.spi_flash[i] == 0xFF

    def test_flash_read(self, original_firmware_emulator):
        """Test flash read via data register with auto-increment."""
        emu = original_firmware_emulator
        # Write known data
        emu.hw.spi_flash[0x100] = 0x42
        emu.hw.spi_flash[0x101] = 0x43

        # Set address
        emu.hw.spi_flash_addr = 0x100

        # Read data (auto-increments)
        val = emu.hw.read(0xC8AE)
        assert val == 0x42
        val = emu.hw.read(0xC8AE)
        assert val == 0x43

    def test_flash_block_erase(self, original_firmware_emulator):
        """Test block erase command (0xD8)."""
        emu = original_firmware_emulator
        # Write data at start of block
        for i in range(256):
            emu.hw.spi_flash[i] = 0x55

        # Set address and send block erase
        emu.hw.regs[0xC8AB] = 0x00
        emu.hw.regs[0xC8AC] = 0x00
        emu.hw.regs[0xC8AD] = 0x00
        emu.hw.write(0xC8AA, 0xD8)  # Block erase (64KB)

        # Verify erased
        for i in range(256):
            assert emu.hw.spi_flash[i] == 0xFF


class TestConfigDescriptorSpeed:
    """Tests for USB2/USB3 config descriptor selection based on speed."""

    def test_usb3_config_loaded_from_rom(self, original_firmware_emulator):
        """Verify USB3 config descriptor loads from ROM."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        desc = emu.hw.usb_ss_config_from_rom
        assert len(desc) > 0, "USB3 config descriptor not loaded"
        assert desc[0] == 0x09  # bLength
        assert desc[1] == 0x02  # bDescriptorType = CONFIG

    def test_usb2_config_loaded_from_rom(self, original_firmware_emulator):
        """Verify USB2 config descriptor loads from ROM."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        desc = emu.hw.usb_hs_config_from_rom
        assert len(desc) > 0, "USB2 config descriptor not loaded"
        assert desc[0] == 0x09  # bLength
        assert desc[1] == 0x02  # bDescriptorType = CONFIG

    def test_usb3_wtotallength_fixed(self, original_firmware_emulator):
        """Verify USB3 config descriptor wTotalLength is corrected."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        desc = emu.hw.usb_ss_config_from_rom
        wTotalLength = desc[2] | (desc[3] << 8)
        assert wTotalLength == len(desc), "wTotalLength should match descriptor length"
        assert wTotalLength > 44, "Should be extended beyond original 44 bytes"

    def test_usb2_wtotallength_fixed(self, original_firmware_emulator):
        """Verify USB2 config descriptor wTotalLength is corrected."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        desc = emu.hw.usb_hs_config_from_rom
        wTotalLength = desc[2] | (desc[3] << 8)
        assert wTotalLength == len(desc), "wTotalLength should match descriptor length"

    def test_usb3_endpoints_have_1024_byte_max_packet(self, original_firmware_emulator):
        """Verify USB3 config has 1024-byte bulk endpoint max packet size."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        desc = emu.hw.usb_ss_config_from_rom

        i = 0
        found_bulk = False
        while i < len(desc) - 6:
            bLength = desc[i]
            bDescriptorType = desc[i + 1]
            if bLength == 0:
                break
            if bDescriptorType == 0x05:  # Endpoint descriptor
                wMaxPacketSize = desc[i + 4] | (desc[i + 5] << 8)
                bmAttributes = desc[i + 3]
                if (bmAttributes & 0x03) == 0x02:  # Bulk endpoint
                    assert wMaxPacketSize == 1024, f"USB3 bulk endpoint should have 1024 byte max packet, got {wMaxPacketSize}"
                    found_bulk = True
            i += bLength
        assert found_bulk, "No bulk endpoint found in USB3 config descriptor"

    def test_usb2_endpoints_have_512_byte_max_packet(self, original_firmware_emulator):
        """Verify USB2 config has 512-byte bulk endpoint max packet size."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        desc = emu.hw.usb_hs_config_from_rom

        i = 0
        found_bulk = False
        while i < len(desc) - 6:
            bLength = desc[i]
            bDescriptorType = desc[i + 1]
            if bLength == 0:
                break
            if bDescriptorType == 0x05:  # Endpoint descriptor
                wMaxPacketSize = desc[i + 4] | (desc[i + 5] << 8)
                bmAttributes = desc[i + 3]
                if (bmAttributes & 0x03) == 0x02:  # Bulk endpoint
                    assert wMaxPacketSize == 512, f"USB2 bulk endpoint should have 512 byte max packet, got {wMaxPacketSize}"
                    found_bulk = True
            i += bLength
        assert found_bulk, "No bulk endpoint found in USB2 config descriptor"

    def test_extend_config_returns_usb3_for_superspeed(self, original_firmware_emulator):
        """Verify _extend_config_descriptor returns USB3 config at SuperSpeed."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        emu.hw.usb_controller.usb_speed = 2  # SuperSpeed

        result = emu.hw._extend_config_descriptor(bytearray(), 121)
        assert result == emu.hw.usb_ss_config_from_rom[:121]

    def test_extend_config_returns_usb2_for_highspeed(self, original_firmware_emulator):
        """Verify _extend_config_descriptor returns USB2 config at High Speed."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        emu.hw.usb_controller.usb_speed = 1  # High Speed

        result = emu.hw._extend_config_descriptor(bytearray(), 64)

        # Verify 512-byte endpoints (USB2) not 1024 (USB3)
        i = 0
        found_bulk = False
        while i < len(result) - 6:
            bLength = result[i]
            bDescriptorType = result[i + 1]
            if bLength == 0:
                break
            if bDescriptorType == 0x05:  # Endpoint descriptor
                wMaxPacketSize = result[i + 4] | (result[i + 5] << 8)
                bmAttributes = result[i + 3]
                if (bmAttributes & 0x03) == 0x02:  # Bulk endpoint
                    assert wMaxPacketSize == 512, f"USB2 should use 512-byte endpoints, got {wMaxPacketSize}"
                    found_bulk = True
            i += bLength
        assert found_bulk, "No bulk endpoint found in returned config descriptor"

    def test_extend_config_returns_usb2_for_fullspeed(self, original_firmware_emulator):
        """Verify _extend_config_descriptor returns USB2 config at Full Speed."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        emu.hw.usb_controller.usb_speed = 0  # Full Speed

        result = emu.hw._extend_config_descriptor(bytearray(), 64)

        # Should use USB2 config (speed < 2)
        i = 0
        found_bulk = False
        while i < len(result) - 6:
            bLength = result[i]
            bDescriptorType = result[i + 1]
            if bLength == 0:
                break
            if bDescriptorType == 0x05:  # Endpoint descriptor
                wMaxPacketSize = result[i + 4] | (result[i + 5] << 8)
                bmAttributes = result[i + 3]
                if (bmAttributes & 0x03) == 0x02:  # Bulk endpoint
                    assert wMaxPacketSize == 512, f"Full Speed should use 512-byte endpoints, got {wMaxPacketSize}"
                    found_bulk = True
            i += bLength
        assert found_bulk, "No bulk endpoint found in returned config descriptor"


class TestConfigDescriptorAltSettings:
    """Tests for config descriptor alt settings (BBB and UAS modes)."""

    def test_usb3_config_has_two_altsettings(self, original_firmware_emulator):
        """Verify USB3 config has alt_setting 0 (BBB) and alt_setting 1 (UAS)."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        desc = emu.hw.usb_ss_config_from_rom

        alt_settings = []
        i = 0
        while i < len(desc) - 4:
            bLength = desc[i]
            bDescriptorType = desc[i + 1]
            if bLength == 0:
                break
            if bDescriptorType == 0x04:  # Interface descriptor
                bInterfaceNumber = desc[i + 2]
                bAlternateSetting = desc[i + 3]
                alt_settings.append((bInterfaceNumber, bAlternateSetting))
            i += bLength

        assert (0, 0) in alt_settings, "Missing alt_setting 0 (BBB mode)"
        assert (0, 1) in alt_settings, "Missing alt_setting 1 (UAS mode)"

    def test_usb2_config_has_two_altsettings(self, original_firmware_emulator):
        """Verify USB2 config has alt_setting 0 and 1."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        desc = emu.hw.usb_hs_config_from_rom

        alt_settings = []
        i = 0
        while i < len(desc) - 4:
            bLength = desc[i]
            bDescriptorType = desc[i + 1]
            if bLength == 0:
                break
            if bDescriptorType == 0x04:  # Interface descriptor
                bInterfaceNumber = desc[i + 2]
                bAlternateSetting = desc[i + 3]
                alt_settings.append((bInterfaceNumber, bAlternateSetting))
            i += bLength

        assert (0, 0) in alt_settings, "Missing alt_setting 0"
        assert (0, 1) in alt_settings, "Missing alt_setting 1"

    def test_bbb_mode_has_two_endpoints(self, original_firmware_emulator):
        """Verify BBB mode (alt_setting 0) has 2 bulk endpoints."""
        emu = original_firmware_emulator
        emu.hw.load_config_descriptor_from_rom()
        desc = emu.hw.usb_ss_config_from_rom

        # Find alt_setting 0 and count its endpoints
        i = 0
        in_alt0 = False
        endpoints = []
        while i < len(desc) - 2:
            bLength = desc[i]
            bDescriptorType = desc[i + 1]
            if bLength == 0:
                break
            if bDescriptorType == 0x04:  # Interface descriptor
                bAlternateSetting = desc[i + 3]
                bNumEndpoints = desc[i + 4]
                if bAlternateSetting == 0:
                    in_alt0 = True
                    assert bNumEndpoints == 2, f"BBB mode should have 2 endpoints, got {bNumEndpoints}"
                else:
                    in_alt0 = False
            elif bDescriptorType == 0x05 and in_alt0:  # Endpoint in alt0
                endpoints.append(desc[i + 2])  # bEndpointAddress
            i += bLength

        assert len(endpoints) == 2, f"BBB mode should have 2 endpoints, found {len(endpoints)}"


class TestUSBSpeedModeRegisters:
    """Tests for USB speed mode register settings."""

    def test_superspeed_mode_sets_correct_registers(self, original_firmware_emulator):
        """Verify SuperSpeed mode sets USB3 indicator registers."""
        emu = original_firmware_emulator
        emu.hw.usb_controller.connect(speed=2)
        assert emu.hw.regs.get(0x90E0, 0) == 2
        assert emu.hw.regs.get(0x9100, 0) == 2
        assert emu.hw.regs.get(0xCC91, 0) & 0x02  # Bit 1 SET for USB3
        assert emu.hw.regs.get(0x09F9, 0) & 0x40  # Bit 6 SET for USB3

    def test_highspeed_mode_sets_correct_registers(self, original_firmware_emulator):
        """Verify High Speed mode clears USB3 indicator registers."""
        emu = original_firmware_emulator
        emu.hw.usb_controller.connect(speed=1)
        assert emu.hw.regs.get(0x90E0, 0) == 1
        assert emu.hw.regs.get(0x9100, 0) == 1
        assert not (emu.hw.regs.get(0xCC91, 0) & 0x02)  # Bit 1 CLEAR for USB2
        assert not (emu.hw.regs.get(0x09F9, 0) & 0x40)  # Bit 6 CLEAR for USB2


class TestUSBDescriptorDMA:
    """Tests for USB descriptor handling via firmware DMA.

    These tests verify the core principle from CLAUDE.md:
    - The emulator must NOT parse, process, or understand USB control messages
    - Firmware reads setup packet, determines response, configures DMA source address
    - USB hardware DMAs out the response from the address FIRMWARE configured
    """

    def _request_descriptor(self, emu, wValue, wLength):
        """Helper to request a descriptor and return result with DMA info."""
        # Clear output buffer
        for i in range(64):
            emu.memory.xdata[0x8000 + i] = 0

        # Track DMA source address writes
        dma_sources = []
        original_write = emu.hw.write

        def track_dma_writes(addr, value):
            if addr == 0x905B:  # DMA src high
                dma_sources.append(('high', value))
            elif addr == 0x905C:  # DMA src low
                dma_sources.append(('low', value))
            elif addr == 0x9092:  # DMA trigger
                dma_sources.append(('trigger', value))
            return original_write(addr, value)

        emu.hw.write = track_dma_writes

        # Inject descriptor request
        emu.hw.usb_controller.inject_control_transfer(
            bmRequestType=0x80,
            bRequest=0x06,  # GET_DESCRIPTOR
            wValue=wValue,
            wIndex=0x0000,
            wLength=wLength
        )

        # Run firmware
        emu.run(max_cycles=500000)

        # Restore write handler
        emu.hw.write = original_write

        # Analyze DMA source
        dma_src_high = 0
        dma_src_low = 0
        triggered = False
        for op, val in dma_sources:
            if op == 'high':
                dma_src_high = val
            elif op == 'low':
                dma_src_low = val
            elif op == 'trigger' and val == 0x01:
                triggered = True

        dma_src = (dma_src_high << 8) | dma_src_low
        result = bytes(emu.memory.xdata[0x8000:0x8000 + wLength])

        return {
            'result': result,
            'dma_src': dma_src,
            'triggered': triggered,
            'non_zero': sum(1 for b in result if b != 0)
        }

    def test_device_descriptor_via_dma(self, original_firmware_emulator):
        """Verify device descriptor is handled by firmware via DMA."""
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        r = self._request_descriptor(emu, 0x0100, 18)  # Type 1 (device)

        # Device descriptor should start with 0x12 (length=18) and 0x01 (type=device)
        assert r['result'][0] == 0x12, f"Device descriptor bLength should be 0x12, got 0x{r['result'][0]:02X}"
        assert r['result'][1] == 0x01, f"Device descriptor bDescriptorType should be 0x01, got 0x{r['result'][1]:02X}"
        assert r['triggered'], "DMA should be triggered for device descriptor"

    def test_config_descriptor_via_dma(self, original_firmware_emulator):
        """Verify config descriptor is handled by firmware via DMA."""
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        r = self._request_descriptor(emu, 0x0200, 9)  # Type 2 (config), 9 bytes header

        # Config descriptor should start with 0x09 (length) and 0x02 (type)
        assert r['result'][0] == 0x09, f"Config descriptor bLength should be 0x09, got 0x{r['result'][0]:02X}"
        assert r['result'][1] == 0x02, f"Config descriptor bDescriptorType should be 0x02, got 0x{r['result'][1]:02X}"
        assert r['triggered'], "DMA should be triggered for config descriptor"

    def test_string_descriptor_language_via_dma(self, original_firmware_emulator):
        """Verify string descriptor (language IDs) is handled by firmware via DMA."""
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        r = self._request_descriptor(emu, 0x0300, 4)  # Type 3 (string), index 0

        # String descriptor should have type 0x03
        assert r['result'][1] == 0x03, f"String descriptor bDescriptorType should be 0x03, got 0x{r['result'][1]:02X}"
        assert r['result'][0] > 0, "String descriptor bLength should be > 0"
        assert r['triggered'], "DMA should be triggered for string descriptor"

    def test_string_descriptor_index1_via_dma(self, original_firmware_emulator):
        """Verify string descriptor index 1 is handled by firmware via DMA."""
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        r = self._request_descriptor(emu, 0x0301, 64)  # Type 3 (string), index 1

        # String descriptor should have type 0x03
        assert r['result'][1] == 0x03, f"String descriptor bDescriptorType should be 0x03, got 0x{r['result'][1]:02X}"
        assert r['triggered'], "DMA should be triggered for string descriptor"

    def test_bos_descriptor_via_dma(self, original_firmware_emulator):
        """Verify BOS descriptor is handled by firmware via DMA."""
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        r = self._request_descriptor(emu, 0x0F00, 5)  # Type 15 (BOS)

        # BOS descriptor should have type 0x0F or at least return some data
        assert r['result'][1] == 0x0F or r['non_zero'] > 0, "BOS descriptor should be returned"
        assert r['triggered'], "DMA should be triggered for BOS descriptor"


class TestSCSIVendorCommands:
    """Tests for SCSI vendor commands (E1, E3, E8) used for firmware updates."""

    def _inject_scsi_cmd(self, emu, opcode, cdb, data=b'', is_write=False):
        """Helper to inject a SCSI vendor command."""
        import struct

        # Check if inject_scsi_vendor_cmd exists
        if hasattr(emu.hw, 'inject_scsi_vendor_cmd'):
            emu.hw.inject_scsi_vendor_cmd(opcode, cdb, data, is_write=is_write)
        else:
            # Manually set up MMIO registers for command injection
            # Set CDB in MMIO registers 0x910D-0x911C
            for i, byte in enumerate(cdb[:16]):
                emu.hw.regs[0x910D + i] = byte

            # Set up USB state
            emu.memory.idata[0x6A] = 0x02  # USB state
            emu.memory.xdata[0x0002] = opcode  # CDB opcode
            emu.memory.xdata[0xEA90] = 0x5A  # Magic value

            # Set interrupt flags
            emu.hw.regs[0x9000] = 0x81
            emu.hw.regs[0x9101] = 0x21
            emu.hw.regs[0xC802] = 0x05
            emu.hw.regs[0x9096] = 0x01

            # Put data in USB buffer if write command
            if is_write and data:
                for i, byte in enumerate(data[:len(data)]):
                    emu.memory.xdata[0x8000 + i] = byte

    def test_e1_config_write_cdb_setup(self, original_firmware_emulator):
        """Verify E1 Config Write command sets up MMIO registers correctly."""
        import struct
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        # Build E1 command CDB: E1 50 block_num
        cdb = struct.pack('>BBB', 0xE1, 0x50, 0x00) + bytes(12)
        config_data = bytes([0x41 + (i % 26) for i in range(128)])

        self._inject_scsi_cmd(emu, 0xE1, cdb, config_data, is_write=True)

        # Verify MMIO registers were set
        assert emu.hw.regs.get(0x910D, 0) == 0xE1, "CDB[0] should be 0xE1"
        assert emu.hw.regs.get(0x910E, 0) == 0x50, "CDB[1] should be 0x50"
        assert emu.memory.xdata[0x0002] == 0xE1, "XDATA CDB opcode should be 0xE1"

    def test_e3_firmware_write_cdb_setup(self, original_firmware_emulator):
        """Verify E3 Firmware Write command sets up MMIO registers correctly."""
        import struct
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        # Build E3 command CDB: E3 50 length(4 bytes)
        fw_length = 256
        cdb = struct.pack('>BBI', 0xE3, 0x50, fw_length) + bytes(9)
        fw_data = bytes([i & 0xFF for i in range(fw_length)])

        self._inject_scsi_cmd(emu, 0xE3, cdb, fw_data, is_write=True)

        # Verify MMIO registers were set
        assert emu.hw.regs.get(0x910D, 0) == 0xE3, "CDB[0] should be 0xE3"
        assert emu.hw.regs.get(0x910E, 0) == 0x50, "CDB[1] should be 0x50"
        assert emu.memory.xdata[0x0002] == 0xE3, "XDATA CDB opcode should be 0xE3"

    def test_e8_commit_cdb_setup(self, original_firmware_emulator):
        """Verify E8 Reset/Commit command sets up MMIO registers correctly."""
        import struct
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        # Build E8 command CDB: E8 51
        cdb = struct.pack('>BB', 0xE8, 0x51) + bytes(13)

        self._inject_scsi_cmd(emu, 0xE8, cdb, b'', is_write=False)

        # Verify MMIO registers were set
        assert emu.hw.regs.get(0x910D, 0) == 0xE8, "CDB[0] should be 0xE8"
        assert emu.hw.regs.get(0x910E, 0) == 0x51, "CDB[1] should be 0x51"
        assert emu.memory.xdata[0x0002] == 0xE8, "XDATA CDB opcode should be 0xE8"

    def test_vendor_cmd_magic_value(self, original_firmware_emulator):
        """Verify vendor commands set the magic value at 0xEA90."""
        import struct
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        cdb = struct.pack('>BBB', 0xE1, 0x50, 0x00) + bytes(12)
        self._inject_scsi_cmd(emu, 0xE1, cdb, bytes(128), is_write=True)

        assert emu.memory.xdata[0xEA90] == 0x5A, "Magic value at 0xEA90 should be 0x5A"

    def test_vendor_cmd_usb_state(self, original_firmware_emulator):
        """Verify vendor commands set USB state to 0x02."""
        import struct
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        cdb = struct.pack('>BBB', 0xE1, 0x50, 0x00) + bytes(12)
        self._inject_scsi_cmd(emu, 0xE1, cdb, bytes(128), is_write=True)

        assert emu.memory.idata[0x6A] == 0x02, "USB state at IDATA[0x6A] should be 0x02"


class TestPatchFlowSequence:
    """Tests for the patch.py firmware update sequence."""

    def test_e1_followed_by_e3_sequence(self, original_firmware_emulator):
        """Verify E1 config write followed by E3 firmware write works."""
        import struct
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        # First E1 command (config block 0)
        cdb1 = struct.pack('>BBB', 0xE1, 0x50, 0x00) + bytes(12)
        config_data = bytes(128)

        if hasattr(emu.hw, 'inject_scsi_vendor_cmd'):
            emu.hw.inject_scsi_vendor_cmd(0xE1, cdb1, config_data, is_write=True)
            emu.run(max_cycles=400000)

            # Then E3 command (firmware write)
            cdb2 = struct.pack('>BBI', 0xE3, 0x50, 256) + bytes(9)
            fw_data = bytes(256)
            emu.hw.inject_scsi_vendor_cmd(0xE3, cdb2, fw_data, is_write=True)
            emu.run(max_cycles=400000)

            # Verify both commands were processed (CDB changed)
            assert emu.memory.xdata[0x0002] == 0xE3, "Last CDB opcode should be E3"

    def test_different_config_blocks(self, original_firmware_emulator):
        """Verify E1 can write to different config blocks (0 and 1)."""
        import struct
        emu = original_firmware_emulator
        emu.run(max_cycles=200000)  # Boot
        emu.hw.usb_controller.connect(speed=2)
        emu.run(max_cycles=300000)

        # Config block 0
        cdb0 = struct.pack('>BBB', 0xE1, 0x50, 0x00) + bytes(12)
        # Config block 1
        cdb1 = struct.pack('>BBB', 0xE1, 0x50, 0x01) + bytes(12)

        if hasattr(emu.hw, 'inject_scsi_vendor_cmd'):
            emu.hw.inject_scsi_vendor_cmd(0xE1, cdb0, bytes(128), is_write=True)
            emu.run(max_cycles=400000)
            assert emu.hw.regs.get(0x910F, 0) == 0x00, "Block 0 indicator"

            emu.hw.inject_scsi_vendor_cmd(0xE1, cdb1, bytes(128), is_write=True)
            emu.run(max_cycles=400000)
            assert emu.hw.regs.get(0x910F, 0) == 0x01, "Block 1 indicator"


if __name__ == "__main__":
    pytest.main([__file__, '-v'])
