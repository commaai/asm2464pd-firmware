#!/usr/bin/env python3
"""
Replicate exactly the sequence that changed F000.

1. Init C4xx (groups 1-9)
2. Write C400=0x01
3. Write C401=0x01
4. Write C402=0x01
5. Write C403=0x01
6. Write C404=0x01
7. Write C405=0x01
8. Write C406=0x01  <-- this one changed F000
"""

import ctypes
import time

from tinygrad.runtime.support.usb import USB3
from tinygrad.runtime.autogen import libusb

class Dev:
    def __init__(self):
        self.dev = USB3(0xADD1, 0x0001, 0x81, 0x83, 0x02, 0x04, use_bot=True)
        print("Device opened")

    def read8(self, addr):
        buf = (ctypes.c_ubyte * 1)()
        ret = libusb.libusb_control_transfer(self.dev.handle, 0xC0, 0xE4, addr, 0, buf, 1, 1000)
        if ret < 0: return None
        return buf[0]

    def write(self, addr, val):
        ret = libusb.libusb_control_transfer(self.dev.handle, 0x40, 0xE5, addr, val, None, 0, 1000)
        return ret >= 0

    def read_range(self, addr, n):
        return bytes([self.read8(addr + i) or 0 for i in range(n)])

    def dump(self, addr, n, label=""):
        data = self.read_range(addr, n)
        if label: print(f"\n{label}:")
        for i in range(0, n, 16):
            chunk = data[i:i+16]
            hex_str = " ".join(f"{b:02X}" for b in chunk)
            print(f"  0x{addr+i:04X}: {hex_str}")
        return data

    def close(self):
        libusb.libusb_release_interface(self.dev.handle, 0)
        libusb.libusb_close(self.dev.handle)

def main():
    d = Dev()

    # set up transfer
    for i, v in enumerate([0x00,0x01,0x02,0x00,0x00,0x00,0x00,0x01,0x30,0x00,0x00,0x00,0x00,0x00]):
        d.write(0xC420 + i, v)

    print("Init done")

    # =========================================================
    # Step 2: Exact scan sequence C400-C406
    # (each iteration: seed F000=0xAA, write reg=0x01, read F000)
    # =========================================================
    d.write(0xF000, 0xAA)
    d.write(0xc400, 1)
    d.write(0xc401, 1)
    d.write(0xc402, 1)
    #d.write(0xc403, 1)
    d.write(0xc404, 1)
    #d.write(0xc405, 1)
    d.write(0xc406, 1)

    time.sleep(0.01)

    """
    print("\nExact scan sequence:")
    for reg in range(0xC400, 0xC407):
        d.write(0xF000, 0xAA)
        d.write(reg, 0x01)
        time.sleep(0.01)
        f = d.read8(0xF000)
        if f is None:
            print(f"  0x{reg:04X}=0x01: F000 FAILED")
        elif f != 0xAA:
            print(f"  0x{reg:04X}=0x01: *** F000 = 0x{f:02X}")
        else:
            print(f"  0x{reg:04X}=0x01: F000 = 0xAA (no change)")
    """

    d.dump(0xF000, 32, "F000 final")
    d.dump(0x7000, 16, "0x7000 reference")

    # =========================================================
    # Now narrow down: which of C400-C405 are actually needed?
    # Binary search — try just C406 without the others
    # =========================================================
    # Can't reset device mid-script, but state is already dirty.
    # The important thing: this confirms the sequence works.

    d.close()

if __name__ == "__main__":
    main()
