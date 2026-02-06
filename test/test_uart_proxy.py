#!/usr/bin/env python3
"""
Tests for UART proxy functionality.

Tests the proxy firmware and Python UARTProxy class work correctly together.
Requires real hardware connected via FTDI with proxy firmware flashed.

To run these tests:
    1. Build and flash proxy firmware:
       cd clean && make proxy && make flash-proxy

    2. Run tests:
       pytest test/test_uart_proxy.py -v

Tests are skipped if no hardware is connected.
"""

import sys
from pathlib import Path
import pytest

# Add emulate directory to path
sys.path.insert(0, str(Path(__file__).parent.parent / 'emulate'))

# Try to import UARTProxy and check for hardware
HAS_HARDWARE = False
HARDWARE_ERROR = None

try:
    from uart_proxy import UARTProxy
    # Try to open connection
    proxy = UARTProxy()
    if proxy.test_connection():
        HAS_HARDWARE = True
    proxy.close()
except Exception as e:
    HARDWARE_ERROR = str(e)


# Skip marker for all hardware tests
requires_hardware = pytest.mark.skipif(
    not HAS_HARDWARE,
    reason=f"Requires FTDI hardware with proxy firmware: {HARDWARE_ERROR or 'not connected'}"
)


@pytest.fixture
def proxy():
    """Create UART proxy connection for tests."""
    if not HAS_HARDWARE:
        pytest.skip("No FTDI hardware connected")
    p = UARTProxy()
    yield p
    p.close()


@requires_hardware
class TestEchoCommand:
    """Tests for the echo (loopback) command."""

    def test_echo_zero(self, proxy):
        """Echo 0x00."""
        result = proxy.echo(0x00)
        assert result == 0x00

    def test_echo_ff(self, proxy):
        """Echo 0xFF."""
        result = proxy.echo(0xFF)
        assert result == 0xFF

    def test_echo_pattern_55(self, proxy):
        """Echo 0x55 (alternating bits)."""
        result = proxy.echo(0x55)
        assert result == 0x55

    def test_echo_pattern_aa(self, proxy):
        """Echo 0xAA (alternating bits)."""
        result = proxy.echo(0xAA)
        assert result == 0xAA

    def test_echo_all_values(self, proxy):
        """Echo all byte values."""
        for i in range(256):
            result = proxy.echo(i)
            assert result == i, f"Echo failed for 0x{i:02X}: got 0x{result:02X}"


@requires_hardware
class TestReadCommand:
    """Tests for the read command."""

    def test_read_low_xdata(self, proxy):
        """Read from low XDATA address."""
        # Just verify we get a byte back
        val = proxy.read(0x0000)
        assert isinstance(val, int)
        assert 0 <= val <= 255

    def test_read_mmio_register(self, proxy):
        """Read from MMIO register range."""
        # Read USB status - should return something
        val = proxy.read(0x9000)
        assert isinstance(val, int)
        assert 0 <= val <= 255

    def test_read_uart_lsr_faked(self, proxy):
        """Read UART LSR should return faked value 0x60."""
        val = proxy.read(0xC009)
        assert val == 0x60, f"Expected UART LSR=0x60, got 0x{val:02X}"

    def test_read_uart_other_faked(self, proxy):
        """Read other UART registers should return 0x00."""
        for addr in [0xC000, 0xC001, 0xC002, 0xC004, 0xC007, 0xC008]:
            val = proxy.read(addr)
            assert val == 0x00, f"Expected UART[0x{addr:04X}]=0x00, got 0x{val:02X}"

    def test_read_sequential(self, proxy):
        """Read multiple sequential addresses."""
        for i in range(16):
            val = proxy.read(0x9000 + i)
            assert isinstance(val, int)


