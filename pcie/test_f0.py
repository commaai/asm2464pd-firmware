#!/usr/bin/env python3
"""Tests for the 0xF0 PCIe TLP control message.

Run after pcie_bringup:
    make -C handmade nflash && python3 pcie/pcie_bringup.py && python3 pcie/test_f0.py
"""
import ctypes
import struct
import time
import unittest

from tinygrad.runtime.support.usb import USB3
from tinygrad.runtime.autogen import libusb


def usb_open():
    dev = USB3(0xADD1, 0x0001, 0x81, 0x83, 0x02, 0x04, use_bot=True)
    return dev.handle

def f0_out(h, fmt_type, byte_enable, addr_lo, addr_hi, value):
    """Send 0xF0 OUT: SETUP(wValue=fmt|be<<8) + DATA_IN(12 bytes: addr_lo LE, addr_hi LE, value BE)."""
    payload = struct.pack('<II', addr_lo, addr_hi) + struct.pack('>I', value)
    buf = (ctypes.c_ubyte * 12)(*payload)
    return libusb.libusb_control_transfer(h, 0x40, 0xF0, fmt_type | (byte_enable << 8), 0, buf, 12, 5000)

def f0_in(h):
    """Read 0xF0 IN: 8 bytes [data(4), cpl_hdr(2), b284(1), status(1)]."""
    buf = (ctypes.c_ubyte * 8)()
    ret = libusb.libusb_control_transfer(h, 0xC0, 0xF0, 0, 0, buf, 8, 5000)
    return ret, bytes(buf)

def f0_request(h, fmt_type, address, value=None, size=4):
    """Full 0xF0 PCIe request: OUT + optional IN. Returns int, 'UR', 'TIMEOUT', or None for posted writes."""
    masked = address & 0xFFFFFFFC
    offset = address & 0x3
    be = ((1 << size) - 1) << offset
    shifted = ((value << (8 * offset)) & 0xFFFFFFFF) if value is not None else 0

    ret = f0_out(h, fmt_type, be, masked, address >> 32, shifted)
    assert ret == 12, f"F0 OUT failed: {ret}"

    # Posted writes: no completion
    is_posted = ((fmt_type & 0xDF) == 0x40) or ((fmt_type & 0xB8) == 0x30)
    if is_posted:
        return None

    ret, data = f0_in(h)
    assert ret == 8, f"F0 IN failed: {ret}"

    status = data[7]
    if status == 0x01: return 'UR'
    if status == 0xFF: return 'TIMEOUT'

    raw = struct.unpack('>I', data[0:4])[0]
    return (raw >> (8 * offset)) & ((1 << (8 * size)) - 1)

def cfg_rd(h, off, bus=0, dev=0, fn=0, size=4):
    fmt = 0x05 if bus > 0 else 0x04
    addr = (bus << 24) | (dev << 19) | (fn << 16) | (off & 0xFFF)
    return f0_request(h, fmt, addr, size=size)

def cfg_wr(h, off, val, bus=0, dev=0, fn=0, size=4):
    fmt = 0x45 if bus > 0 else 0x44
    addr = (bus << 24) | (dev << 19) | (fn << 16) | (off & 0xFFF)
    return f0_request(h, fmt, addr, value=val, size=size)

def mem_rd(h, address, size=4):
    return f0_request(h, 0x20, address, size=size)

def mem_wr(h, address, value, size=4):
    return f0_request(h, 0x60, address, value=value, size=size)

def setup_bridges(h, gpu_bus=4):
    """Set up bridge chain bus 0 -> 1 -> ... -> gpu_bus."""
    for bus in range(gpu_bus):
        buses = (bus << 0) | ((bus + 1) << 8) | (gpu_bus << 16)
        cfg_wr(h, 0x18, buses, bus=bus, size=4)
        cfg_wr(h, 0x20, 0x1000, bus=bus, size=2)
        cfg_wr(h, 0x22, 0xFFFF, bus=bus, size=2)
        cfg_wr(h, 0x24, 0x0000, bus=bus, size=2)
        cfg_wr(h, 0x26, 0xFFFF, bus=bus, size=2)
        cfg_wr(h, 0x28, 0x00000008, bus=bus, size=4)
        cfg_wr(h, 0x2C, 0xFFFFFFFF, bus=bus, size=4)
        cfg_wr(h, 0x04, 0x07, bus=bus, size=1)


