#!/usr/bin/env python3
"""Test PCIe bridge config and type 1 TLP forwarding via E4/E5 control transfers."""
import sys, ctypes, time
sys.path.insert(0, '/home/geohot/tinygrad')
from tinygrad.runtime.autogen import libusb

# USB setup
ctx = ctypes.POINTER(libusb.libusb_context)()
libusb.libusb_init(ctypes.byref(ctx))
handle = ctypes.POINTER(libusb.libusb_device_handle)()
rc = libusb.libusb_open_device_with_vid_pid(ctx, 0xADD1, 0x0001)
handle = rc
assert handle, "Failed to open device"
libusb.libusb_claim_interface(handle, 0)

def e4_read(addr, size=1):
    """Read XDATA via E4 control transfer"""
    buf = (ctypes.c_uint8 * size)()
    rc = libusb.libusb_control_transfer(handle, 0xC0, 0xE4, addr & 0xFFFF, 0, buf, size, 1000)
    if rc < 0:
        print(f"  E4 read 0x{addr:04X} failed: rc={rc}")
        return None
    if size == 1:
        return buf[0]
    return bytes(buf)

def e5_write(addr, val):
    """Write XDATA via E5 control transfer"""
    rc = libusb.libusb_control_transfer(handle, 0x40, 0xE5, addr & 0xFFFF, val & 0xFF, None, 0, 1000)
    if rc < 0:
        print(f"  E5 write 0x{addr:04X}=0x{val:02X} failed: rc={rc}")
        return False
    return True

def read_regs(name, start, count):
    """Read a range of registers"""
    print(f"\n{name} (0x{start:04X}-0x{start+count-1:04X}):")
    for i in range(0, count, 16):
        vals = []
        for j in range(min(16, count-i)):
            v = e4_read(start + i + j)
            vals.append(f"{v:02X}" if v is not None else "??")
        addr = start + i
        print(f"  {addr:04X}: {' '.join(vals)}")

def pcie_cfg_read(bus, dev, fn, reg):
    """Do a PCIe config read via B2xx registers.
    Returns 4-byte value or None on failure."""
    fmt_type = 0x04 if bus == 0 else 0x05  # CfgRd0 or CfgRd1
    address = (bus << 24) | (dev << 19) | (fn << 16) | (reg & 0xFFF)
    
    # Write address to B218-B21B (big-endian)
    e5_write(0xB218, (address >> 24) & 0xFF)
    e5_write(0xB219, (address >> 16) & 0xFF)
    e5_write(0xB21A, (address >> 8) & 0xFF)
    e5_write(0xB21B, address & 0xFF)
    
    # Upper address = 0
    e5_write(0xB21C, 0x00)
    e5_write(0xB21D, 0x00)
    e5_write(0xB21E, 0x00)
    e5_write(0xB21F, 0x00)
    
    # Byte enable = 0x0F (all 4 bytes)
    e5_write(0xB217, 0x0F)
    
    # Format/type
    e5_write(0xB210, fmt_type)
    
    # Trigger
    e5_write(0xB254, 0x0F)
    
    # Wait for completion
    for _ in range(100):
        status = e4_read(0xB296)
        if status is not None and (status & 0x04):
            break
        time.sleep(0.001)
    else:
        print(f"  CfgRd bus={bus} timeout, B296=0x{status:02X}" if status else "  CfgRd timeout")
        return None
    
    # Ack
    e5_write(0xB296, 0x04)
    
    # Check completion status
    b284 = e4_read(0xB284)
    
    # Read data from B220-B223 (big-endian)
    d0 = e4_read(0xB220)
    d1 = e4_read(0xB221)
    d2 = e4_read(0xB222)
    d3 = e4_read(0xB223)
    
    val = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3
    return val, b284

def pcie_cfg_write(bus, dev, fn, reg, value, be=0x0F):
    """Do a PCIe config write via B2xx registers."""
    fmt_type = 0x44 if bus == 0 else 0x45  # CfgWr0 or CfgWr1
    address = (bus << 24) | (dev << 19) | (fn << 16) | (reg & 0xFFF)
    
    # Write data to B220-B223 (big-endian)
    e5_write(0xB220, (value >> 24) & 0xFF)
    e5_write(0xB221, (value >> 16) & 0xFF)
    e5_write(0xB222, (value >> 8) & 0xFF)
    e5_write(0xB223, value & 0xFF)
    
    # Write address
    e5_write(0xB218, (address >> 24) & 0xFF)
    e5_write(0xB219, (address >> 16) & 0xFF)
    e5_write(0xB21A, (address >> 8) & 0xFF)
    e5_write(0xB21B, address & 0xFF)
    
    # Upper address = 0
    e5_write(0xB21C, 0x00)
    e5_write(0xB21D, 0x00)
    e5_write(0xB21E, 0x00)
    e5_write(0xB21F, 0x00)
    
    # Byte enable
    e5_write(0xB217, be)
    
    # Format/type
    e5_write(0xB210, fmt_type)
    
    # Trigger
    e5_write(0xB254, 0x0F)
    
    # Wait for completion
    for _ in range(100):
        status = e4_read(0xB296)
        if status is not None and (status & 0x04):
            break
        time.sleep(0.001)
    else:
        return False
    
    # Ack
    e5_write(0xB296, 0x04)
    return True

