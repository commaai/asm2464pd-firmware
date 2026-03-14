#!/usr/bin/env python3
"""Try fixing PCIe bridge forwarding by writing missing register values."""
import sys, ctypes, time
sys.path.insert(0, '/home/geohot/tinygrad')
from tinygrad.runtime.autogen import libusb

ctx = ctypes.POINTER(libusb.libusb_context)()
libusb.libusb_init(ctypes.byref(ctx))
handle = libusb.libusb_open_device_with_vid_pid(ctx, 0xADD1, 0x0001)
assert handle, "Failed to open device"
libusb.libusb_claim_interface(handle, 0)

def e4_read(addr, size=1):
    buf = (ctypes.c_uint8 * size)()
    rc = libusb.libusb_control_transfer(handle, 0xC0, 0xE4, addr & 0xFFFF, 0, buf, size, 1000)
    if rc < 0: return None
    return buf[0] if size == 1 else bytes(buf)

def e5_write(addr, val):
    rc = libusb.libusb_control_transfer(handle, 0x40, 0xE5, addr & 0xFFFF, val & 0xFF, None, 0, 1000)
    return rc >= 0

def pcie_cfg_read(bus, dev, fn, reg):
    fmt_type = 0x04 if bus == 0 else 0x05
    address = (bus << 24) | (dev << 19) | (fn << 16) | (reg & 0xFFF)
    e5_write(0xB218, (address >> 24) & 0xFF)
    e5_write(0xB219, (address >> 16) & 0xFF)
    e5_write(0xB21A, (address >> 8) & 0xFF)
    e5_write(0xB21B, address & 0xFF)
    e5_write(0xB21C, 0); e5_write(0xB21D, 0); e5_write(0xB21E, 0); e5_write(0xB21F, 0)
    e5_write(0xB217, 0x0F)
    e5_write(0xB210, fmt_type)
    e5_write(0xB254, 0x0F)
    for _ in range(100):
        s = e4_read(0xB296)
        if s and (s & 0x04): break
        time.sleep(0.001)
    else: return None
    e5_write(0xB296, 0x04)
    b284 = e4_read(0xB284)
    d = [e4_read(0xB220+i) for i in range(4)]
    val = (d[0]<<24)|(d[1]<<16)|(d[2]<<8)|d[3]
    return val, b284

def pcie_cfg_write(bus, dev, fn, reg, value, be=0x0F):
    fmt_type = 0x44 if bus == 0 else 0x45
    address = (bus << 24) | (dev << 19) | (fn << 16) | (reg & 0xFFF)
    e5_write(0xB220, (value >> 24) & 0xFF)
    e5_write(0xB221, (value >> 16) & 0xFF)
    e5_write(0xB222, (value >> 8) & 0xFF)
    e5_write(0xB223, value & 0xFF)
    e5_write(0xB218, (address >> 24) & 0xFF)
    e5_write(0xB219, (address >> 16) & 0xFF)
    e5_write(0xB21A, (address >> 8) & 0xFF)
    e5_write(0xB21B, address & 0xFF)
    e5_write(0xB21C, 0); e5_write(0xB21D, 0); e5_write(0xB21E, 0); e5_write(0xB21F, 0)
    e5_write(0xB217, be)
    e5_write(0xB210, fmt_type)
    e5_write(0xB254, 0x0F)
    for _ in range(100):
        s = e4_read(0xB296)
        if s and (s & 0x04): break
        time.sleep(0.001)
    else: return False
    e5_write(0xB296, 0x04)
    return True

def test_bus2():
    result = pcie_cfg_read(2, 0, 0, 0)
    if result:
        val, b284 = result
        vid = val & 0xFFFF
        did = (val >> 16) & 0xFFFF
        found = vid == 0x1002
        print(f"  Bus 2: VID=0x{vid:04X} DID=0x{did:04X} B284=0x{b284:02X} {'*** GPU ***' if found else ''}")
        return found
    else:
        print("  Bus 2: TIMEOUT")
        return False

# Ensure PCIe is up and bus numbers programmed
pcie_init = e4_read(0x0814)
if pcie_init == 0:
    print("Triggering PCIe init...")
    e5_write(0x0818, 1)
    time.sleep(3)

b450 = e4_read(0xB450)
print(f"LTSSM: 0x{b450:02X}")

# Program bus numbers
pcie_cfg_write(0, 0, 0, 0x18, 0x00020100)
pcie_cfg_write(1, 0, 0, 0x18, 0x00020201)
pcie_cfg_write(0, 0, 0, 0x04, 0x00100007)
pcie_cfg_write(1, 0, 0, 0x04, 0x00100007)
pcie_cfg_write(0, 0, 0, 0x20, 0x10001000)
pcie_cfg_write(1, 0, 0, 0x20, 0x10001000)

print("\nBaseline:")
test_bus2()

# Try each fix individually
fixes = [
    ("B438=0xAA", 0xB438, 0xAA),
    ("B2D5=0x01", 0xB2D5, 0x01),
    ("B455=0x19", 0xB455, 0x19),
    ("B436=0xEE (lane bits)", 0xB436, 0xEE),
]

for name, addr, val in fixes:
    old = e4_read(addr)
    e5_write(addr, val)
    new = e4_read(addr)
    print(f"\n{name}: 0x{old:02X} -> wrote 0x{val:02X}, readback=0x{new:02X}")
    test_bus2()

# Try B481 with B401 sequencing
print("\nTrying B481=0x03 with B401 sequencing:")
e5_write(0xB401, e4_read(0xB401) | 0x01)  # B401 set bit 0
e5_write(0xB481, 0x03)
e5_write(0xB401, e4_read(0xB401) & 0xFE)  # B401 clear bit 0
b481 = e4_read(0xB481)
print(f"  B481 readback: 0x{b481:02X}")
test_bus2()

# Also try B480 toggle
print("\nToggling B480:")
e5_write(0xB480, e4_read(0xB480) & 0xFE)
time.sleep(0.01)
e5_write(0xB480, e4_read(0xB480) | 0x01)
test_bus2()

# Dump current state
print("\n=== Final register state ===")
for addr, name in [(0xB436, "B436"), (0xB438, "B438"), (0xB455, "B455"), 
                     (0xB481, "B481"), (0xB2D5, "B2D5"), (0xB404, "B404"),
                     (0xB480, "B480"), (0xB402, "B402"), (0xB298, "B298")]:
    v = e4_read(addr)
    print(f"  {name}: 0x{v:02X}")

libusb.libusb_release_interface(handle, 0)
libusb.libusb_close(handle)
libusb.libusb_exit(ctx)
