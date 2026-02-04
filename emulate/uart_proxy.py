#!/usr/bin/env python3
"""
UART Proxy for ASM2464PD MMIO access.

Communicates with proxy firmware running on real device to
execute MMIO reads/writes on actual hardware.

Protocol (binary):
  CMD_ECHO (0x00):  Send byte -> receive same byte (loopback test)
  CMD_READ (0x01):  Send addr_hi, addr_lo -> receive value
  CMD_WRITE (0x02): Send addr_hi, addr_lo, value -> receive 0x00 (ACK)

Usage:
    from uart_proxy import UARTProxy

    with UARTProxy() as proxy:
        val = proxy.read(0x9000)
        proxy.write(0x9000, 0xFF)
"""

import time
from typing import Optional

from pyftdi.ftdi import Ftdi

# Protocol commands
CMD_ECHO = 0x00
CMD_READ = 0x01
CMD_WRITE = 0x02


class UARTProxy:
    """Proxy MMIO access to real ASM2464PD hardware over UART."""

    def __init__(self, device_url: str = 'ftdi://ftdi:230x/1', timeout: float = 1.0):
        """
        Initialize UART proxy connection.

        Args:
            device_url: FTDI device URL
            timeout: Read timeout in seconds
        """
        self.device_url = device_url
        self.timeout = timeout
        self.ftdi: Optional[Ftdi] = None

        # Statistics
        self.read_count = 0
        self.write_count = 0
        self.echo_count = 0

        # Debug mode
        self.debug = False

        self._open()

    def _open(self):
        """Open FTDI connection."""
        self.ftdi = Ftdi()
        self.ftdi.open_from_url(self.device_url)
        self.ftdi.set_baudrate(921600)
        self.ftdi.set_line_property(8, 1, 'N')  # 8N1, no parity

        # Purge any stale data
        self.ftdi.purge_buffers()

    def close(self):
        """Close FTDI connection."""
        if self.ftdi:
            self.ftdi.close()
            self.ftdi = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def _read_byte(self) -> int:
        """Read one byte from UART with timeout."""
        start = time.monotonic()
        while True:
            data = self.ftdi.read_data(1)
            if data:
                return data[0]
            if time.monotonic() - start > self.timeout:
                raise TimeoutError(f"UART read timeout after {self.timeout}s")
            time.sleep(0.0001)  # 100us poll interval

    def _write_bytes(self, data: bytes):
        """Write bytes to UART."""
        self.ftdi.write_data(data)

    def echo(self, value: int) -> int:
        """
        Echo test - send byte and expect same byte back.

        Args:
            value: Byte to echo (0-255)

        Returns:
            Echoed byte value
        """
        value &= 0xFF
        self._write_bytes(bytes([CMD_ECHO, value]))
        result = self._read_byte()
        self.echo_count += 1

        if self.debug:
            print(f"[PROXY] Echo 0x{value:02X} -> 0x{result:02X}")

        return result

    def read(self, addr: int) -> int:
        """
        Read byte from XDATA address on real hardware.

        Args:
            addr: XDATA address (0x0000-0xFFFF)

        Returns:
            Byte value at address
        """
        addr &= 0xFFFF
        self._write_bytes(bytes([CMD_READ, addr >> 8, addr & 0xFF]))
        value = self._read_byte()
        self.read_count += 1

        if self.debug:
            print(f"[PROXY] Read  0x{addr:04X} = 0x{value:02X}")

        return value

    def write(self, addr: int, value: int):
        """
        Write byte to XDATA address on real hardware.

        Args:
            addr: XDATA address (0x0000-0xFFFF)
            value: Byte value to write (0-255)
        """
        addr &= 0xFFFF
        value &= 0xFF
        self._write_bytes(bytes([CMD_WRITE, addr >> 8, addr & 0xFF, value]))
        ack = self._read_byte()
        self.write_count += 1

        if ack != 0x00:
            raise RuntimeError(f"Write ACK failed: expected 0x00, got 0x{ack:02X}")

        if self.debug:
            print(f"[PROXY] Write 0x{addr:04X} = 0x{value:02X}")

    def read_block(self, addr: int, size: int) -> bytes:
        """
        Read multiple bytes from consecutive addresses.

        Args:
            addr: Starting XDATA address
            size: Number of bytes to read

        Returns:
            Bytes read
        """
        result = bytearray(size)
        for i in range(size):
            result[i] = self.read(addr + i)
        return bytes(result)

    def write_block(self, addr: int, data: bytes):
        """
        Write multiple bytes to consecutive addresses.

        Args:
            addr: Starting XDATA address
            data: Bytes to write
        """
        for i, b in enumerate(data):
            self.write(addr + i, b)

    def test_connection(self) -> bool:
        """
        Test connection with echo command.

        Returns:
            True if connection works
        """
        try:
            for test_val in [0x00, 0x55, 0xAA, 0xFF]:
                result = self.echo(test_val)
                if result != test_val:
                    print(f"Echo test failed: sent 0x{test_val:02X}, got 0x{result:02X}")
                    return False
            return True
        except TimeoutError:
            return False

    def stats(self) -> dict:
        """Get proxy statistics."""
        return {
            'read_count': self.read_count,
            'write_count': self.write_count,
            'echo_count': self.echo_count,
        }


def main():
    """Simple test/demo of UART proxy."""
    import argparse

    parser = argparse.ArgumentParser(description='ASM2464PD UART Proxy')
    parser.add_argument('-d', '--device', default='ftdi://ftdi:230x/1',
                        help='FTDI device URL')
    parser.add_argument('-t', '--test', action='store_true',
                        help='Run connection test')
    parser.add_argument('-r', '--read', type=lambda x: int(x, 0),
                        help='Read from address (hex)')
    parser.add_argument('-w', '--write', nargs=2, metavar=('ADDR', 'VALUE'),
                        help='Write value to address (hex)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose output')
    args = parser.parse_args()

    try:
        with UARTProxy(args.device) as proxy:
            proxy.debug = args.verbose

            if args.test:
                print("Testing connection...")
                if proxy.test_connection():
                    print("Connection OK!")
                else:
                    print("Connection FAILED!")
                    return 1

            if args.read is not None:
                val = proxy.read(args.read)
                print(f"0x{args.read:04X} = 0x{val:02X}")

            if args.write:
                addr = int(args.write[0], 0)
                val = int(args.write[1], 0)
                proxy.write(addr, val)
                print(f"Wrote 0x{val:02X} to 0x{addr:04X}")

            if not (args.test or args.read is not None or args.write):
                # Interactive mode - just test connection
                print("Testing connection...")
                if proxy.test_connection():
                    print("Connection OK!")
                    print(f"Stats: {proxy.stats()}")
                else:
                    print("Connection FAILED!")
                    return 1

    except Exception as e:
        print(f"Error: {e}")
        return 1

    return 0


if __name__ == '__main__':
    exit(main())