@requires_hardware
class TestWriteCommand:
    """Tests for the write command."""

    def test_write_xdata(self, proxy):
        """Write to XDATA address."""
        # Use a safe RAM location
        addr = 0x0500
        proxy.write(addr, 0x42)
        # No exception = success

    def test_write_read_roundtrip(self, proxy):
        """Write value and read it back."""
        addr = 0x0500
        test_val = 0xA5

        proxy.write(addr, test_val)
        result = proxy.read(addr)
        assert result == test_val, f"Expected 0x{test_val:02X}, got 0x{result:02X}"

    def test_write_read_multiple_values(self, proxy):
        """Write and read multiple values."""
        base = 0x0500
        test_values = [0x00, 0xFF, 0x55, 0xAA, 0x12, 0x34, 0x56, 0x78]

        # Write all values
        for i, val in enumerate(test_values):
            proxy.write(base + i, val)

        # Read all values back
        for i, expected in enumerate(test_values):
            result = proxy.read(base + i)
            assert result == expected, f"At offset {i}: expected 0x{expected:02X}, got 0x{result:02X}"

    def test_write_uart_ignored(self, proxy):
        """Write to UART registers should be ignored (no crash)."""
        # These writes should be silently ignored
        for addr in [0xC000, 0xC001, 0xC002, 0xC007]:
            proxy.write(addr, 0xFF)
            # No exception = success


@requires_hardware
class TestBlockOperations:
    """Tests for block read/write operations."""

    def test_read_block(self, proxy):
        """Read a block of bytes."""
        # First write known values
        base = 0x0600
        test_data = bytes([0x10, 0x20, 0x30, 0x40, 0x50])
        proxy.write_block(base, test_data)

        # Read them back as block
        result = proxy.read_block(base, len(test_data))
        assert result == test_data, f"Block read failed: {result.hex()} != {test_data.hex()}"

    def test_write_block(self, proxy):
        """Write a block of bytes."""
        base = 0x0700
        test_data = bytes(range(16))  # 0x00-0x0F

        proxy.write_block(base, test_data)

        # Verify with individual reads
        for i, expected in enumerate(test_data):
            result = proxy.read(base + i)
            assert result == expected, f"At offset {i}: expected 0x{expected:02X}, got 0x{result:02X}"


@requires_hardware
class TestMMIORegisters:
    """Tests for reading actual MMIO registers."""

    def test_read_usb_registers(self, proxy):
        """Read USB interface registers."""
        # These should return real hardware values
        for offset in range(0x10):
            val = proxy.read(0x9000 + offset)
            assert isinstance(val, int)

    def test_read_pcie_registers(self, proxy):
        """Read PCIe passthrough registers."""
        for offset in range(0x10):
            val = proxy.read(0xB200 + offset)
            assert isinstance(val, int)

    def test_read_interrupt_registers(self, proxy):
        """Read interrupt controller registers."""
        for offset in range(0x10):
            val = proxy.read(0xC800 + offset)
            assert isinstance(val, int)

    def test_read_timer_registers(self, proxy):
        """Read timer/CPU control registers."""
        for offset in range(0x10):
            val = proxy.read(0xCC00 + offset)
            assert isinstance(val, int)


@requires_hardware
class TestStress:
    """Stress tests for the proxy."""

    def test_rapid_echo(self, proxy):
        """Rapid echo commands."""
        for _ in range(100):
            for val in [0x00, 0x55, 0xAA, 0xFF]:
                result = proxy.echo(val)
                assert result == val

    def test_rapid_read(self, proxy):
        """Rapid read commands."""
        for _ in range(100):
            val = proxy.read(0x9000)
            assert isinstance(val, int)

    def test_rapid_write_read(self, proxy):
        """Rapid write/read cycles."""
        addr = 0x0800
        for i in range(100):
            val = i & 0xFF
            proxy.write(addr, val)
            result = proxy.read(addr)
            assert result == val, f"Cycle {i}: wrote 0x{val:02X}, read 0x{result:02X}"


@requires_hardware
class TestConnectionInfo:
    """Tests that report connection information."""

    def test_connection_stats(self, proxy):
        """Show connection statistics after operations."""
        # Do some operations
        proxy.echo(0x42)
        proxy.read(0x9000)
        proxy.write(0x0500, 0x00)

        stats = proxy.stats()
        assert stats['echo_count'] >= 1
        assert stats['read_count'] >= 1
        assert stats['write_count'] >= 1

        print(f"\nProxy stats: {stats}")


# Allow running tests directly
if __name__ == '__main__':
    pytest.main([__file__, '-v'])
