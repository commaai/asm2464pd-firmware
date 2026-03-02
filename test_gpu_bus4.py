#!/usr/bin/env python3
"""Access GPU endpoint on bus 4 — full hierarchy."""
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
    return libusb.libusb_control_transfer(handle, 0x40, 0xE5, addr & 0xFFFF, val & 0xFF, None, 0, 1000) >= 0

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
e5_write(0x0818, 1)
time.sleep(3)
print(f"LTSSM: 0x{e4_read(0xB450):02X}")

# Replicate tinygrad pci_setup_usb_bars(gpu_bus=4, mem_base=0x10000000)
GPU_BUS = 4
MEM_BASE = 0x10000000

print("\nProgramming bus hierarchy...")
for bus in range(GPU_BUS):
    pri = bus
    sec = bus + 1
    sub = GPU_BUS
    bus_val = pri | (sec << 8) | (sub << 16)
    pcie_cfg_write(bus, 0, 0, 0x18, bus_val)
    # Enable IO + Memory + BusMaster
    pcie_cfg_write(bus, 0, 0, 0x04, 0x00100007)
    # Memory base/limit (same value for passthrough)
    mem_base_hi = (MEM_BASE >> 16) & 0xFFF0
    pcie_cfg_write(bus, 0, 0, 0x20, (mem_base_hi << 16) | mem_base_hi)
    print(f"  Bus {bus}: pri={pri} sec={sec} sub={sub}")

# Scan
print("\n=== Full bus scan ===")
for bus in range(GPU_BUS + 2):
    r = pcie_cfg_read(bus, 0, 0, 0)
    if r:
        val, b284 = r
        vid = val & 0xFFFF
        did = (val >> 16) & 0xFFFF
        rc = pcie_cfg_read(bus, 0, 0, 0x08)
        cls = ""
        hdr = ""
        if rc:
            base_cls = (rc[0] >> 24) & 0xFF
            sub_cls = (rc[0] >> 16) & 0xFF
            cls = f" class={base_cls:02X}{sub_cls:02X}"
            if base_cls == 0x03:
                cls += " [DISPLAY CONTROLLER!]"
        rh = pcie_cfg_read(bus, 0, 0, 0x0C)
        if rh:
            hdr_type = (rh[0] >> 16) & 0x7F
            hdr = f" hdr={hdr_type}"
        print(f"  Bus {bus}: VID=0x{vid:04X} DID=0x{did:04X} B284=0x{b284:02X}{cls}{hdr}")
    else:
        print(f"  Bus {bus}: TIMEOUT")

# If bus 4 has the GPU, read its config space
r = pcie_cfg_read(GPU_BUS, 0, 0, 0)
if r and r[1] == 0x01:  # B284=1 means real completion
    vid = r[0] & 0xFFFF
    print(f"\n=== Bus {GPU_BUS} Device Config ===")
    for reg in range(0, 0x40, 4):
        result = pcie_cfg_read(GPU_BUS, 0, 0, reg)
        if result:
            val, _ = result
            labels = {0x00: "VID/DID", 0x04: "CMD/STATUS", 0x08: "REV/CLASS",
                      0x0C: "HDR", 0x10: "BAR0", 0x14: "BAR1",
                      0x18: "BAR2", 0x1C: "BAR3", 0x20: "BAR4",
                      0x24: "BAR5", 0x2C: "SUBSYS"}
            print(f"  [{reg:02X}] 0x{val:08X}  {labels.get(reg, '')}")

libusb.libusb_release_interface(handle, 0)
libusb.libusb_close(handle)
libusb.libusb_exit(ctx)
