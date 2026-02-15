#!/usr/bin/env python3
"""Run test_bulk.py with simultaneous UART capture."""
import sys, time, threading
from pyftdi.ftdi import Ftdi

uart_data = bytearray()
uart_done = False

def uart_monitor():
    global uart_data, uart_done
    ftdi = Ftdi()
    ftdi.open_from_url('ftdi://ftdi:230x/1')
    ftdi.set_baudrate(921600)
    # drain
    for _ in range(5):
        ftdi.read_data(4096)
        time.sleep(0.01)
    uart_data = bytearray()
    while not uart_done:
        d = ftdi.read_data(4096)
        if d: uart_data.extend(d)
        time.sleep(0.005)
    # final drain
    for _ in range(50):
        d = ftdi.read_data(4096)
        if d: uart_data.extend(d)
        time.sleep(0.01)
    ftdi.close()

t = threading.Thread(target=uart_monitor, daemon=True)
t.start()
time.sleep(1)

# Run only test 9 (E7 bulk OUT)
from tinygrad.runtime.support.usb import USB3
import struct

dev = USB3(0xADD1, 0x0001, 0x81, 0x83, 0x02, 0x04, use_bot=True)
print("Connected")

# Pre-fill target with known pattern via E5
base = 0x5200
for i in range(4):
    ah = ((base + i) >> 8) & 0xFF
    al = (base + i) & 0xFF
    cdb = struct.pack('>BBBBB10x', 0xE5, 0xBB, 0x00, ah, al)
    dev.send_batch(cdbs=[cdb])
    time.sleep(0.01)

# Read back to confirm pre-fill
dev._tag += 1
cdb = struct.pack('>BBBBB10x', 0xE4, 4, 0x00, 0x52, 0x00)
cbw = struct.pack('<IIIBBB', 0x43425355, dev._tag, 0, 0x80, 0, len(cdb)) + cdb + b'\x00' * (16 - len(cdb))
dev._bulk_out(dev.ep_data_out, cbw)
csw = dev._bulk_in(dev.ep_data_in, 13, timeout=2000)
sig, rtag, residue, status = struct.unpack('<IIIB', csw)
pre = [(residue >> (i*8)) & 0xFF for i in range(4)]
print(f"Pre-fill check: {bytes(pre).hex()}")

time.sleep(0.1)

# E7 bulk OUT: write 64 bytes
pattern = bytes([(i * 7 + 0x33) & 0xFF for i in range(64)])
ah = (base >> 8) & 0xFF
al = base & 0xFF
cdb = struct.pack('>BBBBB10x', 0xE7, 64, 0x00, ah, al)
dev._tag += 1
cbw = struct.pack('<IIIBBB', 0x43425355, dev._tag, 64, 0x00, 0, len(cdb)) + cdb + b'\x00' * (16 - len(cdb))
dev._bulk_out(dev.ep_data_out, cbw)
# Send data phase
dev._bulk_out(dev.ep_data_out, pattern)
# Read CSW
try:
    csw = dev._bulk_in(dev.ep_data_in, 13, timeout=5000)
    sig, rtag, residue, status = struct.unpack('<IIIB', csw)
    print(f"CSW: sig=0x{sig:08X} tag={rtag} residue={residue} status={status}")
except Exception as e:
    print(f"CSW failed: {e}")

time.sleep(0.2)

# Read back via E4
dev._tag += 1
cdb = struct.pack('>BBBBB10x', 0xE4, 4, 0x00, 0x52, 0x00)
cbw = struct.pack('<IIIBBB', 0x43425355, dev._tag, 0, 0x80, 0, len(cdb)) + cdb + b'\x00' * (16 - len(cdb))
dev._bulk_out(dev.ep_data_out, cbw)
csw = dev._bulk_in(dev.ep_data_in, 13, timeout=2000)
sig, rtag, residue, status = struct.unpack('<IIIB', csw)
post = [(residue >> (i*8)) & 0xFF for i in range(4)]
print(f"Post-write check: {bytes(post).hex()}")
expected = pattern[:4]
print(f"Expected:        {expected.hex()}")

uart_done = True
time.sleep(1)
print(f"\n--- UART OUTPUT ---")
print(uart_data.decode('ascii', errors='replace'))
