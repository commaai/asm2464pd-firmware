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

def bswap32(v):
    return struct.unpack('<I', struct.pack('>I', v))[0]

def test_isolation(h, addr_a, addr_b, val_a=0x12345678, val_b=0xCAFEBABE):
    """Write different values to two addresses, verify they don't alias.
    Uses non-palindrome values so byte-swap is distinguishable from aliasing."""
    val_a_sw = bswap32(val_a)
    val_b_sw = bswap32(val_b)

    pcie_mem_write(h, addr_a, val_a)
    pcie_mem_write(h, addr_b, val_b)
    time.sleep(0.002)
    try:
        rb_a = pcie_mem_read(h, addr_a)
        rb_b = pcie_mem_read(h, addr_b)
    except (RuntimeError, TimeoutError) as e:
        return f"error: {e}"

    # With byte-swap: if isolated, rb_a should be val_a_sw and rb_b should be val_b_sw
    # If aliased (both see last write): rb_a == rb_b == val_b_sw (or val_b)
    a_is_a = rb_a in (val_a, val_a_sw)
    b_is_b = rb_b in (val_b, val_b_sw)
    a_is_b = rb_a in (val_b, val_b_sw)

    if a_is_a and b_is_b:
        swap = "swapped" if rb_a == val_a_sw else "exact"
        return f"isolated ({swap})"
    elif a_is_b and b_is_b:
        return f"ALIASED (both=0x{rb_a:08X}, last write was 0x{val_b:08X})"
    else:
        return f"unclear (a=0x{rb_a:08X} b=0x{rb_b:08X}, expected a=0x{val_a:08X}/0x{val_a_sw:08X} b=0x{val_b:08X}/0x{val_b_sw:08X})"

def verbose_mem_read(h, addr):
    """Do a PCIe mem read and dump the raw completion bytes."""
    from pcie.pcie_probe import pcie_request, MRD32, xdata_read
    import ctypes
    from tinygrad.runtime.autogen import libusb

    # Do the write + trigger via normal path
    pcie_mem_write(h, addr, 0xDEADC0DE)
    time.sleep(0.002)

    # Now do MRd manually and inspect raw result
    masked = addr & 0xFFFFFFFC
    be = 0x0F
    payload = struct.pack('<II', masked, 0) + struct.pack('>I', 0)
    buf_out = (ctypes.c_ubyte * 12)(*payload)
    ret = libusb.libusb_control_transfer(h, 0x40, 0xF0, 0x20 | (be << 8), 0, buf_out, 12, 5000)

    buf_in = (ctypes.c_ubyte * 8)()
    ret = libusb.libusb_control_transfer(h, 0xC0, 0xF0, 0, 0, buf_in, 8, 5000)
    raw = bytes(buf_in)
    print(f"  addr=0x{addr:08X} raw={raw.hex()}")
    print(f"    data[0:4]  = {raw[0:4].hex()} (B220-B223 completion data)")
    print(f"    cpl[4:5]   = {raw[4:6].hex()} (B22A-B22B completion header)")
    print(f"    type[6]    = 0x{raw[6]:02X} (B284 completion type)")
    print(f"    status[7]  = 0x{raw[7]:02X} (0=ok, 1=UR, FF=timeout)")

def main():
    h, ctx = usb_open()
    print(f"Opened device\n")

    # Phase 0: Raw completion inspection
    print("=== Phase 0: Raw completion data ===")
    print("MRd to various addresses — inspecting completion bytes:")
    for addr in [0x200000, 0x000000, 0x10000000]:
        verbose_mem_read(h, addr)
    print()

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

    # Phase 5: Verify reads aren't stale — does reading different addrs give different results?
    print(f"\n=== Phase 5: Stale read check ===")
    print("Writing unique values, then reading in different order...")
    # Write 0x200000 = 0x11111111, 0x200004 = 0x22222222, then read both
    pcie_mem_write(h, 0x200000, 0x11111111)
    time.sleep(0.002)
    rb1 = pcie_mem_read(h, 0x200000)
    print(f"  Write 0x200000=0x11111111, read 0x200000: 0x{rb1:08X}")

    pcie_mem_write(h, 0x200004, 0x22222222)
    time.sleep(0.002)
    rb2 = pcie_mem_read(h, 0x200004)
    print(f"  Write 0x200004=0x22222222, read 0x200004: 0x{rb2:08X}")

    # Now re-read 0x200000 — if it's real storage, it should still be 0x11111111 (or swapped)
    rb1_again = pcie_mem_read(h, 0x200000)
    print(f"  Re-read 0x200000: 0x{rb1_again:08X}")
    if rb1_again == rb1:
        print(f"  -> 0x200000 retained its value (real storage or consistent alias)")
    elif rb1_again == rb2:
        print(f"  -> 0x200000 now reads as 0x200004's value — SINGLE REGISTER, not RAM")
    else:
        print(f"  -> unexpected value")

    # Phase 6: Read without prior write — does address matter?
    print(f"\n=== Phase 6: Read-only address sensitivity ===")
    print("Reading different addresses WITHOUT writing first...")
    # Write once, then read multiple addrs
    pcie_mem_write(h, 0x200000, 0xFEEDFACE)
    time.sleep(0.002)
    for addr in [0x200000, 0x200004, 0x200100, 0x300000, 0x000000]:
        try:
            v = pcie_mem_read(h, addr)
            print(f"  read 0x{addr:08X}: 0x{v:08X}")
        except (RuntimeError, TimeoutError) as e:
            print(f"  read 0x{addr:08X}: {e}")

    # Phase 7: SRAM size probe with unique non-palindrome markers
    print(f"\n=== Phase 7: SRAM size probe ===")
    marker = 0x12345678
    marker_sw = bswap32(marker)
    poison = 0xCAFEBABE
    poison_sw = bswap32(poison)
    pcie_mem_write(h, 0x200000, marker)
    time.sleep(0.002)
    for size_log2 in range(2, 25):  # 4 bytes to 16MB
        test_addr = 0x200000 + (1 << size_log2)
        pcie_mem_write(h, test_addr, poison)
        time.sleep(0.002)
        rb = pcie_mem_read(h, 0x200000)
        sz = 1 << size_log2
        if sz < 1024: sz_str = f"{sz}B"
        elif sz < 1024*1024: sz_str = f"{sz // 1024}KB"
        else: sz_str = f"{sz // (1024*1024)}MB"

        if rb in (marker, marker_sw):
            print(f"  +{sz_str} (0x{test_addr:08X}): base preserved (0x{rb:08X}) — independent")
        elif rb in (poison, poison_sw):
            print(f"  +{sz_str} (0x{test_addr:08X}): base CLOBBERED — alias at {sz_str}")
            break
        else:
            print(f"  +{sz_str} (0x{test_addr:08X}): base=0x{rb:08X} — unclear")
        # Re-write marker for next iteration
        pcie_mem_write(h, 0x200000, marker)

    usb_close(h, ctx)

if __name__ == "__main__":
    main()
