#!/usr/bin/env python3
"""Dump ALL registers B000-CFFF to a file for before/after comparison."""
import usb.core, usb.util, struct, sys, time

dev = usb.core.find(idVendor=0xADD1, idProduct=0x0001)
if dev is None:
    dev = usb.core.find(idVendor=0x174C, idProduct=0x2464)
if dev is None:
    print("Device not found!")
    sys.exit(1)

try:
    if dev.is_kernel_driver_active(0):
        dev.detach_kernel_driver(0)
except:
    pass
dev.set_configuration()
usb.util.claim_interface(dev, 0)

tag = 0

def bot_read(cdb_bytes, data_len):
    global tag
    tag += 1
    cbw = struct.pack('<IIIBBB', 0x43425355, tag, data_len, 0x80, 0, len(cdb_bytes))
    cbw += cdb_bytes + b'\x00' * (16 - len(cdb_bytes))
    dev.write(0x02, cbw, timeout=2000)
    data = dev.read(0x81, data_len, timeout=2000)
    csw = dev.read(0x81, 13, timeout=2000)
    sig, rtag, residue, status = struct.unpack('<IIIB', bytes(csw))
    assert sig == 0x53425355 and rtag == tag and status == 0, f"CSW error: sig={sig:#x} tag={rtag} status={status}"
    return bytes(data)

def xdata_read(addr, size=1):
    a = (addr & 0x1FFFF) | 0x500000
    cdb = struct.pack('>BBBHB9x', 0xE4, size, a >> 16, a & 0xFFFF, 0)
    return bot_read(cdb, size)

outfile = sys.argv[1] if len(sys.argv) > 1 else "regdump.txt"

# Dump ranges: B000-BFFF, C000-CFFF, plus some interesting lower addresses
ranges = [
    (0xB000, 0xC000),  # B000-BFFF
    (0xC000, 0xD000),  # C000-CFFF
]

print(f"Dumping registers to {outfile}...")
with open(outfile, 'w') as f:
    for start, end in ranges:
        for addr in range(start, end):
            try:
                val = xdata_read(addr)[0]
                f.write(f"{addr:04X}={val:02X}\n")
                if (addr - start) % 256 == 0:
                    print(f"  {addr:04X}...")
            except Exception as e:
                f.write(f"{addr:04X}=ERR\n")

print(f"Done. Wrote {outfile}")
usb.util.release_interface(dev, 0)
