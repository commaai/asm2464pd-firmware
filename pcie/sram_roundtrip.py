#!/usr/bin/env python3
"""
Probe ASM2464PD SRAM address range via PCIe MWr/MRd TLPs.

Writes a unique pattern to various addresses and reads back to check:
  1. Does the address respond at all?
  2. Does the written value persist (real storage)?
  3. How big is the SRAM?
  4. Is there byte-swapping?

Requires: pcie_bringup.py already run (link in L0).
Does NOT require bridges/BARs set up — we're testing addresses
below the bridge windows (claimed by ASM2464PD root complex).

Usage:
    python3 pcie/sram_roundtrip.py
"""

import struct, sys, time

from pcie.pcie_probe import usb_open, usb_close, pcie_mem_read, pcie_mem_write

def test_roundtrip(h, addr, val=0xDEADC0DE):
    """Write val to addr via MWr, read back via MRd. Returns (read_val, match)."""
    pcie_mem_write(h, addr, val)
    time.sleep(0.001)
    try:
        rb = pcie_mem_read(h, addr)
    except (RuntimeError, TimeoutError) as e:
        return None, False
    return rb, rb == val

def test_byteswap(h, addr, val=0x01020304):
    """Check if SRAM byte-swaps dwords."""
    pcie_mem_write(h, addr, val)
    time.sleep(0.001)
    try:
        rb = pcie_mem_read(h, addr)
    except (RuntimeError, TimeoutError):
        return None
    swapped = struct.unpack('<I', struct.pack('>I', val))[0]
    if rb == val:
        return "no swap"
    elif rb == swapped:
        return "byte-swap"
    else:
        return f"unknown (wrote 0x{val:08X}, got 0x{rb:08X}, swap would be 0x{swapped:08X})"

def test_isolation(h, addr_a, addr_b, val_a=0xAAAAAAAA, val_b=0xBBBBBBBB):
    """Write different values to two addresses, verify they don't alias."""
    pcie_mem_write(h, addr_a, val_a)
    pcie_mem_write(h, addr_b, val_b)
    time.sleep(0.001)
    try:
        rb_a = pcie_mem_read(h, addr_a)
        rb_b = pcie_mem_read(h, addr_b)
    except (RuntimeError, TimeoutError) as e:
        return f"error: {e}"
    if rb_a == val_a and rb_b == val_b:
        return "isolated"
    elif rb_a == val_b or rb_b == val_a:
        return f"ALIASED (a=0x{rb_a:08X} b=0x{rb_b:08X})"
    else:
        return f"weird (a=0x{rb_a:08X} b=0x{rb_b:08X})"

