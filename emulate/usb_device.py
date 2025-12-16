"""
ASM2464PD USB Device Emulator

Makes the 8051 firmware emulator appear as a real USB device on Linux
using raw-gadget and dummy_hcd kernel modules.

This allows testing firmware with real USB tools like:
- python/usb.py (the tinygrad USB library)
- lsusb
- usbmon

Setup:
  # Load kernel modules
  sudo modprobe dummy_hcd
  sudo modprobe raw_gadget  # or build from github.com/xairy/raw-gadget

  # Run the emulator with USB device
  python emulate/usb_device.py [firmware.bin]

The emulated device will appear at VID:PID 0xADD1:0x0001 (ASM2464PD).
"""

import os
import sys
import struct
import threading
import queue
from typing import Optional, Callable
from dataclasses import dataclass

from raw_gadget import (
    RawGadget, RawGadgetError, USBRawEventType, USBControlRequest,
    USBSpeed, USB_DIR_IN, USB_DIR_OUT, USB_TYPE_VENDOR,
    USB_REQ_GET_DESCRIPTOR, USB_REQ_SET_ADDRESS, USB_REQ_SET_CONFIGURATION,
    USB_REQ_GET_CONFIGURATION, USB_REQ_SET_INTERFACE,
    USB_DT_DEVICE, USB_DT_CONFIG, USB_DT_STRING, USB_DT_BOS,
    check_raw_gadget_available
)

# ============================================
# ASM2464PD USB Descriptors
# ============================================

# VID:PID from python/usb.py
ASM2464_VID = 0xADD1
ASM2464_PID = 0x0001

# Device descriptor (18 bytes) - USB 2.0 High-Speed compatible
DEVICE_DESCRIPTOR = bytes([
    18,         # bLength
    0x01,       # bDescriptorType (Device)
    0x00, 0x02, # bcdUSB (2.00 for USB 2.0)
    0x00,       # bDeviceClass (defined at interface level)
    0x00,       # bDeviceSubClass
    0x00,       # bDeviceProtocol
    64,         # bMaxPacketSize0 (64 bytes for USB 2.0 High-Speed)
    ASM2464_VID & 0xFF, ASM2464_VID >> 8,  # idVendor
    ASM2464_PID & 0xFF, ASM2464_PID >> 8,  # idProduct
    0x00, 0x01, # bcdDevice
    0x01,       # iManufacturer
    0x02,       # iProduct
    0x03,       # iSerialNumber
    0x01,       # bNumConfigurations
])

# Configuration descriptor + interface + endpoints
# Based on the USB3 class with UAS-like bulk streaming
def make_config_descriptor():
    """Build full configuration descriptor with interface and endpoints."""
    # Interface descriptor (9 bytes)
    interface_desc = bytes([
        9,          # bLength
        0x04,       # bDescriptorType (Interface)
        0x00,       # bInterfaceNumber
        0x01,       # bAlternateSetting (alt 1 for streaming)
        0x04,       # bNumEndpoints
        0x08,       # bInterfaceClass (Mass Storage)
        0x06,       # bInterfaceSubClass (SCSI)
        0x62,       # bInterfaceProtocol (UAS)
        0x00,       # iInterface
    ])

    # Endpoint descriptors - USB 2.0 High-Speed (max 512 bytes for bulk)
    # EP1 IN (0x81) - Data IN - Bulk
    ep1_in = bytes([
        7,          # bLength
        0x05,       # bDescriptorType (Endpoint)
        0x81,       # bEndpointAddress (EP1 IN)
        0x02,       # bmAttributes (Bulk)
        0x00, 0x02, # wMaxPacketSize (512)
        0x00,       # bInterval
    ])

    # EP2 OUT (0x02) - Data OUT - Bulk
    ep2_out = bytes([
        7,          # bLength
        0x05,       # bDescriptorType (Endpoint)
        0x02,       # bEndpointAddress (EP2 OUT)
        0x02,       # bmAttributes (Bulk)
        0x00, 0x02, # wMaxPacketSize (512)
        0x00,       # bInterval
    ])

    # EP3 IN (0x83) - Status IN - Bulk
    ep3_in = bytes([
        7,          # bLength
        0x05,       # bDescriptorType (Endpoint)
        0x83,       # bEndpointAddress (EP3 IN)
        0x02,       # bmAttributes (Bulk)
        0x00, 0x02, # wMaxPacketSize (512)
        0x00,       # bInterval
    ])

    # EP4 OUT (0x04) - Command OUT - Bulk
    ep4_out = bytes([
        7,          # bLength
        0x05,       # bDescriptorType (Endpoint)
        0x04,       # bEndpointAddress (EP4 OUT)
        0x02,       # bmAttributes (Bulk)
        0x00, 0x02, # wMaxPacketSize (512)
        0x00,       # bInterval
    ])

    # Alt setting 0 (no endpoints, for initial enumeration)
    interface_alt0 = bytes([
        9,          # bLength
        0x04,       # bDescriptorType (Interface)
        0x00,       # bInterfaceNumber
        0x00,       # bAlternateSetting
        0x00,       # bNumEndpoints
        0x08,       # bInterfaceClass
        0x06,       # bInterfaceSubClass
        0x62,       # bInterfaceProtocol
        0x00,       # iInterface
    ])

    # Combine all descriptors
    descriptors = interface_alt0 + interface_desc + ep1_in + ep2_out + ep3_in + ep4_out
    total_length = 9 + len(descriptors)

    # Configuration descriptor header (9 bytes)
    config_header = bytes([
        9,          # bLength
        0x02,       # bDescriptorType (Configuration)
        total_length & 0xFF, total_length >> 8,  # wTotalLength
        0x01,       # bNumInterfaces
        0x01,       # bConfigurationValue
        0x00,       # iConfiguration
        0x80,       # bmAttributes (Bus powered)
        0xFA,       # bMaxPower (500mA)
    ])

    return config_header + descriptors

