#!/usr/bin/env python3
"""Read stock firmware B4xx state and test PCIe config reads."""
import sys, ctypes, time
sys.path.insert(0, '/home/geohot/tinygrad')
from tinygrad.runtime.autogen import libusb

ctx = ctypes.POINTER(libusb.libusb_context)()
libusb.libusb_init(ctypes.byref(ctx))
handle = libusb.libusb_open_device_with_vid_pid(ctx, 0xADD1, 0x0001)
assert handle, "Failed to open device"
# Detach kernel driver if attached
libusb.libusb_set_auto_detach_kernel_driver(handle, 1)
libusb.libusb_claim_interface(handle, 0)

def e4_read(addr, size=1):
    buf = (ctypes.c_uint8 * size)()
    rc = libusb.libusb_control_transfer(handle, 0xC0, 0xE4, addr & 0xFFFF, 0, buf, size, 1000)
    if rc < 0: return None
    return buf[0] if size == 1 else bytes(buf)

def e5_write(addr, val):
    rc = libusb.libusb_control_transfer(handle, 0x40, 0xE5, addr & 0xFFFF, val & 0xFF, None, 0, 1000)
    return rc >= 0

def read_regs(name, start, count):
    print(f"\n{name} (0x{start:04X}-0x{start+count-1:04X}):")
    for i in range(0, count, 16):
        vals = []
        for j in range(min(16, count-i)):
            v = e4_read(start + i + j)
            vals.append(f"{v:02X}" if v is not None else "??")
        addr = start + i
        print(f"  {addr:04X}: {' '.join(vals)}")

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

# First trigger PCIe init via TUR (stock needs bulk for this)
# The stock firmware's PCIe init is triggered differently - let's just see current state
print("=== Stock Firmware PCIe State ===")
b450 = e4_read(0xB450)
print(f"LTSSM (B450): 0x{b450:02X}" + (" [L0]" if b450 == 0x78 else ""))

pcie_done = e4_read(0x0AF7)
print(f"PCIE_ENUM_DONE (0x0AF7): 0x{pcie_done:02X}" if pcie_done is not None else "PCIE_ENUM_DONE: N/A")

# Need to trigger PCIe init via TUR first
# Stock firmware triggers on SET_CONFIGURATION + TUR
# Let's check if PCIe is already initialized
if b450 != 0x78:
    print("\nPCIe not up yet. Stock firmware needs TUR to trigger it.")
    print("Sending TUR via bulk is not possible from this script.")
    print("Let's just dump the B4xx state before PCIe init.")

read_regs("B4xx Bridge Config", 0xB400, 0x90)
read_regs("B2xx PCIe Control (290-29F)", 0xB290, 16)
read_regs("B2D0 region", 0xB2D0, 16)

# Try config reads anyway
print("\n=== PCIe Config Reads (stock) ===")
for bus in range(4):
    result = pcie_cfg_read(bus, 0, 0, 0)
    if result:
        val, b284 = result
        vid = val & 0xFFFF
        did = (val >> 16) & 0xFFFF
        print(f"Bus {bus}: VID=0x{vid:04X} DID=0x{did:04X} (B284=0x{b284:02X})")
    else:
        print(f"Bus {bus}: TIMEOUT")

# Dump some important addresses
print("\n=== Key Registers ===")
for addr, name in [(0xB436, "B436 lane_cfg"), (0xB438, "B438"), 
                     (0xB455, "B455"), (0xB481, "B481"),
                     (0xB2D5, "B2D5"), (0xB404, "B404"),
                     (0xB480, "B480"), (0xB402, "B402"),
                     (0xB298, "B298"), (0xB430, "B430"),
                     (0xB431, "B431"), (0xB432, "B432"),
                     (0xC659, "C659 12V"), (0xCA06, "CA06"),
                     (0xB22B, "B22B link_width")]:
    v = e4_read(addr)
    print(f"  {name}: 0x{v:02X}" if v is not None else f"  {name}: FAIL")

libusb.libusb_release_interface(handle, 0)
libusb.libusb_close(handle)
libusb.libusb_exit(ctx)
