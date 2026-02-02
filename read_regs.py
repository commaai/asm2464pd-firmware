#!/usr/bin/env python3
"""Read registers from ASM2464PD over USB using E4 vendor command"""

import sys
import struct
import ctypes
from tinygrad.runtime.autogen import libusb

SUPPORTED_CONTROLLERS = [
    (0xADD1, 0x0001),
    (0x174C, 0x2464),
    (0x174C, 0x2463),
]

# Endpoints
EP_DATA_IN = 0x81
EP_DATA_OUT = 0x02

class USBDevice:
    def __init__(self, vendor, product):
        self.ctx = ctypes.POINTER(libusb.struct_libusb_context)()
        if libusb.libusb_init(ctypes.byref(self.ctx)):
            raise RuntimeError("libusb_init failed")
        
        self.handle = libusb.libusb_open_device_with_vid_pid(self.ctx, vendor, product)
        if not self.handle:
            libusb.libusb_exit(self.ctx)
            raise RuntimeError(f"Device {vendor:04x}:{product:04x} not found")
        
        # Detach kernel driver if needed
        if libusb.libusb_kernel_driver_active(self.handle, 0):
            libusb.libusb_detach_kernel_driver(self.handle, 0)
        
        if libusb.libusb_claim_interface(self.handle, 0):
            libusb.libusb_close(self.handle)
            libusb.libusb_exit(self.ctx)
            raise RuntimeError("claim_interface failed")
        
        self._tag = 0
    
    def __del__(self):
        if hasattr(self, 'handle') and self.handle:
            libusb.libusb_release_interface(self.handle, 0)
            libusb.libusb_close(self.handle)
        if hasattr(self, 'ctx') and self.ctx:
            libusb.libusb_exit(self.ctx)
    
    def _bulk_out(self, ep, data, timeout=1000):
        transferred = ctypes.c_int(0)
        buf = (ctypes.c_ubyte * len(data))(*data)
        rc = libusb.libusb_bulk_transfer(self.handle, ep, buf, len(data), 
                                          ctypes.byref(transferred), timeout)
        if rc != 0:
            raise RuntimeError(f"bulk OUT failed: {rc}")
        return transferred.value
    
    def _bulk_in(self, ep, length, timeout=1000):
        transferred = ctypes.c_int(0)
        buf = (ctypes.c_ubyte * length)()
        rc = libusb.libusb_bulk_transfer(self.handle, ep, buf, length,
                                          ctypes.byref(transferred), timeout)
        if rc != 0:
            raise RuntimeError(f"bulk IN failed: {rc}")
        return bytes(buf[:transferred.value])
    
    def scsi_command(self, cdb, data_in_len=0, data_out=None, timeout=2000):
        """Execute SCSI command via BOT protocol"""
        self._tag += 1
        
        # Determine direction and data length
        if data_in_len > 0:
            direction = 0x80  # Device to host
            data_len = data_in_len
        elif data_out is not None:
            direction = 0x00  # Host to device
            data_len = len(data_out)
        else:
            direction = 0x00
            data_len = 0
        
        # Build and send CBW (Command Block Wrapper)
        # Signature (USBC), Tag, DataTransferLength, Flags, LUN, CBLength
        cbw = struct.pack("<IIIBBB", 0x43425355, self._tag, data_len, direction, 0, len(cdb))
        cbw += cdb + b'\x00' * (16 - len(cdb))
        self._bulk_out(EP_DATA_OUT, cbw, timeout)
        
        # Data phase
        result = None
        if data_in_len > 0:
            result = self._bulk_in(EP_DATA_IN, data_in_len, timeout)
        elif data_out is not None:
            self._bulk_out(EP_DATA_OUT, data_out, timeout)
        
        # Read CSW (Command Status Wrapper)
        csw = self._bulk_in(EP_DATA_IN, 13, timeout)
        sig, rtag, residue, status = struct.unpack("<IIIB", csw)
        
        if sig != 0x53425355:
            raise RuntimeError(f"Bad CSW signature: 0x{sig:08X}")
        if rtag != self._tag:
            raise RuntimeError(f"CSW tag mismatch: {rtag} vs {self._tag}")
        if status != 0:
            raise RuntimeError(f"SCSI command failed: status={status}")
        
        return result

def get_device():
    for vendor, device in SUPPORTED_CONTROLLERS:
        try:
            return USBDevice(vendor, device)
        except RuntimeError:
            pass
    raise RuntimeError('Could not open controller')

def read_xdata(dev, addr, size=1):
    """Read from XDATA using E4 vendor command"""
    # E4 CDB format: E4, size, addr_hi (0x50 for XDATA), addr_lo (16-bit big endian), 0
    cdb = struct.pack('>BBBHB', 0xE4, size, 0x50, addr, 0)
    return dev.scsi_command(cdb, data_in_len=size)