CONFIG_DESCRIPTOR = make_config_descriptor()

# String descriptors
STRING_DESCRIPTOR_0 = bytes([4, 0x03, 0x09, 0x04])  # Supported languages (English)

def make_string_descriptor(s: str) -> bytes:
    """Create a USB string descriptor from a Python string."""
    encoded = s.encode('utf-16-le')
    return bytes([2 + len(encoded), 0x03]) + encoded

STRING_DESCRIPTORS = {
    0: STRING_DESCRIPTOR_0,
    1: make_string_descriptor("ASMedia"),
    2: make_string_descriptor("ASM2464PD"),
    3: make_string_descriptor("000000000001"),
}

# BOS descriptor for USB 3.x
BOS_DESCRIPTOR = bytes([
    # BOS descriptor header
    5,          # bLength
    0x0F,       # bDescriptorType (BOS)
    22, 0,      # wTotalLength
    2,          # bNumDeviceCaps

    # USB 2.0 Extension
    7,          # bLength
    0x10,       # bDescriptorType (Device Capability)
    0x02,       # bDevCapabilityType (USB 2.0 Extension)
    0x02, 0x00, 0x00, 0x00,  # bmAttributes (LPM supported)

    # SuperSpeed USB Device Capability
    10,         # bLength
    0x10,       # bDescriptorType (Device Capability)
    0x03,       # bDevCapabilityType (SuperSpeed)
    0x00,       # bmAttributes
    0x0E, 0x00, # wSpeedsSupported (FS, HS, SS)
    0x01,       # bFunctionalitySupport (FS)
    0x0A,       # bU1DevExitLat
    0xFF, 0x07, # wU2DevExitLat
])


# ============================================
# USB Command Handler
# ============================================

@dataclass
class USBCommand:
    """Parsed USB command from host."""
    cmd_type: int       # 0xE4=read, 0xE5=write, 0x8A=scsi_write
    address: int        # Target XDATA address (for E4/E5)
    size: int           # Read size (E4) or 0
    value: int          # Write value (E5) or 0
    data: bytes         # SCSI write data