def main():
    h, ctx = usb_open()
    print(f"Opened device\n")

    # Phase 1: Scan for responsive addresses
    print("=== Phase 1: Address range scan ===")
    print("Testing which PCIe addresses respond to MRd/MWr...")
    test_addrs = [
        0x000000, 0x100000, 0x200000, 0x300000, 0x400000,
        0x500000, 0x600000, 0x700000, 0x800000, 0x900000,
        0xA00000, 0xB00000, 0xC00000, 0xD00000, 0xE00000, 0xF00000,
        0x1000000, 0x2000000, 0x4000000, 0x8000000,
    ]
    responsive = []
    for addr in test_addrs:
        val = 0xA5000000 | (addr & 0xFFFFFF)
        rb, match = test_roundtrip(h, addr, val)
        if rb is not None:
            responsive.append(addr)
            # Check for byte swap
            swapped = struct.unpack('<I', struct.pack('>I', val))[0]
            if match:
                status = "exact match"
            elif rb == swapped:
                status = f"byte-swapped (got 0x{rb:08X})"
            else:
                status = f"responded but wrong (got 0x{rb:08X})"
            print(f"  0x{addr:08X}: {status}")
        else:
            print(f"  0x{addr:08X}: no response / timeout")

    if not responsive:
        print("\nNo responsive addresses found!")
        usb_close(h, ctx)
        return

    # Phase 2: Fine-grained scan around 0x200000
    print(f"\n=== Phase 2: Fine scan around 0x200000 ===")
    base = 0x200000
    offsets = [0x0, 0x4, 0x100, 0x1000, 0x10000, 0x40000, 0x7FFFC, 0x80000, 0xFFFFC]
    for off in offsets:
        addr = base + off
        val = 0xBE000000 | off
        rb, match = test_roundtrip(h, addr, val)
        swapped = struct.unpack('<I', struct.pack('>I', val))[0]
        if rb is None:
            print(f"  +0x{off:06X} (0x{addr:08X}): no response")
        elif match:
            print(f"  +0x{off:06X} (0x{addr:08X}): exact match")
        elif rb == swapped:
            print(f"  +0x{off:06X} (0x{addr:08X}): byte-swapped")
        else:
            print(f"  +0x{off:06X} (0x{addr:08X}): got 0x{rb:08X}")

    # Phase 3: Isolation test — are different addresses independent storage?
    print(f"\n=== Phase 3: Isolation tests ===")
    pairs = [
        (0x200000, 0x200004, "adjacent dwords"),
        (0x200000, 0x201000, "+0x1000 apart"),
        (0x200000, 0x210000, "+0x10000 apart"),
        (0x200000, 0x280000, "+0x80000 apart"),
        (0x200000, 0x300000, "0x200000 vs 0x300000"),
        (0x200000, 0x400000, "0x200000 vs 0x400000"),
        (0x200000, 0x600000, "0x200000 vs 0x600000"),
    ]
    for a, b, desc in pairs:
        result = test_isolation(h, a, b)
        print(f"  {desc}: {result}")

    # Phase 4: Byte-swap characterization
    print(f"\n=== Phase 4: Byte-swap test ===")
    result = test_byteswap(h, 0x200000, 0x01020304)
    print(f"  0x200000 with 0x01020304: {result}")
    result = test_byteswap(h, 0x200000, 0xDEADBEEF)
    print(f"  0x200000 with 0xDEADBEEF: {result}")

    # Phase 5: Find upper bound of SRAM
    print(f"\n=== Phase 5: SRAM size probe ===")
    # Binary search for the upper bound
    # First write a marker at 0x200000 and check if high addresses alias back
    pcie_mem_write(h, 0x200000, 0x12345678)
    time.sleep(0.001)
    for size_log2 in range(17, 25):  # 128KB to 16MB
        test_addr = 0x200000 + (1 << size_log2)
        pcie_mem_write(h, test_addr, 0xAAAAAAAA)
        time.sleep(0.001)
        # Check if 0x200000 was corrupted (alias)
        rb = pcie_mem_read(h, 0x200000)
        swapped_marker = struct.unpack('<I', struct.pack('>I', 0x12345678))[0]
        if rb in (0x12345678, swapped_marker):
            aliased = False
        elif rb == 0xAAAAAAAA or rb == struct.unpack('<I', struct.pack('>I', 0xAAAAAAAA))[0]:
            aliased = True
        else:
            aliased = None  # unclear
        sz = 1 << size_log2
        sz_str = f"{sz // 1024}KB" if sz < 1024*1024 else f"{sz // (1024*1024)}MB"
        if aliased is True:
            print(f"  +{sz_str} (0x{test_addr:08X}): ALIASED with 0x200000 — SRAM < {sz_str}")
            break
        elif aliased is False:
            print(f"  +{sz_str} (0x{test_addr:08X}): independent")
        else:
            print(f"  +{sz_str} (0x{test_addr:08X}): unclear (base=0x{rb:08X})")
        # Re-write marker
        pcie_mem_write(h, 0x200000, 0x12345678)

    usb_close(h, ctx)

if __name__ == "__main__":
    main()