def read_reg(dev, addr):
    """Read single byte from XDATA"""
    data = read_xdata(dev, addr, 1)
    if data and len(data) > 0:
        return data[0]
    return None

def main():
    dev = get_device()
    print("Connected to ASM2464PD")
    
    # Key registers to check based on emulator trace
    regs_to_check = [
        # USB PHY/link
        (0x91C0, "91C0 USB PHY control"),
        (0x91C1, "91C1 USB PHY control"),
        (0x91C3, "91C3 USB PHY"),
        (0x91D1, "91D1"),
        (0x9100, "9100 USB link status"),
        (0x9101, "9101 USB peripheral status"),
        (0x9105, "9105"),
        (0x9118, "9118 USB enum state"),
        (0x9000, "9000 USB active status"),
        (0x9002, "9002"),
        (0x9005, "9005"),
        (0x9006, "9006 EP0 config"),
        (0x900B, "900B"),
        (0x9010, "9010"),
        (0x901A, "901A"),
        (0x905E, "905E"),
        (0x905F, "905F USB config"),
        (0x9091, "9091 USB interrupt"),
        (0x9093, "9093"),
        (0x90E0, "90E0 USB speed"),
        (0x90E2, "90E2 USB init trigger"),
        (0x92C0, "92C0 USB power"),
        (0x92C1, "92C1"),
        (0x92C2, "92C2 USB PHY"),
        (0x92C5, "92C5"),
        (0x92C6, "92C6"),
        (0x92C8, "92C8"),
        (0x92F7, "92F7"),
        (0x9241, "9241 USB PHY config"),
        (0x9300, "9300 EP config"),
        (0x9301, "9301 EP0 arm"),
        (0x9302, "9302"),
        (0x9303, "9303"),
        (0x9304, "9304"),
        (0x9305, "9305"),
        
        # Interrupt controller
        (0xC800, "C800"),
        (0xC801, "C801"),
        (0xC802, "C802 interrupt status"),
        (0xC805, "C805"),
        (0xC806, "C806 system status"),
        (0xC807, "C807"),
        (0xC809, "C809 interrupt routing"),
        (0xC471, "C471"),
        (0xC473, "C473"),
        
        # Channel config (C8Ax, C8Bx)
        (0xC8A1, "C8A1"),
        (0xC8A2, "C8A2"),
        (0xC8A3, "C8A3"),
        (0xC8A4, "C8A4"),
        (0xC8A6, "C8A6"),
        (0xC8A9, "C8A9"),
        (0xC8AA, "C8AA channel select"),
        (0xC8AB, "C8AB"),
        (0xC8AC, "C8AC"),
        (0xC8B2, "C8B2"),
        (0xC8B3, "C8B3"),
        (0xC8B4, "C8B4"),
        (0xC8B5, "C8B5"),
        (0xC8B6, "C8B6"),
        (0xC8B8, "C8B8"),
        
        # CCxx registers
        (0xCC2A, "CC2A"),
        (0xCC35, "CC35"),
        (0xCC80, "CC80"),
        (0xCC81, "CC81"),
        (0xCC82, "CC82"),
        (0xCC83, "CC83"),
        (0xCC90, "CC90"),
        (0xCC91, "CC91"),
        (0xCC92, "CC92"),
        (0xCC93, "CC93"),
        
        # Other
        (0xB480, "B480 PCIe link"),
        (0xCA2E, "CA2E"),
        (0xCA60, "CA60"),
        (0xCE89, "CE89 USB state machine"),
        (0xCD31, "CD31"),
        (0xC620, "C620"),
        (0xE7E3, "E7E3"),
        (0xE7FC, "E7FC"),
    ]
    
    print("\nRegister dump:")
    print("-" * 50)
    
    for addr, name in regs_to_check:
        try:
            val = read_reg(dev, addr)
            if val is not None:
                print(f"  {name}: 0x{val:02X}")
            else:
                print(f"  {name}: READ FAILED (None)")
        except Exception as e:
            print(f"  {name}: ERROR: {e}")
    
    # If specific addresses provided on command line, read those too
    if len(sys.argv) > 1:
        print("\nCustom addresses:")
        for arg in sys.argv[1:]:
            addr = int(arg, 16)
            try:
                val = read_reg(dev, addr)
                if val is not None:
                    print(f"  0x{addr:04X}: 0x{val:02X}")
                else:
                    print(f"  0x{addr:04X}: READ FAILED")
            except Exception as e:
                print(f"  0x{addr:04X}: ERROR: {e}")

if __name__ == "__main__":
    main()