class ASM2464Device:
    """
    ASM2464PD USB device emulation using raw-gadget.

    Handles USB enumeration and forwards vendor commands (E4/E5)
    to the 8051 firmware emulator.
    """

    def __init__(self, memory_read: Callable[[int], int] = None,
                       memory_write: Callable[[int, int], None] = None):
        """
        Initialize the USB device emulator.

        Args:
            memory_read: Callback to read from XDATA (addr) -> value
            memory_write: Callback to write to XDATA (addr, value) -> None
        """
        self.gadget: Optional[RawGadget] = None
        self.running = False
        self.configured = False
        self.address = 0

        # Memory access callbacks (connect to 8051 emulator)
        self.memory_read = memory_read or (lambda addr: 0)
        self.memory_write = memory_write or (lambda addr, val: None)

        # Command queue for async processing
        self.cmd_queue = queue.Queue()
        self.response_queue = queue.Queue()

        # Endpoint numbers (assigned by kernel)
        self.ep_data_in = None   # 0x81
        self.ep_data_out = None  # 0x02
        self.ep_stat_in = None   # 0x83
        self.ep_cmd_out = None   # 0x04

        # Pending command state
        self.pending_cmd: Optional[USBCommand] = None

    def start(self, driver: str = "dummy_udc", device: str = "dummy_udc.0",
              speed: USBSpeed = USBSpeed.USB_SPEED_HIGH):
        """Start the USB device emulation."""
        print(f"[USB_DEV] Starting ASM2464PD emulation on {device}")

        self.gadget = RawGadget()
        self.gadget.open()
        self.gadget.init(driver, device, speed)

        print(f"[USB_DEV] Initialized, calling run()...")
        self.gadget.run()

        self.running = True
        print(f"[USB_DEV] Device running, waiting for host connection")

    def stop(self):
        """Stop the USB device emulation."""
        self.running = False
        if self.gadget:
            self.gadget.close()
            self.gadget = None
        print("[USB_DEV] Device stopped")

    def handle_events(self):
        """
        Main event loop - handle USB events from raw-gadget.

        This should be called in a loop or from a dedicated thread.
        """
        if not self.running or not self.gadget:
            return

        try:
            event = self.gadget.event_fetch()
        except RawGadgetError as e:
            print(f"[USB_DEV] Event fetch error: {e}")
            return

        if event.type == USBRawEventType.CONNECT:
            print(f"[USB_DEV] Host connected (speed={event.length})")
            # Get available endpoints
            eps = self.gadget.eps_info()
            print(f"[USB_DEV] {len(eps)} endpoints available")

        elif event.type == USBRawEventType.DISCONNECT:
            print("[USB_DEV] Host disconnected")
            self.configured = False

        elif event.type == USBRawEventType.RESET:
            print("[USB_DEV] Bus reset")
            self.configured = False
            self.address = 0

        elif event.type == USBRawEventType.SUSPEND:
            print("[USB_DEV] Suspend")

        elif event.type == USBRawEventType.RESUME:
            print("[USB_DEV] Resume")

        elif event.type == USBRawEventType.CONTROL:
            ctrl = event.get_control_request()
            if ctrl:
                self._handle_control(ctrl, event.data[8:] if event.length > 8 else b'')

    def _handle_control(self, ctrl: USBControlRequest, data: bytes):
        """Handle USB control transfer."""
        is_in = bool(ctrl.direction)  # USB_DIR_IN = 0x80

        # Standard requests
        if ctrl.type == 0x00:  # USB_TYPE_STANDARD
            if ctrl.bRequest == USB_REQ_GET_DESCRIPTOR:
                self._handle_get_descriptor(ctrl)
            elif ctrl.bRequest == USB_REQ_SET_ADDRESS:
                self.address = ctrl.wValue
                # OUT request: use ep0_read for status stage
                self.gadget.ep0_read(0)
                print(f"[USB_DEV] Set address: {self.address}")
            elif ctrl.bRequest == USB_REQ_SET_CONFIGURATION:
                self._handle_set_configuration(ctrl.wValue)
            elif ctrl.bRequest == USB_REQ_GET_CONFIGURATION:
                self.gadget.ep0_write(bytes([1 if self.configured else 0]))
            elif ctrl.bRequest == USB_REQ_SET_INTERFACE:
                self._handle_set_interface(ctrl.wIndex, ctrl.wValue)
            else:
                print(f"[USB_DEV] Unhandled standard request: 0x{ctrl.bRequest:02X}")
                self.gadget.ep0_stall()

        # Vendor requests (E4/E5 commands via control transfers)
        elif ctrl.type == USB_TYPE_VENDOR:
            self._handle_vendor_request(ctrl, data)

        else:
            print(f"[USB_DEV] Unhandled request type: 0x{ctrl.bRequestType:02X}")
            self.gadget.ep0_stall()

    def _handle_get_descriptor(self, ctrl: USBControlRequest):
        """Handle GET_DESCRIPTOR request."""
        desc_type = ctrl.wValue >> 8
        desc_index = ctrl.wValue & 0xFF
        max_len = ctrl.wLength

        if desc_type == USB_DT_DEVICE:
            self.gadget.ep0_write(DEVICE_DESCRIPTOR[:max_len])
            print(f"[USB_DEV] Sent device descriptor ({len(DEVICE_DESCRIPTOR)} bytes)")

        elif desc_type == USB_DT_CONFIG:
            self.gadget.ep0_write(CONFIG_DESCRIPTOR[:max_len])
            print(f"[USB_DEV] Sent config descriptor ({len(CONFIG_DESCRIPTOR)} bytes)")

        elif desc_type == USB_DT_STRING:
            if desc_index in STRING_DESCRIPTORS:
                desc = STRING_DESCRIPTORS[desc_index]
                self.gadget.ep0_write(desc[:max_len])
                print(f"[USB_DEV] Sent string descriptor {desc_index}")
            else:
                self.gadget.ep0_stall()

        elif desc_type == USB_DT_BOS:
            self.gadget.ep0_write(BOS_DESCRIPTOR[:max_len])
            print(f"[USB_DEV] Sent BOS descriptor")

        else:
            print(f"[USB_DEV] Unknown descriptor type: 0x{desc_type:02X}")
            self.gadget.ep0_stall()

    def _handle_set_configuration(self, config: int):
        """Handle SET_CONFIGURATION request."""
        print(f"[USB_DEV] Set configuration: {config}")

        if config == 1:
            self.gadget.configure()
            self.configured = True
            # OUT request: use ep0_read for status stage
            self.gadget.ep0_read(0)
            print("[USB_DEV] Device configured!")
        elif config == 0:
            self.configured = False
            self.gadget.ep0_read(0)
        else:
            self.gadget.ep0_stall()

    def _handle_set_interface(self, interface: int, alt_setting: int):
        """Handle SET_INTERFACE request."""
        print(f"[USB_DEV] Set interface {interface} alt {alt_setting}")

        if interface == 0 and alt_setting in (0, 1):
            if alt_setting == 1:
                # Enable bulk endpoints for streaming (512 bytes for USB 2.0 High-Speed)
                try:
                    self.ep_data_in = self.gadget.ep_enable(0x81, 2, 512)   # Bulk IN
                    self.ep_data_out = self.gadget.ep_enable(0x02, 2, 512)  # Bulk OUT
                    self.ep_stat_in = self.gadget.ep_enable(0x83, 2, 512)   # Status IN
                    self.ep_cmd_out = self.gadget.ep_enable(0x04, 2, 512)   # Command OUT
                    print(f"[USB_DEV] Endpoints enabled: IN={self.ep_data_in}, OUT={self.ep_data_out}, "
                          f"STAT={self.ep_stat_in}, CMD={self.ep_cmd_out}")
                except RawGadgetError as e:
                    print(f"[USB_DEV] Failed to enable endpoints: {e}")
                    self.gadget.ep0_stall()
                    return

            # OUT request: use ep0_read for status stage
            self.gadget.ep0_read(0)
        else:
            self.gadget.ep0_stall()

    def _handle_vendor_request(self, ctrl: USBControlRequest, data: bytes):
        """Handle vendor-specific control request (E4/E5 via control pipe)."""
        # This handles E4/E5 commands sent as control transfers
        # The real device uses bulk endpoints, but control transfers are simpler

        if ctrl.direction == USB_DIR_IN:
            # Read request (like E4)
            # wValue = address low, wIndex = address high
            addr = (ctrl.wIndex << 16) | ctrl.wValue
            size = ctrl.wLength

            # Read from emulator memory
            result = bytearray(size)
            for i in range(size):
                result[i] = self.memory_read(addr + i) & 0xFF

            self.gadget.ep0_write(bytes(result))
            print(f"[USB_DEV] Vendor read: addr=0x{addr:04X} size={size} -> {result.hex()}")

        else:
            # Write request (like E5)
            addr = (ctrl.wIndex << 16) | ctrl.wValue

            if ctrl.wLength > 0:
                # Read the data phase
                write_data = self.gadget.ep0_read(ctrl.wLength)
                for i, val in enumerate(write_data):
                    self.memory_write(addr + i, val)
                print(f"[USB_DEV] Vendor write: addr=0x{addr:04X} data={write_data.hex()}")
            else:
                print(f"[USB_DEV] Vendor write: addr=0x{addr:04X} (no data)")

            self.gadget.ep0_write(b'')  # ZLP ACK

    def process_bulk_command(self, cmd_data: bytes) -> Optional[bytes]:
        """
        Process a bulk command packet (from EP4 OUT).

        This handles the UAS-like command protocol used by ASM2464PD.
        Returns response data for EP1 IN, or None if no response needed.
        """
        if len(cmd_data) < 16:
            print(f"[USB_DEV] Command too short: {len(cmd_data)} bytes")
            return None

        # Parse command packet header
        # Based on python/usb.py cmd_template format:
        # [0x01, 0x00, 0x00, slot, ..., CDB at offset 16]
        slot = cmd_data[3]
        cdb = cmd_data[16:] if len(cmd_data) > 16 else b''

        if len(cdb) < 6:
            print(f"[USB_DEV] CDB too short: {len(cdb)} bytes")
            return None

        cmd_type = cdb[0]
        print(f"[USB_DEV] Bulk command: slot={slot} type=0x{cmd_type:02X} cdb={cdb[:8].hex()}")

        if cmd_type == 0xE4:
            # Read XDATA
            # Format: struct.pack('>BBBHB', 0xE4, size, addr >> 16, addr & 0xFFFF, 0)
            size = cdb[1]
            addr = (cdb[2] << 16) | (cdb[3] << 8) | cdb[4]
            # Convert from USB address format (0x50XXXX) to XDATA (0xXXXX)
            xdata_addr = addr & 0xFFFF

            result = bytearray(size)
            for i in range(size):
                result[i] = self.memory_read(xdata_addr + i) & 0xFF

            print(f"[USB_DEV] E4 read: addr=0x{xdata_addr:04X} size={size} -> {result.hex()}")
            return bytes(result)

        elif cmd_type == 0xE5:
            # Write XDATA
            # Format: struct.pack('>BBBHB', 0xE5, value, addr >> 16, addr & 0xFFFF, 0)
            value = cdb[1]
            addr = (cdb[2] << 16) | (cdb[3] << 8) | cdb[4]
            xdata_addr = addr & 0xFFFF

            self.memory_write(xdata_addr, value)
            print(f"[USB_DEV] E5 write: addr=0x{xdata_addr:04X} value=0x{value:02X}")
            return None  # No data response, just status

        elif cmd_type == 0x8A:
            # SCSI write
            # Format: struct.pack('>BBQIBB', 0x8A, 0, lba, sectors, 0, 0)
            lba = struct.unpack('>Q', cdb[2:10])[0]
            sectors = struct.unpack('>I', cdb[10:14])[0]
            print(f"[USB_DEV] SCSI write: LBA={lba} sectors={sectors}")
            # Data will come on EP2 OUT
            self.pending_cmd = USBCommand(0x8A, 0, sectors * 512, 0, b'')
            return None

        else:
            print(f"[USB_DEV] Unknown command type: 0x{cmd_type:02X}")
            return None


