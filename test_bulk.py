#!/usr/bin/env python3
"""
Test script for ASM2464PD custom firmware bulk transfers.

Protocol:
  E8: No-data vendor command (CDB only, CSW response)
  E5: Write register (CDB contains value + address, no data phase)
  E4: Read register (CDB contains size + address, result in CSW residue)
       No data phase — register value is encoded in CSW dCSWDataResidue field.
       Max 4 bytes per read (limited by 32-bit residue field).

Usage: python3 test_bulk.py
"""

import struct
import sys
import time

from tinygrad.runtime.support.usb import USB3

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

def e5_write(dev, addr, val):
    """Write a byte to an XDATA address via E5 vendor command."""
    ah = (addr >> 8) & 0xFF
    al = addr & 0xFF
    cdb = struct.pack('>BBBBB10x', 0xE5, val, 0x00, ah, al)
    dev.send_batch(cdbs=[cdb])

def e4_read(dev, addr, size=1):
    """Read bytes from an XDATA address via E4 vendor command.
    Returns the register value(s) from CSW residue (max 4 bytes)."""
    if size > 4:
        size = 4
    ah = (addr >> 8) & 0xFF
    al = addr & 0xFF
    cdb = struct.pack('>BBBBB10x', 0xE4, size, 0x00, ah, al)
    # Send as no-data BOT command (transfer_length=0)
    dev._tag += 1
    cbw = struct.pack('<IIIBBB', 0x43425355, dev._tag, 0, 0x80, 0, len(cdb)) + cdb + b'\x00' * (16 - len(cdb))
    dev._bulk_out(dev.ep_data_out, cbw)
    # Read CSW
    csw = dev._bulk_in(dev.ep_data_in, 13, timeout=2000)
    sig, rtag, residue, status = struct.unpack('<IIIB', csw)
    assert sig == 0x53425355, f"Bad CSW signature 0x{sig:08X}"
    assert rtag == dev._tag, f"CSW tag mismatch: got {rtag}, expected {dev._tag}"
    assert status == 0, f"SCSI command failed, status=0x{status:02X}"
    # Extract bytes from residue (little-endian)
    result = []
    for i in range(size):
        result.append((residue >> (i * 8)) & 0xFF)
    return bytes(result)


def test_e8_single(dev):
    """Test E8 vendor command (no data phase)"""
    print("\n--- Test 1: E8 single ---")
    cdb = struct.pack('>BB13x', 0xE8, 0x00)
    try:
        dev.send_batch(cdbs=[cdb])
        print("  PASS")
        return True
    except Exception as e:
        print(f"  FAIL: {e}")
        return False

def test_e8_multiple(dev):
    """Test multiple E8 commands in sequence"""
    print("\n--- Test 2: E8 x10 sequential ---")
    cdb = struct.pack('>BB13x', 0xE8, 0x00)
    for i in range(10):
        try:
            dev.send_batch(cdbs=[cdb])
        except Exception as e:
            print(f"  E8 #{i+1}: FAIL: {e}")
            return False
        time.sleep(0.05)
    print("  PASS: 10 sequential E8 commands succeeded")
    return True

def test_e5_write(dev):
    """Test E5 vendor command (write register)"""
    print("\n--- Test 3: E5 write register ---")
    try:
        e5_write(dev, 0x5000, 0x42)
        print("  PASS: E5 write 0x42 to 0x5000")
        return True
    except Exception as e:
        print(f"  FAIL: {e}")
        return False

def test_e4_read(dev):
    """Test E4 vendor command (read register via CSW residue)"""
    print("\n--- Test 4: E4 read register ---")
    try:
        # Read from a known register (0xC001 should be non-zero)
        data = e4_read(dev, 0xC001, 1)
        print(f"  Read C001: 0x{data[0]:02X}")
        print("  PASS")
        return True
    except Exception as e:
        print(f"  FAIL: {e}")
        return False

def test_e4_read_multi(dev):
    """Test E4 vendor command (read 4 bytes)"""
    print("\n--- Test 5: E4 read 4 bytes ---")
    try:
        data = e4_read(dev, 0x9000, 4)
        print(f"  Read 9000-9003: {data.hex()}")
        print("  PASS")
        return True
    except Exception as e:
        print(f"  FAIL: {e}")
        return False

def test_roundtrip(dev):
    """Test E5 write then E4 read back"""
    print("\n--- Test 6: E5+E4 roundtrip ---")
    test_vals = [0xA5, 0x5A, 0xFF, 0x00, 0x42]
    base_addr = 0x5000
    for i, val in enumerate(test_vals):
        addr = base_addr + i
        try:
            e5_write(dev, addr, val)
            time.sleep(0.05)
            data = e4_read(dev, addr, 1)
            got = data[0]
            if got != val:
                print(f"  FAIL: wrote 0x{val:02X} to 0x{addr:04X}, read back 0x{got:02X}")
                return False
        except Exception as e:
            print(f"  FAIL at addr 0x{addr:04X}: {e}")
            return False
    print(f"  PASS: {len(test_vals)} roundtrip values verified")
    return True

def test_stress(dev):
    """Stress test: rapid E8/E5/E4 commands"""
    print("\n--- Test 7: Stress test (50 mixed commands) ---")
    cdb_e8 = struct.pack('>BB13x', 0xE8, 0x00)
    try:
        for i in range(50):
            if i % 3 == 0:
                dev.send_batch(cdbs=[cdb_e8])
            elif i % 3 == 1:
                e5_write(dev, 0x5010, i & 0xFF)
            else:
                data = e4_read(dev, 0x5010, 1)
            time.sleep(0.02)
        print("  PASS: 50 mixed commands completed")
        return True
    except Exception as e:
        print(f"  FAIL at command {i}: {e}")
        return False

def main():
    dev = find_device()

    results = []
    tests = [
        ("E8 single", test_e8_single),
        ("E8 x10", test_e8_multiple),
        ("E5 write", test_e5_write),
        ("E4 read", test_e4_read),
        ("E4 read 4 bytes", test_e4_read_multi),
        ("E5+E4 roundtrip", test_roundtrip),
        ("Stress test", test_stress),
    ]
    for name, test_fn in tests:
        ok = test_fn(dev)
        results.append((name, ok))
        time.sleep(0.1)  # Let EP_COMPLETE + re-arm settle

    print("\n" + "=" * 40)
    print("RESULTS:")
    passed = sum(1 for _, ok in results if ok)
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'}: {name}")
    print(f"\n{passed}/{len(results)} tests passed")

    return 0 if passed == len(results) else 1

if __name__ == "__main__":
    sys.exit(main())
