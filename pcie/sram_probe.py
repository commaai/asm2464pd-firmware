#!/usr/bin/env python3
"""
Probe the ASM2464PD's internal SRAM.

XDATA windows into SRAM (confirmed by probing):
  0x8000-0x8FFF: 4KB window into SRAM at PCI 0x200000+ (offset unknown)
  0xA000-0xAFFF: 4KB window into SRAM at PCI 0x820000+ (completion queue)
  0xF000-0xFFFF: 4KB window into SRAM at PCI 0x200000+ (offset unknown)

  0x8000 and 0xF000 are NOT aliased (different SRAM offsets).
  0xA000 confirmed: CE00 DMA completion descriptors appear here.

CE00 DMA engine:
  CE76-CE79: 32-bit PCI target address
  CE55: transfer count (READ-ONLY, set by USB hardware)
  CE00=0x03: triggers DMA, decrements CE55, writes completion to A000
  CE00 completes instantly even when CE55=0 (no-op without USB data)

The window base offset register has NOT been found.

Usage:
    python3 pcie/sram_probe.py
"""

import ctypes, struct, time
from tinygrad.runtime.support.usb import USB3
from tinygrad.runtime.autogen import libusb

def usb_open():
    for vid, pid in [(0xADD1, 0x0001), (0x174C, 0x2463), (0x174C, 0x2464)]:
        try:
            dev = USB3(vid, pid, 0x81, 0x83, 0x02, 0x04, use_bot=True)
            print(f"opened {vid:04X}:{pid:04X}")
            return dev
        except RuntimeError:
            pass
    raise RuntimeError("no ASM2464PD found")

def peek(dev, addr, size=1):
    buf = (ctypes.c_ubyte * size)()
    ret = libusb.libusb_control_transfer(dev.handle, 0xC0, 0xE4, addr & 0xFFFF, 0, buf, size, 1000)
    if ret < 0: raise IOError(f"E4 read 0x{addr:04X} failed: {ret}")
    return bytes(buf[:ret])

def poke(dev, addr, val):
    ret = libusb.libusb_control_transfer(dev.handle, 0x40, 0xE5, addr & 0xFFFF, val & 0xFF, None, 0, 1000)
    if ret < 0: raise IOError(f"E5 write 0x{addr:04X}=0x{val:02X} failed: {ret}")

def peek8(dev, addr): return peek(dev, addr, 1)[0]

def probe_alias(dev):
    print("=== Alias Tests ===")
    for name, a, b in [("F000 vs 8000", 0xF000, 0x8000),
                        ("D000 vs D400", 0xD000, 0xD400),
                        ("D800 vs E000", 0xD800, 0xE000)]:
        orig_a, orig_b = peek8(dev, a), peek8(dev, b)
        v = (orig_a ^ 0xA5) & 0xFF
        poke(dev, a, v)
        rb = peek8(dev, b)
        poke(dev, a, orig_a)
        print(f"  {name}: {'ALIASED' if rb == v else 'INDEPENDENT'}")

def probe_ce_dma(dev):
    print("\n=== CE00 DMA: completion queue at A000 ===")
    # Fill A000 with marker
    for i in range(16): poke(dev, 0xA000 + i, 0xCC)
    print(f"  A000 before: {peek(dev, 0xA000, 8).hex()}")

    # Set PCI target and trigger
    poke(dev, 0xCE76, 0x00); poke(dev, 0xCE77, 0x00)
    poke(dev, 0xCE78, 0x20); poke(dev, 0xCE79, 0x00)
    poke(dev, 0xCE00, 0x03)
    time.sleep(0.01)

    a000 = peek(dev, 0xA000, 16)
    print(f"  A000 after:  {a000[:8].hex()} (completion descriptor)")
    changed = a000 != b'\xCC' * 16
    print(f"  DMA wrote completion: {changed}")

def probe_ce55_readonly(dev):
    print("\n=== CE55 is read-only ===")
    for v in [0x01, 0xFF]:
        poke(dev, 0xCE55, v)
        rb = peek8(dev, 0xCE55)
        print(f"  wrote 0x{v:02X}, read 0x{rb:02X}: {'writable' if rb == v else 'READ-ONLY'}")

def probe_bulk_out(dev):
    print("\n=== Bulk OUT -> 0x7000 ===")
    pattern = b'\xDE\xAD\xBE\xEF\xCA\xFE\xBA\xBE'
    dev._bulk_out(0x02, pattern + b'\x00' * 24)
    time.sleep(0.01)
    result = peek(dev, 0x7000, 8)
    print(f"  sent:   {pattern.hex()}")
    print(f"  0x7000: {result.hex()}")
    print(f"  match:  {result == pattern}")

def probe_window_content(dev):
    print("\n=== SRAM Window Content ===")
    # Write markers via E5, check they persist
    poke(dev, 0xF000, 0xDE); poke(dev, 0xF001, 0xAD)
    poke(dev, 0x8000, 0xCA); poke(dev, 0x8001, 0xFE)
    print(f"  F000 (wrote DEAD): {peek(dev, 0xF000, 2).hex()}")
    print(f"  8000 (wrote CAFE): {peek(dev, 0x8000, 2).hex()}")
    print(f"  A000:              {peek(dev, 0xA000, 4).hex()}")

def probe_ce00_writes_sram(dev):
    """Verify CE00 DMA writes go to SRAM (not visible at F000/8000)."""
    print("\n=== CE00 DMA write test ===")
    # Write to SRAM, check if F000/8000 change
    poke(dev, 0xF000, 0x11); poke(dev, 0x8000, 0x22)

    pattern = b'\xAA\xBB\xCC\xDD'
    dev._bulk_out(0x02, pattern + b'\x00' * 508)
    time.sleep(0.01)

    poke(dev, 0xCE76, 0x00); poke(dev, 0xCE77, 0x00)
    poke(dev, 0xCE78, 0x20); poke(dev, 0xCE79, 0x00)
    poke(dev, 0xCE00, 0x03)
    time.sleep(0.01)

    f000 = peek8(dev, 0xF000)
    e8000 = peek8(dev, 0x8000)
    print(f"  F000 (was 0x11): 0x{f000:02X} {'CHANGED' if f000 != 0x11 else 'unchanged'}")
    print(f"  8000 (was 0x22): 0x{e8000:02X} {'CHANGED' if e8000 != 0x22 else 'unchanged'}")
    print(f"  A000 completion: {peek(dev, 0xA000, 4).hex()}")

def main():
    dev = usb_open()
    try:
        probe_alias(dev)
        probe_bulk_out(dev)
        probe_window_content(dev)
        probe_ce55_readonly(dev)
        probe_ce_dma(dev)
        probe_ce00_writes_sram(dev)
    finally:
        libusb.libusb_release_interface(dev.handle, 0)
        libusb.libusb_close(dev.handle)
    print("\ndone")

if __name__ == "__main__":
    main()