# ============================================
# Main entry point
# ============================================

def run_usb_device(firmware_path: str = None):
    """
    Run the USB device emulator standalone.

    If firmware_path is provided, loads and runs the firmware.
    Otherwise, uses simple memory stubs.
    """
    # Check raw-gadget availability
    available, msg = check_raw_gadget_available()
    if not available:
        print(f"Error: {msg}")
        print("\nSetup instructions:")
        print("  # Clone and build raw-gadget")
        print("  git clone https://github.com/xairy/raw-gadget")
        print("  cd raw-gadget/dummy_hcd && make && sudo ./insmod.sh")
        print("  cd ../raw_gadget && make && sudo ./insmod.sh")
        print("\n  # Or load pre-built modules if available")
        print("  sudo modprobe dummy_hcd")
        print("  sudo modprobe raw_gadget")
        return 1

    print(f"[USB_DEV] {msg}")

    # Simple memory for standalone testing
    xdata = bytearray(65536)

    def mem_read(addr):
        return xdata[addr & 0xFFFF]

    def mem_write(addr, val):
        xdata[addr & 0xFFFF] = val & 0xFF
        print(f"  [MEM] Write 0x{addr:04X} = 0x{val:02X}")

    # Create device
    device = ASM2464Device(memory_read=mem_read, memory_write=mem_write)

    try:
        device.start()

        print("\n[USB_DEV] Device should now appear in lsusb as ADD1:0001")
        print("[USB_DEV] Press Ctrl+C to stop\n")

        while device.running:
            device.handle_events()

    except KeyboardInterrupt:
        print("\n[USB_DEV] Interrupted")
    except Exception as e:
        print(f"[USB_DEV] Error: {e}")
        import traceback
        traceback.print_exc()
    finally:
        device.stop()

    return 0


if __name__ == "__main__":
    firmware = sys.argv[1] if len(sys.argv) > 1 else None
    sys.exit(run_usb_device(firmware))
