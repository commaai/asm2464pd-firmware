#!/usr/bin/env python3
"""Read PCIe-relevant registers from ASM2464PD firmware via E4 bulk read."""
import struct, time, sys
from tinygrad.runtime.support.usb import USB3

REGS = [
    # PCIe link/tunnel
    ("B210", 0xB210), ("B213", 0xB213), ("B217", 0xB217),
    ("B250", 0xB250), ("B251", 0xB251), ("B254", 0xB254),
    ("B264", 0xB264), ("B265", 0xB265), ("B266", 0xB266), ("B267", 0xB267),
    ("B26C", 0xB26C), ("B26D", 0xB26D), ("B26E", 0xB26E), ("B26F", 0xB26F),
    ("B281", 0xB281), ("B296", 0xB296), ("B298", 0xB298),
    ("B2D5", 0xB2D5),
    # PCIe control
    ("B400", 0xB400), ("B401", 0xB401), ("B402", 0xB402), ("B403", 0xB403),
    ("B404", 0xB404), ("B405", 0xB405),
    ("B430", 0xB430), ("B431", 0xB431), ("B432", 0xB432), ("B433", 0xB433),
    ("B434", 0xB434), ("B435", 0xB435), ("B436", 0xB436),
    ("B450", 0xB450), ("B451", 0xB451), ("B452", 0xB452), ("B453", 0xB453),
    ("B454", 0xB454), ("B455", 0xB455),
    ("B480", 0xB480), ("B481", 0xB481), ("B482", 0xB482),
    # Tunnel adapter
    ("B410", 0xB410), ("B411", 0xB411), ("B412", 0xB412), ("B413", 0xB413),
    ("B414", 0xB414), ("B415", 0xB415), ("B416", 0xB416), ("B417", 0xB417),
    ("B418", 0xB418), ("B419", 0xB419), ("B41A", 0xB41A),
    ("B420", 0xB420), ("B421", 0xB421), ("B422", 0xB422), ("B423", 0xB423),
    ("B424", 0xB424), ("B425", 0xB425), ("B426", 0xB426), ("B427", 0xB427),
    # SerDes PHY (C2xx lanes 0-1)
    ("C21D", 0xC21D), ("C21F", 0xC21F),
    ("C282", 0xC282), ("C283", 0xC283), ("C284", 0xC284), ("C285", 0xC285), ("C286", 0xC286),
    ("C287", 0xC287), ("C289", 0xC289), ("C28B", 0xC28B), ("C28C", 0xC28C),
    ("C290", 0xC290), ("C291", 0xC291), ("C292", 0xC292), ("C293", 0xC293), ("C294", 0xC294),
    ("C295", 0xC295), ("C296", 0xC296), ("C297", 0xC297), ("C299", 0xC299), ("C29A", 0xC29A),
    ("C29B", 0xC29B),
    ("C2A0", 0xC2A0), ("C2A1", 0xC2A1), ("C2A2", 0xC2A2), ("C2A3", 0xC2A3), ("C2A4", 0xC2A4),
    ("C2A5", 0xC2A5), ("C2A6", 0xC2A6), ("C2A7", 0xC2A7), ("C2A8", 0xC2A8), ("C2A9", 0xC2A9),
    ("C2AA", 0xC2AA), ("C2AB", 0xC2AB), ("C2AC", 0xC2AC),
    ("C2BC", 0xC2BC), ("C2C3", 0xC2C3), ("C2C5", 0xC2C5), ("C2C6", 0xC2C6), ("C2C9", 0xC2C9),
    ("C2CA", 0xC2CA), ("C2CC", 0xC2CC), ("C2CD", 0xC2CD), ("C2CE", 0xC2CE), ("C2DB", 0xC2DB),
    # SerDes PHY (C3xx lanes 2-3)
    ("C302", 0xC302), ("C303", 0xC303), ("C304", 0xC304), ("C305", 0xC305), ("C306", 0xC306),
    ("C307", 0xC307), ("C309", 0xC309), ("C30B", 0xC30B), ("C30C", 0xC30C),
    ("C310", 0xC310), ("C311", 0xC311), ("C312", 0xC312), ("C313", 0xC313), ("C314", 0xC314),
    ("C315", 0xC315), ("C316", 0xC316), ("C317", 0xC317), ("C319", 0xC319), ("C31A", 0xC31A),
    ("C31B", 0xC31B),
    ("C320", 0xC320), ("C321", 0xC321), ("C322", 0xC322), ("C323", 0xC323), ("C324", 0xC324),
    ("C325", 0xC325), ("C326", 0xC326), ("C327", 0xC327), ("C328", 0xC328), ("C329", 0xC329),
    ("C32A", 0xC32A), ("C32B", 0xC32B), ("C32C", 0xC32C),
    ("C33C", 0xC33C), ("C343", 0xC343), ("C345", 0xC345), ("C346", 0xC346), ("C349", 0xC349),
    ("C34A", 0xC34A), ("C34C", 0xC34C), ("C34D", 0xC34D), ("C34E", 0xC34E), ("C35B", 0xC35B),
    # E741/E742/CC43
    ("E741", 0xE741), ("E742", 0xE742), ("CC43", 0xCC43),
    # 93xx SerDes DMA
    ("9310", 0x9310), ("9311", 0x9311), ("9312", 0x9312), ("9313", 0x9313),
    ("9314", 0x9314), ("9315", 0x9315), ("9316", 0x9316), ("9317", 0x9317),
    ("9318", 0x9318), ("9319", 0x9319), ("931A", 0x931A), ("931B", 0x931B),
    ("931C", 0x931C), ("931D", 0x931D), ("931E", 0x931E), ("931F", 0x931F),
    ("9320", 0x9320), ("9321", 0x9321), ("9322", 0x9322), ("9323", 0x9323),
    # PHY config
    ("C620", 0xC620), ("C623", 0xC623), ("C62D", 0xC62D),
    ("C655", 0xC655), ("C656", 0xC656), ("C659", 0xC659), ("C65A", 0xC65A),
    ("C65B", 0xC65B), ("C6A8", 0xC6A8),
    # PHY PLL / clock
    ("C808", 0xC808), ("C809", 0xC809), ("C80A", 0xC80A),
    ("C20E", 0xC20E),
    # LTSSM
    ("CC3D", 0xCC3D), ("CC3F", 0xCC3F), ("CC37", 0xCC37),
    # Timer/DMA
    ("CD30", 0xCD30), ("CD31", 0xCD31), ("CD32", 0xCD32), ("CD33", 0xCD33),
    # PHY events
    ("E40B", 0xE40B), ("E40F", 0xE40F), ("E410", 0xE410),
    # PHY link
    ("E302", 0xE302), ("E314", 0xE314),
    ("E710", 0xE710), ("E716", 0xE716), ("E717", 0xE717),
    ("E760", 0xE760), ("E761", 0xE761), ("E763", 0xE763), ("E764", 0xE764),
    ("E7E3", 0xE7E3),
    # CPU link
    ("CEE0", 0xCEE0), ("CEEF", 0xCEEF), ("CEF0", 0xCEF0), ("CEF2", 0xCEF2), ("CEF3", 0xCEF3),
    # Power/USB
    ("9090", 0x9090), ("91C0", 0x91C0), ("92C2", 0x92C2), ("92C8", 0x92C8),
    ("92E1", 0x92E1), ("92F8", 0x92F8),
    # CA81
    ("CA81", 0xCA81),
]

def find_device():
    for vid, pid in [(0xADD1, 0x0001), (0x174C, 0x2463), (0x174C, 0x2464)]:
        try:
            dev = USB3(vid, pid, 0x81, 0x83, 0x02, 0x04, use_bot=True)
            print(f"# Device {vid:04X}:{pid:04X}")
            return dev
        except Exception:
            pass
    raise RuntimeError("No device found")

def e4_read(dev, addr, size=1):
    cdb = struct.pack('>BBBBB10x', 0xE4, size, 0x00, (addr >> 8) & 0xFF, addr & 0xFF)
    dev._tag += 1
    cbw = struct.pack('<IIIBBB', 0x43425355, dev._tag, size, 0x80, 0, len(cdb)) + cdb + b'\x00' * (16 - len(cdb))
    dev._bulk_out(dev.ep_data_out, cbw)
    data = dev._bulk_in(dev.ep_data_in, size, timeout=2000)
    csw = dev._bulk_in(dev.ep_data_in, 13, timeout=2000)
    return data

dev = find_device()
time.sleep(0.3)
for name, addr in REGS:
    val = e4_read(dev, addr, 1)[0]
    print(f"{name}=0x{val:02X}")
