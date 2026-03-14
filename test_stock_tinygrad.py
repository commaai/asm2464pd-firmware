#!/usr/bin/env python3
"""Use tinygrad to probe stock firmware B4xx state and PCIe config reads."""
import sys, time
sys.path.insert(0, '/home/geohot/tinygrad')
from tinygrad.runtime.support.usb import USB3, ASM24Controller

print("Connecting to ASM2464PD via tinygrad...")
ctrl = ASM24Controller()

print("Connected! Reading registers...")

# Read B4xx bridge config
print("\nB4xx Bridge Config:")
for base in range(0xB400, 0xB490, 16):
    data = ctrl.read(base, 16)
    vals = [f"{b:02X}" for b in data]
    print(f"  {base:04X}: {' '.join(vals)}")

# Key registers  
print("\nKey registers:")
for addr, name in [(0xB436, "B436 lane_cfg"), (0xB438, "B438"),
                     (0xB455, "B455"), (0xB481, "B481"),
                     (0xB2D5, "B2D5"), (0xB404, "B404"),
                     (0xB480, "B480"), (0xB402, "B402"),
                     (0xB298, "B298"), (0xB450, "B450 LTSSM"),
                     (0xB22B, "B22B link_width"),
                     (0x0AF7, "0AF7 ENUM_DONE"),
                     (0xC659, "C659 12V"),
                     (0xCA06, "CA06")]:
    v = ctrl.read(addr, 1)[0]
    print(f"  {name}: 0x{v:02X}")

# B2xx control region
print("\nB2xx Control:")
for base in [0xB290, 0xB2D0]:
    data = ctrl.read(base, 16)
    vals = [f"{b:02X}" for b in data]
    print(f"  {base:04X}: {' '.join(vals)}")

# PCIe config reads via tinygrad
print("\n=== PCIe Config Reads (stock firmware) ===")
for bus in range(5):
    try:
        val = ctrl.pcie_cfg_req(0, bus, 0, 0, size=4)
        vid = val & 0xFFFF
        did = (val >> 16) & 0xFFFF
        print(f"Bus {bus}: VID=0x{vid:04X} DID=0x{did:04X} raw=0x{val:08X}")
    except Exception as e:
        print(f"Bus {bus}: {e}")

# Read bus 0 and bus 1 offset 0x18 (bus numbers)
print("\n=== Bus Number Config ===")
for bus in range(3):
    try:
        val = ctrl.pcie_cfg_req(0x18, bus, 0, 0, size=4)
        pri = val & 0xFF
        sec = (val >> 8) & 0xFF
        sub = (val >> 16) & 0xFF
        print(f"Bus {bus} offset 0x18: pri={pri} sec={sec} sub={sub} raw=0x{val:08X}")
    except Exception as e:
        print(f"Bus {bus} offset 0x18: {e}")

ctrl.usb.close()
