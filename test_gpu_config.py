#!/usr/bin/env python3
"""Read GPU config space and test memory BAR access."""
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

# Trigger PCIe init
print("Triggering PCIe init...")
e5_write(0x0818, 1)
time.sleep(3)
b450 = e4_read(0xB450)
print(f"LTSSM: 0x{b450:02X}")

# Program bus numbers
pcie_cfg_write(0, 0, 0, 0x18, 0x00020100)
pcie_cfg_write(0, 0, 0, 0x04, 0x00100007)
pcie_cfg_write(0, 0, 0, 0x20, 0x10001000)
pcie_cfg_write(1, 0, 0, 0x18, 0x00020201)
pcie_cfg_write(1, 0, 0, 0x04, 0x00100007)
pcie_cfg_write(1, 0, 0, 0x20, 0x10001000)

# Read GPU config space header
print("\n=== GPU Config Space (Bus 2 Dev 0 Fn 0) ===")
for reg in range(0, 0x40, 4):
    result = pcie_cfg_read(2, 0, 0, reg)
    if result:
        val, b284 = result
        labels = {0x00: "VID/DID", 0x04: "CMD/STATUS", 0x08: "REV/CLASS",
                  0x0C: "CACHE/LAT/HDR/BIST", 0x10: "BAR0", 0x14: "BAR1",
                  0x18: "BAR2/BUS_NUM", 0x1C: "BAR3", 0x20: "BAR4/MEM_BASE",
                  0x24: "BAR5/MEM_LIMIT", 0x28: "CIS", 0x2C: "SUBSYS",
                  0x30: "ROM_BASE", 0x34: "CAP_PTR", 0x38: "RSVD", 0x3C: "INT"}
        label = labels.get(reg, "")
        print(f"  [{reg:02X}] 0x{val:08X}  {label}")
    else:
        print(f"  [{reg:02X}] TIMEOUT")

# Check header type to determine if bridge or endpoint
result = pcie_cfg_read(2, 0, 0, 0x0C)
if result:
    val, _ = result
    hdr_type = (val >> 16) & 0x7F
    print(f"\n  Header Type: 0x{hdr_type:02X} ({'Bridge' if hdr_type == 1 else 'Endpoint' if hdr_type == 0 else 'Unknown'})")

# Read class code
result = pcie_cfg_read(2, 0, 0, 0x08)
if result:
    val, _ = result
    base_class = (val >> 24) & 0xFF
    sub_class = (val >> 16) & 0xFF
    prog_if = (val >> 8) & 0xFF
    rev = val & 0xFF
    print(f"  Class: {base_class:02X}{sub_class:02X}{prog_if:02X} Rev: {rev:02X}")
    
    class_names = {0x06: "Bridge", 0x03: "Display Controller", 0x02: "Network", 0x01: "Storage"}
    print(f"  = {class_names.get(base_class, 'Unknown')} device")

# If it's a bridge (class 0x0604), check if there's another device behind it
if result and base_class == 0x06:
    print("\n  This is a bridge - checking downstream...")
    # Program bus 2 bus numbers  
    pcie_cfg_write(2, 0, 0, 0x18, 0x00030302)
    pcie_cfg_write(2, 0, 0, 0x04, 0x00100007)
    pcie_cfg_write(2, 0, 0, 0x20, 0x10001000)
    
    # Try bus 3
    result3 = pcie_cfg_read(3, 0, 0, 0)
    if result3:
        val3, b284_3 = result3
        vid3 = val3 & 0xFFFF
        did3 = (val3 >> 16) & 0xFFFF
        print(f"  Bus 3: VID=0x{vid3:04X} DID=0x{did3:04X} B284=0x{b284_3:02X}")
        if vid3 == 0x1002:
            print("  GPU endpoint found on bus 3!")
            # Read its class
            result3c = pcie_cfg_read(3, 0, 0, 0x08)
            if result3c:
                val3c, _ = result3c
                print(f"  Class: {(val3c>>24)&0xFF:02X}{(val3c>>16)&0xFF:02X}")
    else:
        print("  Bus 3: TIMEOUT")

libusb.libusb_release_interface(handle, 0)
libusb.libusb_close(handle)
libusb.libusb_exit(ctx)