class TestF0(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.h = usb_open()
        # Verify PCIe link is up
        buf = (ctypes.c_ubyte * 1)()
        libusb.libusb_control_transfer(cls.h, 0xC0, 0xE4, 0xB450, 0, buf, 1, 1000)
        assert buf[0] in (0x48, 0x78), f"PCIe link not up (LTSSM=0x{buf[0]:02X}). Run pcie_bringup.py first."

    # -- Basic OUT/IN transport --

    def test_out_returns_12(self):
        """F0 OUT should return 12 (bytes transferred)."""
        ret = f0_out(self.h, 0x04, 0x0F, 0, 0, 0)
        self.assertEqual(ret, 12)

    def test_in_returns_8(self):
        """F0 IN should return 8 bytes."""
        f0_out(self.h, 0x04, 0x0F, 0, 0, 0)
        ret, data = f0_in(self.h)
        self.assertEqual(ret, 8)
        self.assertEqual(len(data), 8)

    # -- Config reads --

    def test_cfgrd0_vid_did(self):
        """CfgRd0 bus 0 dev 0 offset 0x00 should return ASMedia VID/DID."""
        v = cfg_rd(self.h, 0x00)
        self.assertIsInstance(v, int)
        self.assertEqual(v & 0xFFFF, 0x1B21, f"VID mismatch: 0x{v&0xFFFF:04X}")
        self.assertEqual((v >> 16) & 0xFFFF, 0x2463, f"DID mismatch: 0x{(v>>16)&0xFFFF:04X}")

    def test_cfgrd0_class(self):
        """CfgRd0 bus 0 class code should be 0x06 (bridge)."""
        v = cfg_rd(self.h, 0x08)
        self.assertIsInstance(v, int, f"Expected int, got {v}")
        cc = (v >> 24) & 0xFF
        self.assertEqual(cc, 0x06, f"Expected class 0x06, got 0x{cc:02X} (raw=0x{v:08X})")

    def test_cfgrd0_header_type(self):
        """CfgRd0 bus 0 header type should be 1 (PCI-to-PCI bridge)."""
        v = cfg_rd(self.h, 0x0E, size=1)
        self.assertIsInstance(v, int)
        self.assertEqual(v & 0x7F, 1)

    def test_cfgrd0_consecutive(self):
        """Multiple consecutive CfgRd0 should all return correct values (no stale data)."""
        for _ in range(10):
            vid = cfg_rd(self.h, 0x00)
            cls = cfg_rd(self.h, 0x08)
            self.assertIsInstance(vid, int, f"VID read failed: {vid}")
            self.assertIsInstance(cls, int, f"class read failed: {cls}")
            self.assertEqual(vid & 0xFFFF, 0x1B21)
            self.assertEqual((cls >> 24) & 0xFF, 0x06, f"Stale data? class=0x{(cls>>24)&0xFF:02X} raw=0x{cls:08X}")

    def test_cfgrd0_subdword(self):
        """Sub-dword config reads (size=2, size=1)."""
        vid = cfg_rd(self.h, 0x00, size=2)
        did = cfg_rd(self.h, 0x02, size=2)
        self.assertIsInstance(vid, int)
        self.assertIsInstance(did, int)
        self.assertEqual(vid, 0x1B21)
        self.assertEqual(did, 0x2463)

        htype = cfg_rd(self.h, 0x0E, size=1)
        self.assertIsInstance(htype, int)
        self.assertEqual(htype & 0x7F, 1)

    # -- Config writes --

    def test_cfgwr0_bus_nums(self):
        """CfgWr0 to bus numbers register, then read back."""
        buses = (0 << 0) | (1 << 8) | (4 << 16)
        cfg_wr(self.h, 0x18, buses, size=4)
        v = cfg_rd(self.h, 0x18)
        self.assertIsInstance(v, int, f"readback failed: {v}")
        self.assertEqual((v >> 8) & 0xFF, 1, f"secondary bus wrong: {(v>>8)&0xFF}")
        self.assertEqual((v >> 16) & 0xFF, 4, f"subordinate bus wrong: {(v>>16)&0xFF}")

    def test_cfgwr0_enable_cmd(self):
        """CfgWr0 to command register (size=1)."""
        cfg_wr(self.h, 0x04, 0x07, size=1)
        v = cfg_rd(self.h, 0x04, size=1)
        self.assertIsInstance(v, int)
        self.assertEqual(v & 0x07, 0x07)

    # -- Bus 1+ access through bridge --

    def test_cfgrd1_bus1(self):
        """CfgRd1 to bus 1 after bridge setup should find ASMedia second port."""
        cfg_wr(self.h, 0x18, (0 | (1 << 8) | (4 << 16)), size=4)
        cfg_wr(self.h, 0x04, 0x07, size=1)
        v = cfg_rd(self.h, 0x00, bus=1)
        self.assertIsInstance(v, int, f"bus 1 read failed: {v}")
        vid = v & 0xFFFF
        self.assertNotIn(vid, (0xFFFF, 0x0000), f"No device on bus 1 (VID=0x{vid:04X})")

    # -- Full GPU discovery --

    def test_gpu_discovery(self):
        """Set up full bridge chain and discover AMD GPU on bus 4."""
        setup_bridges(self.h)
        v = cfg_rd(self.h, 0x00, bus=4)
        self.assertIsInstance(v, int, f"GPU read failed: {v}")
        vid = v & 0xFFFF
        self.assertEqual(vid, 0x1002, f"Expected AMD VID 0x1002, got 0x{vid:04X}")

    # -- Memory reads --

    def test_mem_read_bar5(self):
        """Memory read to BAR5 MMIO space should complete (not UR/timeout)."""
        setup_bridges(self.h)
        # Assign BAR5 at offset 0x24 (BAR reg index 5) to 0x10000000
        cfg_wr(self.h, 0x24, 0x10000000, bus=4, size=4)
        cfg_wr(self.h, 0x04, 0x07, bus=4, size=1)
        v = mem_rd(self.h, 0x10000000)
        self.assertIsInstance(v, int, f"Memory read failed: {v}")

    # -- Posted write --

    def test_mem_write_posted(self):
        """Memory write (MWr, 0x60) should return None (posted, no completion)."""
        setup_bridges(self.h)
        cfg_wr(self.h, 0x24, 0x10000000, bus=4, size=4)
        cfg_wr(self.h, 0x04, 0x07, bus=4, size=1)
        result = mem_wr(self.h, 0x10000000 + 0x2040, 0xDEADBEEF)
        self.assertIsNone(result)

    # -- Speed --

    def test_speed(self):
        """100 config reads should complete in under 2 seconds."""
        t0 = time.time()
        for _ in range(100):
            v = cfg_rd(self.h, 0x00)
            self.assertIsInstance(v, int)
        dt = time.time() - t0
        print(f"\n  100 CfgRd0: {dt:.3f}s ({dt/100*1000:.1f}ms each)")
        self.assertLess(dt, 2.0, f"Too slow: {dt:.3f}s for 100 reads")


if __name__ == '__main__':
    unittest.main(verbosity=2)
