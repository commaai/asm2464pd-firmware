#!/usr/bin/env python3
"""Test B455/B2D5 trigger sequence to enable PCIe config forwarding."""
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
    for _ in range(200):
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
    for _ in range(200):
        s = e4_read(0xB296)
        if s and (s & 0x04): break
        time.sleep(0.001)
    else: return False
    e5_write(0xB296, 0x04)
    return True

# 1) Trigger PCIe init
print("Triggering PCIe init...")
e5_write(0x0818, 1)
time.sleep(3)
b450 = e4_read(0xB450)
pcie_init = e4_read(0x0814)
print(f"LTSSM: 0x{b450:02X}, pcie_initialized: {pcie_init}")

if b450 != 0x78:
    print("PCIe link not up! Aborting.")
    sys.exit(1)

# 2) Program bus numbers
print("\nProgramming bus numbers...")
pcie_cfg_write(0, 0, 0, 0x18, 0x00020100)
pcie_cfg_write(0, 0, 0, 0x04, 0x00100007)
pcie_cfg_write(0, 0, 0, 0x20, 0x10001000)

# Verify bus 1 is visible
result = pcie_cfg_read(1, 0, 0, 0)
if result:
    val, b284 = result
    print(f"Bus 1: VID=0x{val&0xFFFF:04X} DID=0x{(val>>16)&0xFFFF:04X} B284=0x{b284:02X}")

pcie_cfg_write(1, 0, 0, 0x18, 0x00020201)
pcie_cfg_write(1, 0, 0, 0x04, 0x00100007)
pcie_cfg_write(1, 0, 0, 0x20, 0x10001000)

# 3) Baseline bus 2 read
print("\nBaseline bus 2:")
result = pcie_cfg_read(2, 0, 0, 0)
if result:
    val, b284 = result
    vid = val & 0xFFFF
    did = (val >> 16) & 0xFFFF
    print(f"  VID=0x{vid:04X} DID=0x{did:04X} B284=0x{b284:02X}")

# Show pre-state
print(f"\nPre-sequence state:")
print(f"  B455=0x{e4_read(0xB455):02X}, B2D5=0x{e4_read(0xB2D5):02X}, B438=0x{e4_read(0xB438):02X}")

# 4) B455/B2D5 trigger sequence (from stock 0x35E2-0x35F6)
print("\n=== Executing B455/B2D5 trigger sequence ===")
print("Step 1: B455 = 0x02 (clear pending)")
e5_write(0xB455, 0x02)
print(f"  B455 readback: 0x{e4_read(0xB455):02X}")

print("Step 2: B455 = 0x04 (trigger)")
e5_write(0xB455, 0x04)
print(f"  B455 readback: 0x{e4_read(0xB455):02X}")

print("Step 3: B2D5 = 0x01 (enable)")
e5_write(0xB2D5, 0x01)
print(f"  B2D5 readback: 0x{e4_read(0xB2D5):02X}")

print("Step 4: B296 = 0x08 (ack)")
e5_write(0xB296, 0x08)

# 5) Poll B455 bit 1 for completion
print("Step 5: Polling B455 bit 1 for completion...")
for i in range(100):
    val = e4_read(0xB455)
    if val & 0x02:
        print(f"  B455 bit 1 SET after {i} polls: 0x{val:02X}")
        # Ack
        e5_write(0xB455, 0x02)
        break
    time.sleep(0.01)
else:
    print(f"  TIMEOUT waiting for B455 bit 1 (last: 0x{val:02X})")

# 6) Check post-state
print(f"\nPost-sequence state:")
b455 = e4_read(0xB455)
b2d5 = e4_read(0xB2D5)
b438 = e4_read(0xB438)
b481 = e4_read(0xB481)
print(f"  B455=0x{b455:02X}, B2D5=0x{b2d5:02X}, B438=0x{b438:02X}, B481=0x{b481:02X}")

# 7) Test bus 2 again!
print("\n=== Bus 2 after trigger sequence ===")
result = pcie_cfg_read(2, 0, 0, 0)
if result:
    val, b284 = result
    vid = val & 0xFFFF
    did = (val >> 16) & 0xFFFF
    print(f"  VID=0x{vid:04X} DID=0x{did:04X} B284=0x{b284:02X}")
    if vid == 0x1002:
        print("  *** AMD GPU FOUND! ***")
else:
    print("  TIMEOUT")

# 8) Full bus scan
print("\n=== Full bus scan ===")
for bus in range(5):
    result = pcie_cfg_read(bus, 0, 0, 0)
    if result:
        val, b284 = result
        vid = val & 0xFFFF
        did = (val >> 16) & 0xFFFF
        print(f"  Bus {bus}: VID=0x{vid:04X} DID=0x{did:04X} B284=0x{b284:02X}")
    else:
        print(f"  Bus {bus}: TIMEOUT")

libusb.libusb_release_interface(handle, 0)
libusb.libusb_close(handle)
libusb.libusb_exit(ctx)
