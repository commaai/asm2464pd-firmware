#!/usr/bin/env python3
"""
Test script for ASM2464PD custom firmware bulk transfers.

Protocol:
  E8: No-data vendor command (CDB only, CSW response)
  E5: Write register (CDB contains value + address, no data phase)
  E4: Read register (CDB contains size + address, result in CSW residue)
       Max 4 bytes per read (limited by 32-bit residue field).
  E6: Bulk IN  (CDB contains length + address, data phase IN)
  E7: Bulk OUT (CDB contains length + address, data phase OUT)

Usage: python3 test_bulk.py
"""

import ctypes
import struct
import sys
import time

from tinygrad.runtime.support.usb import USB3
from tinygrad.runtime.autogen import libusb

SUPPORTED_CONTROLLERS = [
    (0xADD1, 0x0001),  # Custom firmware
    (0x174C, 0x2463),  # Stock 2463
    (0x174C, 0x2464),  # Stock 2464
]

def find_device():
    for vendor, device in SUPPORTED_CONTROLLERS:
        try:
            dev = USB3(vendor, device, 0x81, 0x83, 0x02, 0x04, use_bot=True)
            print(f"Found device {vendor:04X}:{device:04X}")
            return dev
        except RuntimeError:
            pass
    raise RuntimeError("No device found")

# ============================================================
# Low-level helpers
# ============================================================

def e5_write(dev, addr, val):
    """Write a byte to an XDATA address via E5 vendor command."""
    cdb = struct.pack('>BBBBB10x', 0xE5, val, 0x00, (addr >> 8) & 0xFF, addr & 0xFF)
    dev.send_batch(cdbs=[cdb])

def e4_read(dev, addr, size=1):
    """Read bytes from an XDATA address via E4 vendor command.
    Returns bytes via bulk IN data phase (up to 255 bytes)."""
    size = min(size, 255)
    cdb = struct.pack('>BBBBB10x', 0xE4, size, 0x00, (addr >> 8) & 0xFF, addr & 0xFF)
    dev._tag += 1
    cbw = struct.pack('<IIIBBB', 0x43425355, dev._tag, size, 0x80, 0, len(cdb)) + cdb + b'\x00' * (16 - len(cdb))
    dev._bulk_out(dev.ep_data_out, cbw)
    data = dev._bulk_in(dev.ep_data_in, size, timeout=2000)
    csw = dev._bulk_in(dev.ep_data_in, 13, timeout=2000)
    sig, rtag, residue, status = struct.unpack('<IIIB', csw)
    assert sig == 0x53425355, f"Bad CSW sig 0x{sig:08X}"
    assert rtag == dev._tag, f"CSW tag mismatch"
    assert status == 0, f"CSW status {status}"
    return data

def e6_bulk_in(dev, addr, length=64):
    """Bulk IN: read length bytes from XDATA[addr] via E6 data phase."""
    cdb = struct.pack('>BBBBB10x', 0xE6, min(length, 255), 0x00, (addr >> 8) & 0xFF, addr & 0xFF)
    dev._tag += 1
    cbw = struct.pack('<IIIBBB', 0x43425355, dev._tag, length, 0x80, 0, len(cdb)) + cdb + b'\x00' * (16 - len(cdb))
    dev._bulk_out(dev.ep_data_out, cbw)
    data = dev._bulk_in(dev.ep_data_in, length, timeout=3000)
    csw = dev._bulk_in(dev.ep_data_in, 13, timeout=3000)
    sig, rtag, residue, status = struct.unpack('<IIIB', csw)
    assert sig == 0x53425355, f"Bad CSW sig 0x{sig:08X}"
    assert rtag == dev._tag, f"CSW tag mismatch"
    assert status == 0, f"CSW status {status}"
    return data

def e7_bulk_out(dev, addr, data):
    """Bulk OUT: write data to XDATA[addr] via E7 data phase."""
    length = len(data)
    cdb = struct.pack('>BBBBB10x', 0xE7, min(length, 255), 0x00, (addr >> 8) & 0xFF, addr & 0xFF)
    dev._tag += 1
    cbw = struct.pack('<IIIBBB', 0x43425355, dev._tag, length, 0x00, 0, len(cdb)) + cdb + b'\x00' * (16 - len(cdb))
    dev._bulk_out(dev.ep_data_out, cbw)
    dev._bulk_out(dev.ep_data_out, data)
    csw = dev._bulk_in(dev.ep_data_in, 13, timeout=3000)
    sig, rtag, residue, status = struct.unpack('<IIIB', csw)
    assert sig == 0x53425355, f"Bad CSW sig 0x{sig:08X}"
    assert rtag == dev._tag, f"CSW tag mismatch"
    assert status == 0, f"CSW status {status}"

