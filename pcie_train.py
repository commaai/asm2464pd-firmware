#!/usr/bin/env python3
"""Manually trigger PCIe link training via E5 register writes on stock firmware."""
import usb.core, usb.util, struct, time, sys

dev = usb.core.find(idVendor=0xADD1, idProduct=0x0001)
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
    assert sig == 0x53425355 and rtag == tag and status == 0
    return bytes(data)

def bot_write_nodata(cdb_bytes):
    global tag
    tag += 1
    cbw = struct.pack('<IIIBBB', 0x43425355, tag, 0, 0x00, 0, len(cdb_bytes))
    cbw += cdb_bytes + b'\x00' * (16 - len(cdb_bytes))
    dev.write(0x02, cbw, timeout=2000)
    csw = dev.read(0x81, 13, timeout=2000)
    sig, rtag, residue, status = struct.unpack('<IIIB', bytes(csw))
    assert sig == 0x53425355 and rtag == tag and status == 0

def xdata_read(addr, size=1):
    a = (addr & 0x1FFFF) | 0x500000
    cdb = struct.pack('>BBBHB9x', 0xE4, size, a >> 16, a & 0xFFFF, 0)
    return bot_read(cdb, size)

def xdata_write(addr, val):
    a = (addr & 0x1FFFF) | 0x500000
    cdb = struct.pack('>BBBHB9x', 0xE5, val, a >> 16, a & 0xFFFF, 0)
    bot_write_nodata(cdb)

def xdata_set_bits(addr, mask):
    val = xdata_read(addr)[0]
    xdata_write(addr, val | mask)

def xdata_clear_bits(addr, mask):
    val = xdata_read(addr)[0]
    xdata_write(addr, val & ~mask)

def check_ltssm():
    ltssm = xdata_read(0xB450)[0]
    speed = xdata_read(0xB455)[0]
    return ltssm, speed

# Check initial state
ltssm, speed = check_ltssm()
print(f"Initial: LTSSM=0x{ltssm:02x} speed=0x{speed:02x}")

if ltssm >= 0x40:
    print("Already trained! Exiting.")
    sys.exit(0)

# Apply the register writes we identified from the diff
# These are the firmware-written changes (not hardware effects)
print("\n=== Applying register writes ===")

# Power on
print("1. Power: C659 |= 0x01, C656 |= 0x20")
xdata_set_bits(0xC659, 0x01)  # 12V buck
xdata_set_bits(0xC656, 0x20)  # 3.3V HDDPC
time.sleep(0.1)

# NVMe/PCIe controller config
print("2. NVMe: C428 |= 0x20, C450 |= 0x04, C472 &= 0xFE")
xdata_set_bits(0xC428, 0x20)
xdata_set_bits(0xC450, 0x04)
xdata_clear_bits(0xC472, 0x01)

print("3. NVMe: C4EB |= 0x01, C4ED |= 0x01")
xdata_set_bits(0xC4EB, 0x01)
xdata_set_bits(0xC4ED, 0x01)

# CPU mode
print("4. CPU: CA06 &= 0xBF (clear bit 6)")
xdata_clear_bits(0xCA06, 0x40)

# PCIe lane/tunnel
print("5. PCIe: CA81 &= 0xFE (clear bit 0)")
xdata_clear_bits(0xCA81, 0x01)

# Tunnel
print("6. Tunnel: B403 |= 0x01")
xdata_set_bits(0xB403, 0x01)

print("\n=== Monitoring LTSSM (10s) ===")
prev = 0xFF
for i in range(100):
    ltssm, speed = check_ltssm()
    if ltssm != prev:
        print(f"  t={i*100}ms: LTSSM=0x{ltssm:02x} speed=0x{speed:02x}")
        prev = ltssm
    if ltssm >= 0x40:
        print("  *** LINK TRAINED! ***")
        break
    time.sleep(0.1)
else:
    print(f"  Timeout. Final: LTSSM=0x{ltssm:02x} speed=0x{speed:02x}")

usb.util.release_interface(dev, 0)
