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
CMD_SFR_READ = 0x03
CMD_SFR_WRITE = 0x04

# Interrupt signal - double 0xFE prefix
INT_SIGNAL = 0xFE

# Interrupt numbers (matches 8051 interrupt vectors)
INT_NAMES = {
    0: 'INT0',
    1: 'Timer0', 
    2: 'INT1',
    3: 'Timer1',
    4: 'Serial',
    5: 'Timer2',
}


class InterruptPending(Exception):
    """
    Raised when an interrupt signal is received instead of a command response.
    
    The proxy is now inside the ISR handler, waiting for commands.
    The caller must:
    1. Handle the interrupt (run ISR in emulator)
    2. Send ack (0xFE) when ISR completes
    3. Retry the original operation
    """
    def __init__(self, int_num: int):
        self.int_num = int_num
        int_name = INT_NAMES.get(int_num, f'Unknown({int_num})')
        super().__init__(f"Interrupt pending: {int_name}")


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

        # Pending interrupts from hardware (queue of interrupt numbers)
        self.pending_interrupts = []
        
        # Interrupt statistics
        self.interrupt_count = 0

        self._open()

    def _open(self):
        """Open FTDI connection."""
        from pyftdi.eeprom import FtdiEeprom
        
        self.ftdi = Ftdi()
        self.ftdi.open_from_url(self.device_url)
        self.ftdi.set_baudrate(921600)
        self.ftdi.set_line_property(8, 1, 'N')  # 8N1, no parity

        # Setup CBUS GPIO for reset control (same as ftdi_debug.py)
        # CBUS1 = GPIO (bootloader), CBUS2 = GPIO (reset)
        self.CBUS_RESET = (1 << 2)
        self.CBUS_BOOTLOADER = (1 << 1)
        
        # Setup GPIO direction
        self.ftdi.set_cbus_direction(self.CBUS_RESET | self.CBUS_BOOTLOADER, 
                                     self.CBUS_RESET | self.CBUS_BOOTLOADER)
        self.ftdi.set_cbus_gpio(0x00)

        # Purge any stale data
        self.ftdi.purge_buffers()

    def reset_device(self):
        """Reset the target device and wait for proxy firmware to boot."""
        # Purge before reset
        self.ftdi.purge_buffers()
        
        # Clear any pending interrupts from before reset
        self.pending_interrupts = []
        
        # Assert reset
        self.ftdi.set_cbus_gpio(self.CBUS_RESET)
        time.sleep(0.1)
        # Release reset
        self.ftdi.set_cbus_gpio(0)
        
        # Wait for "PK\n" hello message (don't purge - we want to read it!)
        hello = b''
        start = time.monotonic()
        while time.monotonic() - start < 2.0:
            data = self.ftdi.read_data(100)
            if data:
                hello += data
                if b'PK' in hello:
                    break
            time.sleep(0.01)
        
        if b'PK' not in hello:
            raise RuntimeError(f"Proxy firmware did not respond with 'PK' after reset (got: {hello!r})")
        
        # Purge again and clear any pending interrupts that might have been
        # detected during boot (hardware interrupts may be active)
        time.sleep(0.05)  # Let any pending data arrive
        self.ftdi.purge_buffers()
        self.pending_interrupts = []

    def close(self):
        """Close FTDI connection."""
        if self.ftdi:
            self.ftdi.close()
            self.ftdi = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def _read_byte_raw(self) -> int:
        """Read one byte from UART with timeout (no interrupt handling)."""
        start = time.monotonic()
        poll_count = 0
        while True:
            data = self.ftdi.read_data(1)
            if data:
                return data[0]
            poll_count += 1
            elapsed = time.monotonic() - start
            if elapsed > self.timeout:
                if self.debug:
                    # Check if there's any data in buffer we might have missed
                    extra = self.ftdi.read_data(64)
                    print(f"[PROXY] _read_byte_raw timeout after {poll_count} polls, {elapsed:.3f}s, extra data: {extra.hex() if extra else 'none'}")
                raise TimeoutError(f"UART read timeout after {self.timeout}s")
            time.sleep(0.0001)  # 100us poll interval

    def _read_response(self, context: str = None) -> int:
        """
        Read 2-byte response, handling interrupt signals.
        
        Protocol:
          - Normal response: <value> <~value>
          - Interrupt signal: 0xFE 0xFE <int_num>
        
        Args:
            context: Description of what operation we're waiting for (for debug)
        
        Returns:
            The value from the response
            
        When an interrupt signal is received:
          - The interrupt is queued in pending_interrupts
          - We continue reading for the actual response (the command was already
            sent and the proxy's ISR handler will process it and send the response)
        """
        while True:
            try:
                byte0 = self._read_byte_raw()
            except TimeoutError:
                ctx = f" (waiting for: {context})" if context else ""
                raise TimeoutError(f"UART read timeout waiting for response byte 0{ctx}")
            
            try:
                byte1 = self._read_byte_raw()
            except TimeoutError:
                ctx = f" (waiting for: {context})" if context else ""
                raise TimeoutError(f"UART read timeout waiting for response byte 1 (got byte0=0x{byte0:02X}){ctx}")
            
            # Check for interrupt signal (0xFE 0xFE)
            # With new protocol, interrupt signal comes AFTER response, so this
            # shouldn't normally happen during _read_response. But we handle it
            # anyway for robustness - queue the interrupts and continue reading.
            if byte0 == INT_SIGNAL and byte1 == INT_SIGNAL:
                # Third byte is bitmask of pending interrupts
                int_mask = self._read_byte_raw()
                
                # Queue each interrupt that's set in the bitmask
                for i in range(6):
                    if int_mask & (1 << i):
                        self.pending_interrupts.append(i)
                        self.interrupt_count += 1
                
                if self.debug:
                    int_names = [INT_NAMES.get(i, f'?{i}') for i in range(6) if int_mask & (1 << i)]
                    print(f"[PROXY] >>> INTERRUPT mask=0x{int_mask:02X} ({', '.join(int_names)}) - {len(self.pending_interrupts)} pending")
                
                # Continue to read actual response
                continue
            
            # Verify complement
            expected_complement = (~byte0) & 0xFF
            if byte1 != expected_complement:
                raise RuntimeError(f"Response verification failed: got 0x{byte0:02X} 0x{byte1:02X}, "
                                   f"expected complement 0x{expected_complement:02X}")
            
            return byte0
    
    def check_interrupts(self) -> list:
        """
        Check for and return any pending interrupts (non-blocking).
        
        Returns:
            List of interrupt numbers that fired (empty if none)
        """
        # Do a non-blocking read to check for interrupt signals
        data = self.ftdi.read_data(64)  # Read any available data
        
        i = 0
        while i < len(data):
            # Look for interrupt pattern: 0xFE 0xFE <int_num>
            if i + 2 < len(data) and data[i] == INT_SIGNAL and data[i + 1] == INT_SIGNAL:
                int_num = data[i + 2]
                self.pending_interrupts.append(int_num)
                self.interrupt_count += 1
                if self.debug:
                    int_name = INT_NAMES.get(int_num, f'Unknown({int_num})')
                    print(f"[PROXY] >>> INTERRUPT: {int_name}")
                i += 3
            else:
                # Unexpected data - might be leftover from previous operation
                i += 1
        
        # Return and clear pending interrupts
        interrupts = self.pending_interrupts
        self.pending_interrupts = []
        return interrupts
    
    def get_pending_interrupt(self) -> Optional[int]:
        """
        Get next pending interrupt if any.
        
        Returns:
            Interrupt number or None if no interrupts pending
        """
        if self.pending_interrupts:
            return self.pending_interrupts.pop(0)
        return None

    def ack_interrupt(self):
        """
        Send acknowledgment that emulator finished running ISR.
        
        The proxy firmware waits for this 0xFE byte before returning
        from the interrupt handler. This ensures the emulator runs
        the ISR code before the hardware continues.
        """
        self._write_bytes(bytes([INT_SIGNAL]))  # Send 0xFE
        if self.debug:
            print(f"[PROXY] <<< ACK interrupt (sent 0xFE)")

    def _write_bytes(self, data: bytes):
        """Write bytes to UART."""
        self.ftdi.write_data(data)

    def _drain_interrupt_signals(self):
        """
        Check for and drain any pending interrupt signals from the RX buffer.
        
        This should be called before sending a new command to handle the case
        where an interrupt fired between the previous response and our new command.
        
        Returns:
            True if interrupts were drained (caller should handle them)
        """
        had_interrupts = False
        
        while True:
            # Non-blocking peek at available data
            data = self.ftdi.read_data(3)  # Need at least 3 bytes for interrupt signal
            if len(data) < 3:
                # Not enough data - put it back by... well, we can't put it back
                # So we need a different approach
                break
            
            if data[0] == INT_SIGNAL and data[1] == INT_SIGNAL:
                # Interrupt signal
                int_num = data[2]
                self.pending_interrupts.append(int_num)
                self.interrupt_count += 1
                had_interrupts = True
                
                if self.debug:
                    int_name = INT_NAMES.get(int_num, f'Unknown({int_num})')
                    print(f"[PROXY] >>> INTERRUPT (drained): {int_name}")
            else:
                # Not an interrupt signal - this is unexpected data
                # Put it back... we can't, so this is a problem
                if self.debug:
                    print(f"[PROXY] WARNING: Unexpected data in buffer: {data.hex()}")
                break
        
        return had_interrupts

    def _check_for_interrupt_signal(self):
        """
        Check if proxy sent an interrupt signal after the response.
        
        With the new protocol, interrupt signals come AFTER command responses:
        1. Command response: <val> <~val>
        2. If interrupts pending: 0xFE 0xFE <bitmask>
        3. Proxy waits for ack (0xFE)
        
        This does a non-blocking check and queues any interrupts found.
        """
        # Small delay to let interrupt signal arrive if there is one
        import time
        time.sleep(0.001)
        
        # Non-blocking read - check if there's data
        data = self.ftdi.read_data(3)
        if len(data) >= 3 and data[0] == INT_SIGNAL and data[1] == INT_SIGNAL:
            int_mask = data[2]
            
            # Queue each interrupt that's set in the bitmask
            for i in range(6):
                if int_mask & (1 << i):
                    self.pending_interrupts.append(i)
                    self.interrupt_count += 1
            
            if self.debug:
                int_names = [INT_NAMES.get(i, f'?{i}') for i in range(6) if int_mask & (1 << i)]
                print(f"[PROXY] >>> INTERRUPT mask=0x{int_mask:02X} ({', '.join(int_names)}) - {len(self.pending_interrupts)} pending")

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
        result = self._read_response(f"ECHO 0x{value:02X}")
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
        self._last_addr = addr  # Track for debugging
        self._write_bytes(bytes([CMD_READ, addr >> 8, addr & 0xFF]))
        value = self._read_response(f"READ 0x{addr:04X}")
        self._check_for_interrupt_signal()
        self.read_count += 1
        # Debug print handled by caller (hardware.py hook) which has PC context
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
        ack = self._read_response(f"WRITE 0x{addr:04X}=0x{value:02X}")
        self._check_for_interrupt_signal()
        self.write_count += 1

        if ack != 0x00:
            raise RuntimeError(f"Write ACK failed: expected 0x00, got 0x{ack:02X}")
        # Debug print handled by caller (hardware.py hook) which has PC context

    def sfr_read(self, addr: int) -> int:
        """
        Read SFR (Special Function Register) on real hardware.

        Args:
            addr: SFR address (0x80-0xFF)

        Returns:
            Byte value of SFR
        """
        addr &= 0xFF
        self._write_bytes(bytes([CMD_SFR_READ, addr]))
        value = self._read_response(f"SFR_READ 0x{addr:02X}")
        self._check_for_interrupt_signal()

        # Debug print handled by caller (hardware.py hook) which has PC context
        return value

    def sfr_write(self, addr: int, value: int):
        """
        Write SFR (Special Function Register) on real hardware.

        Args:
            addr: SFR address (0x80-0xFF)
            value: Byte value to write
        """
        addr &= 0xFF
        value &= 0xFF
        self._write_bytes(bytes([CMD_SFR_WRITE, addr, value]))
        ack = self._read_response(f"SFR_WRITE 0x{addr:02X}=0x{value:02X}")
        self._check_for_interrupt_signal()

        if ack != 0x00:
            raise RuntimeError(f"SFR Write ACK failed: expected 0x00, got 0x{ack:02X}")

        # Debug print handled by caller (hardware.py hook) which has PC context

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
            'interrupt_count': self.interrupt_count,
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