def verify_match(expected, actual, label="data"):
    """Compare two byte sequences and report first mismatch."""
    if actual == expected:
        return True
    for i in range(min(len(expected), len(actual))):
        if expected[i] != actual[i]:
            print(f"  MISMATCH at {label}[{i}]: expected 0x{expected[i]:02X}, got 0x{actual[i]:02X}")
            return False
    print(f"  MISMATCH: length differs: expected {len(expected)}, got {len(actual)}")
    return False

# ============================================================
# Tests
# ============================================================

def test_e8_single(dev):
    """E8 no-data command"""
    cdb = struct.pack('>BB13x', 0xE8, 0x00)
    dev.send_batch(cdbs=[cdb])
    return True

def test_e8_sequential(dev):
    """10 sequential E8 commands"""
    cdb = struct.pack('>BB13x', 0xE8, 0x00)
    for _ in range(10):
        dev.send_batch(cdbs=[cdb])
    return True

def test_e5_write(dev):
    """E5 write to XDATA"""
    e5_write(dev, 0x5000, 0x42)
    return True

def test_e4_read(dev):
    """E4 read 1 byte from known register"""
    data = e4_read(dev, 0xC001, 1)
    print(f"  C001=0x{data[0]:02X}")
    return True

def test_e4_read_multi(dev):
    """E4 read 4 bytes"""
    data = e4_read(dev, 0x9000, 4)
    assert len(data) == 4, f"Expected 4 bytes, got {len(data)}"
    print(f"  9000-9003: {data.hex()}")
    return True

def test_e5_e4_roundtrip(dev):
    """E5 write then E4 read back — 5 values"""
    for i, val in enumerate([0xA5, 0x5A, 0xFF, 0x00, 0x42]):
        addr = 0x5000 + i
        e5_write(dev, addr, val)
        got = e4_read(dev, addr, 1)[0]
        assert got == val, f"addr 0x{addr:04X}: wrote 0x{val:02X}, read 0x{got:02X}"
    return True

def test_stress(dev):
    """50 mixed E8/E5/E4 commands"""
    cdb_e8 = struct.pack('>BB13x', 0xE8, 0x00)
    for i in range(50):
        if i % 3 == 0:
            dev.send_batch(cdbs=[cdb_e8])
        elif i % 3 == 1:
            e5_write(dev, 0x5010, i & 0xFF)
        else:
            e4_read(dev, 0x5010, 1)
    return True

def test_e6_bulk_in(dev):
    """E6 bulk IN — write 64-byte pattern via E5, read back via E6"""
    base = 0x5100
    # Seed with known pattern
    for i in range(64):
        e5_write(dev, base + i, 0xA0 + (i & 0x3F))
    # Read back via bulk IN
    data = e6_bulk_in(dev, base, 64)
    expected = bytes([0xA0 + (i & 0x3F) for i in range(64)])
    assert verify_match(expected, data, "bulk_in"), "E6 data mismatch"
    return True

def test_e7_bulk_out(dev):
    """E7 bulk OUT — write 64 bytes via E7, verify all 64 via E6 readback"""
    base = 0x5200
    pattern = bytes([(i * 7 + 0x33) & 0xFF for i in range(64)])
    e7_bulk_out(dev, base, pattern)
    # Full readback via E6
    data = e6_bulk_in(dev, base, 64)
    assert verify_match(pattern, data, "bulk_out"), "E7 data mismatch"
    return True

def test_bulk_roundtrip(dev):
    """E7 write + E6 read — full 64-byte roundtrip"""
    base = 0x5300
    pattern = bytes([(i * 13 + 0x42) & 0xFF for i in range(64)])
    e7_bulk_out(dev, base, pattern)
    data = e6_bulk_in(dev, base, 64)
    assert verify_match(pattern, data, "roundtrip"), "Roundtrip mismatch"
    return True

def test_bulk_sizes(dev):
    """Bulk roundtrip at various sizes: 1, 2, 4, 32, 63, 64 bytes"""
    base = 0x5400
    for size in [1, 2, 4, 32, 63, 64]:
        pattern = bytes([(i * 11 + size) & 0xFF for i in range(size)])
        e7_bulk_out(dev, base, pattern)
        data = e6_bulk_in(dev, base, size)
        assert verify_match(pattern, data, f"size={size}"), f"Size {size} mismatch"
    return True

def test_bulk_addresses(dev):
    """Bulk roundtrip at different XDATA addresses"""
    for addr in [0x5000, 0x5100, 0x5500, 0x5F00]:
        pattern = bytes([(addr >> 8) ^ i for i in range(64)])
        e7_bulk_out(dev, addr, pattern)
        data = e6_bulk_in(dev, addr, 64)
        assert verify_match(pattern, data, f"addr=0x{addr:04X}"), f"Addr 0x{addr:04X} mismatch"
    return True

