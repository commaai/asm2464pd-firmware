#!/usr/bin/env python3
"""E6/E7 bulk transfer test with UART monitoring."""
import struct, sys, time, threading
from pyftdi.ftdi import Ftdi
from tinygrad.runtime.support.usb import USB3

# UART reader
uart_data = []
def uart_reader(ftdi):
    while True:
        d = ftdi.read_data(1024)
        if d:
            uart_data.append(d)
            sys.stdout.buffer.write(d)
            sys.stdout.buffer.flush()

print("Opening UART...")
ftdi = Ftdi()
ftdi.open_from_url('ftdi://ftdi:230x/1')
ftdi.set_baudrate(921600)
t = threading.Thread(target=uart_reader, args=(ftdi,), daemon=True)
t.start()
time.sleep(0.5)

print("\nConnecting to device...")
dev = USB3(0xADD1, 0x0001, 0x81, 0x83, 0x02, 0x04, use_bot=True)
print("Connected!")

# First write a pattern to XDATA via E5 so we know what to expect
base = 0x5100
print(f"\nWriting pattern to 0x{base:04X} via E5...")
for i in range(64):
    ah = ((base+i) >> 8) & 0xFF
    al = (base+i) & 0xFF
    cdb = struct.pack('>BBBBB10x', 0xE5, 0xA0 + (i & 0x3F), 0x00, ah, al)
    dev.send_batch(cdbs=[cdb])
    time.sleep(0.01)
print("Pattern written!")
time.sleep(0.3)

def try_e6(dev, test_val, xfer_len=64):
    """Try E6 bulk IN — CDB[1]=test_val, CDB[3:4]=addr."""
    ah = (base >> 8) & 0xFF
    al = base & 0xFF
    cdb = struct.pack('>BBBBB10x', 0xE6, test_val, 0x00, ah, al)

    dev._tag += 1
    cbw = struct.pack('<IIIBBB', 0x43425355, dev._tag, xfer_len, 0x80, 0, len(cdb)) + cdb + b'\x00' * (16 - len(cdb))
    print(f"  CBW tag={dev._tag}, xfer_len={xfer_len}, CDB[1]={test_val}")
    dev._bulk_out(dev.ep_data_out, cbw)

    # Try to read data phase
    got_data = None
    got_csw = None
    try:
        data = dev._bulk_in(dev.ep_data_in, xfer_len, timeout=5000)
        got_data = data
        print(f"  DATA: got {len(data)} bytes: {data[:64].hex()}")
    except Exception as e:
        print(f"  DATA FAILED: {e}")

    # Try to read CSW
    try:
        csw = dev._bulk_in(dev.ep_data_in, 13, timeout=3000)
        got_csw = csw
        if len(csw) >= 13:
            sig, rtag, residue, status = struct.unpack('<IIIB', csw[:13])
            print(f"  CSW: sig=0x{sig:08X} tag={rtag} residue={residue} status={status}")
            if sig == 0x53425355 and rtag == dev._tag and status == 0:
                print("  CSW OK!")
        else:
            print(f"  CSW: got {len(csw)} bytes: {csw.hex()}")
    except Exception as e:
        print(f"  CSW FAILED: {e}")
        # Try reading more data in case CSW came with data
        try:
            extra = dev._bulk_in(dev.ep_data_in, 64, timeout=2000)
            print(f"  EXTRA: got {len(extra)} bytes: {extra.hex()}")
        except:
            pass

    time.sleep(0.3)
    return got_data, got_csw

# Test with CDB[1]=64 (len=64)
print(f"\n--- E6 with CDB[1]=64 ---")
try:
    data, csw = try_e6(dev, 64, xfer_len=64)
except Exception as e:
    print(f"  EXCEPTION: {e}")
    time.sleep(2)

# Quick E8 to verify device is still alive
print(f"\n--- Quick E8 health check ---")
try:
    cdb = struct.pack('>BB13x', 0xE8, 0x00)
    dev.send_batch(cdbs=[cdb])
    print("  OK")
except Exception as e:
    print(f"  FAIL: {e}")

time.sleep(1)
print("\nUART output:")
print(b''.join(uart_data).decode('ascii', errors='replace'))
