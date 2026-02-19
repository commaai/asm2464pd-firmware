#!/usr/bin/env python3
"""Read PCIe-relevant registers from stock ASM2464PD firmware via BOT with bCBWCBLength=6."""
import ctypes, struct, time, sys
from tinygrad.runtime.autogen import libusb

VID, PID = 0x174C, 0x2463

REGS = [
    ("B400", 0xB400), ("B401", 0xB401), ("B402", 0xB402), ("B404", 0xB404),
    ("B430", 0xB430), ("B432", 0xB432), ("B434", 0xB434), ("B436", 0xB436),
    ("B450", 0xB450), ("B451", 0xB451), ("B452", 0xB452), ("B453", 0xB453),
    ("B454", 0xB454), ("B455", 0xB455),
    ("B480", 0xB480), ("B481", 0xB481), ("B482", 0xB482),
    ("B298", 0xB298), ("B2D5", 0xB2D5), ("B281", 0xB281),
    ("C620", 0xC620), ("C623", 0xC623), ("C62D", 0xC62D),
    ("C655", 0xC655), ("C656", 0xC656), ("C659", 0xC659), ("C65A", 0xC65A),
    ("C65B", 0xC65B), ("C6A8", 0xC6A8),
    ("C808", 0xC808), ("C809", 0xC809), ("C80A", 0xC80A), ("C20E", 0xC20E),
    ("CC3D", 0xCC3D), ("CC3F", 0xCC3F), ("CC37", 0xCC37),
    ("CD30", 0xCD30), ("CD31", 0xCD31), ("CD33", 0xCD33),
    ("E40B", 0xE40B), ("E40F", 0xE40F), ("E410", 0xE410),
    ("E302", 0xE302), ("E314", 0xE314),
    ("E710", 0xE710), ("E764", 0xE764), ("E7E3", 0xE7E3),
    ("CEEF", 0xCEEF), ("CEF0", 0xCEF0), ("CEF2", 0xCEF2), ("CEF3", 0xCEF3),
    ("9090", 0x9090), ("91C0", 0x91C0), ("92C2", 0x92C2), ("92C8", 0x92C8),
    ("92E1", 0x92E1), ("92F8", 0x92F8), ("CA81", 0xCA81),
    ("9100", 0x9100),
]

ctx = ctypes.POINTER(libusb.struct_libusb_context)()
libusb.libusb_init(ctypes.byref(ctx))
handle = libusb.libusb_open_device_with_vid_pid(ctx, VID, PID)
if not handle:
    print("Device not found"); sys.exit(1)

if libusb.libusb_kernel_driver_active(handle, 0) == 1:
    libusb.libusb_detach_kernel_driver(handle, 0)
libusb.libusb_claim_interface(handle, 0)
libusb.libusb_control_transfer(handle, 0x21, 0xFF, 0, 0, None, 0, 2000)
time.sleep(0.5)
libusb.libusb_clear_halt(handle, 0x81)
libusb.libusb_clear_halt(handle, 0x02)

tag = [0]

def bulk_out(data):
    buf = (ctypes.c_uint8 * len(data))(*data)
    t = ctypes.c_int()
    return libusb.libusb_bulk_transfer(handle, 0x02, buf, len(data), ctypes.byref(t), 5000)

def bulk_in(size):
    buf = (ctypes.c_uint8 * size)()
    t = ctypes.c_int()
    rc = libusb.libusb_bulk_transfer(handle, 0x81, buf, size, ctypes.byref(t), 5000)
    return rc, bytes(buf[:t.value])

def e4_read(addr):
    tag[0] += 1
    encoded = (addr & 0x1FFFF) | 0x500000
    cdb = struct.pack('>BBBHB', 0xE4, 1, encoded >> 16, encoded & 0xFFFF, 0)
    cbw = struct.pack('<IIIBBB', 0x43425355, tag[0], 1, 0x80, 0, 6) + cdb + b'\x00' * (16 - len(cdb))
    rc = bulk_out(cbw)
    if rc != 0: return None
    rc, data = bulk_in(1)
    if rc != 0: return None
    bulk_in(13)  # CSW
    return data[0] if data else None

print(f"# Stock firmware {VID:04X}:{PID:04X}")
for name, addr in REGS:
    val = e4_read(addr)
    if val is not None:
        print(f"{name}=0x{val:02X}")
    else:
        print(f"{name}=ERR")