def test_bulk_stress(dev):
    """20 back-to-back bulk roundtrips with different patterns"""
    base = 0x5600
    for r in range(20):
        pattern = bytes([(i + r * 37) & 0xFF for i in range(64)])
        e7_bulk_out(dev, base, pattern)
        data = e6_bulk_in(dev, base, 64)
        assert verify_match(pattern, data, f"round={r}"), f"Stress round {r} failed"
    return True

def test_pcie_cfg_read(dev):
    """PCIe config read bus 0 dev 0 — should return VID/PID"""
    # CfgRd0: fmt_type=0x04, address = (bus<<24)|(dev<<19)|(fn<<16)|byte_addr
    bus, device, fn, byte_addr = 0, 0, 0, 0  # Read VID/PID at offset 0
    address = (bus << 24) | (device << 19) | (fn << 16) | (byte_addr & 0xFFF)

    # Write address (big-endian 32-bit to B218-B21B)
    e5_write(dev, 0xB218, (address >> 24) & 0xFF)
    e5_write(dev, 0xB219, (address >> 16) & 0xFF)
    e5_write(dev, 0xB21A, (address >> 8) & 0xFF)
    e5_write(dev, 0xB21B, address & 0xFF)
    # Address high = 0
    e5_write(dev, 0xB21C, 0x00)
    e5_write(dev, 0xB21D, 0x00)
    e5_write(dev, 0xB21E, 0x00)
    e5_write(dev, 0xB21F, 0x00)
    # Byte enable = 0x0F (all 4 bytes)
    e5_write(dev, 0xB217, 0x0F)
    # Format/Type = CfgRd0
    e5_write(dev, 0xB210, 0x04)
    # Trigger
    e5_write(dev, 0xB254, 0x0F)
    e5_write(dev, 0xB296, 0x04)

    # Poll B296 for completion (bit 1)
    for _ in range(50):
        stat = e4_read(dev, 0xB296, 1)[0]
        if stat & 0x02:
            break
        if stat & 0x01:
            print(f"  TLP error (B296=0x{stat:02X}), retrying...")
            e5_write(dev, 0xB296, 0x01)
            time.sleep(0.01)
    else:
        print(f"  TIMEOUT: B296=0x{stat:02X}")
        return False

    # Read result from B220 (4 bytes, big-endian)
    result_bytes = e4_read(dev, 0xB220, 4)
    result = struct.unpack('>I', result_bytes)[0]
    vid = result & 0xFFFF
    pid = (result >> 16) & 0xFFFF
    print(f"  Bus 0 VID:PID = {vid:04X}:{pid:04X}")
    # We expect the ASMedia bridge: 1B21:6424 (or similar)
    assert vid != 0xFFFF, f"Got FFFF — no device present or link not up"
    assert vid != 0x0000, f"Got 0000 — TLP completion data invalid"
    return True

# ============================================================
# Runner
# ============================================================

TESTS = [
    ("E8 single",          test_e8_single),
    ("E8 x10",             test_e8_sequential),
    ("E5 write",           test_e5_write),
    ("E4 read",            test_e4_read),
    ("E4 read 4 bytes",    test_e4_read_multi),
    ("E5+E4 roundtrip",    test_e5_e4_roundtrip),
    ("Stress 50 mixed",    test_stress),
    ("E6 bulk IN",         test_e6_bulk_in),
    ("E7 bulk OUT",        test_e7_bulk_out),
    ("Bulk roundtrip",     test_bulk_roundtrip),
    ("Bulk sizes",         test_bulk_sizes),
    ("Bulk addresses",     test_bulk_addresses),
    ("Bulk stress x20",    test_bulk_stress),
    ("PCIe cfg read",      test_pcie_cfg_read),
]

def main():
    dev = find_device()
    time.sleep(0.1)  # Wait for bulk init to complete after set_configuration
    results = []
    for name, fn in TESTS:
        print(f"\n--- {name} ---")
        try:
            ok = fn(dev)
            print(f"  PASS")
        except Exception as e:
            print(f"  FAIL: {e}")
            ok = False
        results.append((name, ok))

    print("\n" + "=" * 40)
    print("RESULTS:")
    passed = sum(1 for _, ok in results if ok)
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'}: {name}")
    print(f"\n{passed}/{len(results)} tests passed")

    # Properly clean up USB interface so second run works.
    libusb.libusb_release_interface(dev.handle, 0)
    libusb.libusb_close(dev.handle)

    return 0 if passed == len(results) else 1

if __name__ == "__main__":
    sys.exit(main())