print("=== PCIe Bridge Diagnostic ===")

# Check PCIe link state
b450 = e4_read(0xB450)
print(f"\nLTSSM state (B450): 0x{b450:02X}" + (" [L0 ACTIVE]" if b450 == 0x78 else " [NOT L0!]"))

pcie_init = e4_read(0x0814)
print(f"pcie_initialized: {pcie_init}")

# Trigger PCIe init if needed
if pcie_init == 0:
    print("\nTriggering PCIe init via need_pcie_init...")
    e5_write(0x0818, 1)
    time.sleep(3)
    pcie_init = e4_read(0x0814)
    b450 = e4_read(0xB450)
    print(f"pcie_initialized: {pcie_init}, B450=0x{b450:02X}")

# Read key bridge registers
read_regs("B4xx Bridge Config", 0xB400, 0x90)
read_regs("B2xx PCIe Control", 0xB290, 16)
read_regs("B2D0 region", 0xB2D0, 16)

# Now try config reads
print("\n=== PCIe Config Reads ===")
for bus in range(4):
    result = pcie_cfg_read(bus, 0, 0, 0)
    if result:
        val, b284 = result
        vid = val & 0xFFFF
        did = (val >> 16) & 0xFFFF
        print(f"Bus {bus}: VID=0x{vid:04X} DID=0x{did:04X} (B284=0x{b284:02X})")
    else:
        print(f"Bus {bus}: TIMEOUT")

print("\n=== Now programming bus numbers ===")
# Bus 0: pri=0, sec=1, sub=2
print("Bus 0: PCI_PRIMARY_BUS = 0x00020100")
pcie_cfg_write(0, 0, 0, 0x18, 0x00020100)

# Verify
result = pcie_cfg_read(0, 0, 0, 0x18)
if result:
    val, b284 = result
    print(f"  Bus 0 offset 0x18 readback: 0x{val:08X} (B284=0x{b284:02X})")

# Read bus 1 (should now be visible)
result = pcie_cfg_read(1, 0, 0, 0)
if result:
    val, b284 = result
    vid = val & 0xFFFF
    did = (val >> 16) & 0xFFFF
    print(f"\nBus 1: VID=0x{vid:04X} DID=0x{did:04X} (B284=0x{b284:02X})")

# Bus 1: pri=1, sec=2, sub=2
print("Bus 1: PCI_PRIMARY_BUS = 0x00020201")
pcie_cfg_write(1, 0, 0, 0x18, 0x00020201)

# Verify
result = pcie_cfg_read(1, 0, 0, 0x18)
if result:
    val, b284 = result
    print(f"  Bus 1 offset 0x18 readback: 0x{val:08X} (B284=0x{b284:02X})")

# Enable bus mastering on bus 0 and bus 1
print("\nEnabling PCI_COMMAND on bus 0 and bus 1...")
pcie_cfg_write(0, 0, 0, 0x04, 0x00100007)  # IO+Mem+BusMaster
pcie_cfg_write(1, 0, 0, 0x04, 0x00100007)

# Set memory base/limit on bus 0 and bus 1
print("Setting memory windows...")
pcie_cfg_write(0, 0, 0, 0x20, 0x10001000)  # memory base/limit
pcie_cfg_write(1, 0, 0, 0x20, 0x10001000)

# Now try bus 2
print("\n=== Bus 2 (GPU) ===")
result = pcie_cfg_read(2, 0, 0, 0)
if result:
    val, b284 = result
    vid = val & 0xFFFF
    did = (val >> 16) & 0xFFFF
    print(f"Bus 2: VID=0x{vid:04X} DID=0x{did:04X} (B284=0x{b284:02X})")
    
    if vid == 0x1002:
        print("*** GPU FOUND! ***")
    elif vid == 0x0000 and did == 0x0604:
        print("  Shadow register bleed-through (switch responding locally)")
    elif vid == 0xFFFF:
        print("  Unsupported Request (bus not reachable)")
    else:
        print(f"  Unknown response")
else:
    print("Bus 2: TIMEOUT")

# Read B436 (lane config) 
print(f"\n=== Key register values ===")
for addr, name in [(0xB436, "B436 lane_cfg"), (0xB438, "B438 ???"), 
                     (0xB455, "B455 ltssm_cfg"), (0xB481, "B481 bridge_cfg"),
                     (0xB2D5, "B2D5 pcie_en"), (0xB404, "B404 port_cfg"),
                     (0xB402, "B402 port_ctrl"), (0xB480, "B480 bridge_en")]:
    v = e4_read(addr)
    print(f"  {name}: 0x{v:02X}" if v is not None else f"  {name}: FAIL")

libusb.libusb_release_interface(handle, 0)
libusb.libusb_close(handle)
libusb.libusb_exit(ctx)
