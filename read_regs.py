#!/usr/bin/env python3
"""Read XDATA registers via BOT protocol using raw libusb."""
import usb.core, usb.util, struct, time, sys

dev = usb.core.find(idVendor=0xADD1, idProduct=0x0001)
if dev is None:
    dev = usb.core.find(idVendor=0x174C, idProduct=0x2464)
if dev is None:
    print("Device not found!")
    sys.exit(1)

dev.set_configuration()
try:
    usb.util.claim_interface(dev, 0)
except:
    dev.detach_kernel_driver(0)
    usb.util.claim_interface(dev, 0)

tag = 0

def bot_read(cdb_bytes, data_len):
    global tag
    tag += 1
    # CBW: sig, tag, data_len, flags(0x80=IN), lun, cdb_len
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

print(f'=== PCIe state check (firmware on flash) ===')
print()

ltssm = xdata_read(0xB450)[0]
c6b3 = xdata_read(0xC6B3)[0]
b480 = xdata_read(0xB480)[0]
b455 = xdata_read(0xB455)[0]
cc3d = xdata_read(0xCC3D)[0]
cc3f = xdata_read(0xCC3F)[0]
state_flag = xdata_read(0x06E6)[0]

print(f'LTSSM: 0x{ltssm:02x}')
print(f'C6B3 (PHY): 0x{c6b3:02x}')
print(f'B480 (tunnel): 0x{b480:02x}')
print(f'B455 (speed): 0x{b455:02x}')
print(f'CC3D: 0x{cc3d:02x}')
print(f'CC3F: 0x{cc3f:02x}')
print(f'STATE_FLAG: 0x{state_flag:02x}')

# GPIO
print()
print('GPIO state (non-default only):')
for i in range(28):
    ctrl = xdata_read(0xC620 + i)[0]
    byte_val = xdata_read(0xC650 + (i >> 3))[0]
    inp = (byte_val >> (i & 7)) & 1
    modes = {0x00: 'INPUT', 0x02: 'OUT_LOW', 0x03: 'OUT_HIGH'}
    mstr = modes.get(ctrl, f'0x{ctrl:02x}')
    if ctrl != 0x00 or inp != 1:
        print(f'  GPIO{i:2d}: ctrl=0x{ctrl:02x} ({mstr:>10}), input={inp}')

# Power/PHY registers
print()
print('PHY/Power registers:')
for reg, name in [(0x920C, '920C'), (0xC20C, 'C20C'), (0xC208, 'C208'),
                   (0xC20E, 'C20E'), (0xCC37, 'CC37'), (0xC62D, 'C62D'),
                   (0xC656, 'C656'), (0xC659, 'C659'), (0xC65B, 'C65B'),
                   (0x92C0, '92C0'), (0x92C1, '92C1'), (0x92C5, '92C5'),
                   (0x9241, '9241')]:
    val = xdata_read(reg)[0]
    print(f'  {name}: 0x{val:02x}')

# PCIe registers 
print()
print('PCIe registers:')
for addr in range(0xB400, 0xB410):
    val = xdata_read(addr)[0]
    print(f'  B4{addr&0xFF:02X}: 0x{val:02x}')
for addr in range(0xB430, 0xB440):
    val = xdata_read(addr)[0]
    print(f'  B4{addr&0xFF:02X}: 0x{val:02x}')
for addr in range(0xB480, 0xB490):
    val = xdata_read(addr)[0]
    print(f'  B4{addr&0xFF:02X}: 0x{val:02x}')

# LTSSM area
print()
print('LTSSM registers:')
for addr in range(0xCC30, 0xCC50):
    val = xdata_read(addr)[0]
    print(f'  CC{addr&0xFF:02X}: 0x{val:02x}')

# Monitor
print()
print('LTSSM monitoring (5s):')
for i in range(25):
    ltssm = xdata_read(0xB450)[0]
    c6b3 = xdata_read(0xC6B3)[0]
    if i % 5 == 0 or ltssm > 0x01:
        print(f'  t={i*200}ms: LTSSM=0x{ltssm:02x} C6B3=0x{c6b3:02x}')
    if ltssm >= 0x40:
        print('  *** LINK TRAINED! ***')
        break
    time.sleep(0.2)

usb.util.release_interface(dev, 0)
