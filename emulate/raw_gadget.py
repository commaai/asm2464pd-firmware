"""
Raw Gadget Python Wrapper

Low-level interface to Linux USB Raw Gadget kernel module.
Allows emulating USB devices from userspace without additional hardware
when used with dummy_hcd.

Setup:
  # Build and load modules (from https://github.com/xairy/raw-gadget)
  cd raw-gadget/dummy_hcd && make && sudo ./insmod.sh
  cd raw-gadget/raw_gadget && make && sudo ./insmod.sh

Usage:
  gadget = RawGadget()
  gadget.init("dummy_udc", "dummy_udc.0", USB_SPEED_HIGH)
  gadget.run()
  while True:
      event = gadget.event_fetch()
      # Handle USB events...
"""

import os
import fcntl
import ctypes
import struct
from enum import IntEnum
from typing import Optional, Tuple, List
from dataclasses import dataclass

# ============================================
# Constants from raw_gadget.h
# ============================================

UDC_NAME_LENGTH_MAX = 128
USB_RAW_EPS_NUM_MAX = 30
USB_RAW_EP_NAME_MAX = 16
USB_RAW_EP_ADDR_ANY = 0xFF

# IO flags
USB_RAW_IO_FLAGS_ZERO = 0x0001
USB_RAW_IO_FLAGS_MASK = 0x0001

# USB speeds
class USBSpeed(IntEnum):
    USB_SPEED_UNKNOWN = 0
    USB_SPEED_LOW = 1
    USB_SPEED_FULL = 2
    USB_SPEED_HIGH = 3
    USB_SPEED_WIRELESS = 4
    USB_SPEED_SUPER = 5
    USB_SPEED_SUPER_PLUS = 6

# Event types
class USBRawEventType(IntEnum):
    INVALID = 0
    CONNECT = 1
    CONTROL = 2
    SUSPEND = 3
    RESUME = 4
    RESET = 5
    DISCONNECT = 6

# Standard USB request types
USB_DIR_OUT = 0x00
USB_DIR_IN = 0x80
USB_TYPE_STANDARD = 0x00
USB_TYPE_CLASS = 0x20
USB_TYPE_VENDOR = 0x40
USB_RECIP_DEVICE = 0x00
USB_RECIP_INTERFACE = 0x01
USB_RECIP_ENDPOINT = 0x02

# Standard USB requests
USB_REQ_GET_STATUS = 0x00
USB_REQ_CLEAR_FEATURE = 0x01
USB_REQ_SET_FEATURE = 0x03
USB_REQ_SET_ADDRESS = 0x05
USB_REQ_GET_DESCRIPTOR = 0x06
USB_REQ_SET_DESCRIPTOR = 0x07
USB_REQ_GET_CONFIGURATION = 0x08
USB_REQ_SET_CONFIGURATION = 0x09
USB_REQ_GET_INTERFACE = 0x0A
USB_REQ_SET_INTERFACE = 0x0B

# Descriptor types
USB_DT_DEVICE = 0x01
USB_DT_CONFIG = 0x02
USB_DT_STRING = 0x03
USB_DT_INTERFACE = 0x04
USB_DT_ENDPOINT = 0x05
USB_DT_DEVICE_QUALIFIER = 0x06
USB_DT_BOS = 0x0F

# ============================================
# IOCTL definitions
# ============================================

def _IOC(dir, type, nr, size):
    return (dir << 30) | (size << 16) | (ord(type) << 8) | nr

def _IO(type, nr):
    return _IOC(0, type, nr, 0)

def _IOR(type, nr, size):
    return _IOC(2, type, nr, size)

def _IOW(type, nr, size):
    return _IOC(1, type, nr, size)

def _IOWR(type, nr, size):
    return _IOC(3, type, nr, size)

# Raw gadget ioctls
# The size in ioctl number is sizeof(struct), not the buffer size
# struct usb_raw_init is 257 bytes (128 + 128 + 1)
USB_RAW_IOCTL_INIT = _IOW('U', 0, 257)
USB_RAW_IOCTL_RUN = _IO('U', 1)
# struct usb_raw_event: u32 type + u32 length + flexible array = 8 bytes base
USB_RAW_IOCTL_EVENT_FETCH = _IOR('U', 2, 8)
# struct usb_raw_ep_io: u16 ep + u16 flags + u32 length + flexible array = 8 bytes base
USB_RAW_IOCTL_EP0_WRITE = _IOW('U', 3, 8)
USB_RAW_IOCTL_EP0_READ = _IOWR('U', 4, 8)
# struct usb_endpoint_descriptor is 7 bytes
USB_RAW_IOCTL_EP_ENABLE = _IOW('U', 5, 7)
USB_RAW_IOCTL_EP_DISABLE = _IOW('U', 6, 4)
USB_RAW_IOCTL_EP_WRITE = _IOW('U', 7, 8)
USB_RAW_IOCTL_EP_READ = _IOWR('U', 8, 8)
USB_RAW_IOCTL_CONFIGURE = _IO('U', 9)
USB_RAW_IOCTL_VBUS_DRAW = _IOW('U', 10, 4)
# struct usb_raw_eps_info: 30 * sizeof(usb_raw_ep_info)
# usb_raw_ep_info: name[16] + u32 addr + caps(4) + limits(8) = 32 bytes
USB_RAW_IOCTL_EPS_INFO = _IOR('U', 11, 30 * 32)
USB_RAW_IOCTL_EP0_STALL = _IO('U', 12)
USB_RAW_IOCTL_EP_SET_HALT = _IOW('U', 13, 4)
USB_RAW_IOCTL_EP_CLEAR_HALT = _IOW('U', 14, 4)
USB_RAW_IOCTL_EP_SET_WEDGE = _IOW('U', 15, 4)

# ============================================
# Data structures
# ============================================

@dataclass
class USBControlRequest:
    """USB control transfer setup packet."""
    bRequestType: int
    bRequest: int
    wValue: int
    wIndex: int
    wLength: int

    @classmethod
    def from_bytes(cls, data: bytes) -> 'USBControlRequest':
        if len(data) < 8:
            raise ValueError(f"Control request too short: {len(data)} bytes")
        bRequestType, bRequest, wValue, wIndex, wLength = struct.unpack('<BBHHH', data[:8])
        return cls(bRequestType, bRequest, wValue, wIndex, wLength)

    @property
    def direction(self) -> int:
        return self.bRequestType & 0x80

    @property
    def type(self) -> int:
        return self.bRequestType & 0x60

    @property
    def recipient(self) -> int:
        return self.bRequestType & 0x1F

    def is_vendor(self) -> bool:
        return self.type == USB_TYPE_VENDOR

    def __repr__(self):
        dir_str = "IN" if self.direction else "OUT"
        type_str = {0x00: "STD", 0x20: "CLASS", 0x40: "VENDOR"}.get(self.type, "?")
        return (f"USBControlRequest({dir_str} {type_str} "
                f"req=0x{self.bRequest:02X} val=0x{self.wValue:04X} "
                f"idx=0x{self.wIndex:04X} len={self.wLength})")


@dataclass
class USBRawEvent:
    """USB event from raw gadget."""
    type: USBRawEventType
    length: int
    data: bytes

    def get_control_request(self) -> Optional[USBControlRequest]:
        """Parse control request from event data."""
        if self.type == USBRawEventType.CONTROL and self.length >= 8:
            return USBControlRequest.from_bytes(self.data)
        return None


@dataclass
class USBEndpointInfo:
    """Information about a USB endpoint."""
    name: str
    addr: int
    type_control: bool
    type_iso: bool
    type_bulk: bool
    type_int: bool
    dir_in: bool
    dir_out: bool
    maxpacket_limit: int
    max_streams: int


# ============================================
# Raw Gadget Interface
# ============================================

class RawGadgetError(Exception):
    """Error from raw gadget operations."""
    pass


class RawGadget:
    """
    Python interface to Linux USB Raw Gadget.

    Example usage:
        gadget = RawGadget()
        gadget.init("dummy_udc", "dummy_udc.0", USBSpeed.USB_SPEED_HIGH)
        gadget.run()

        while True:
            event = gadget.event_fetch()
            if event.type == USBRawEventType.CONTROL:
                ctrl = event.get_control_request()
                # Handle control request...
                gadget.ep0_write(response_data)
    """

    def __init__(self, device: str = "/dev/raw-gadget"):
        self.device = device
        self.fd: Optional[int] = None
        self.eps: List[USBEndpointInfo] = []
        self.configured = False
        self._enabled_eps: dict = {}  # ep_num -> fd

    def open(self):
        """Open the raw-gadget device."""
        if self.fd is not None:
            return
        try:
            self.fd = os.open(self.device, os.O_RDWR)
        except OSError as e:
            raise RawGadgetError(f"Failed to open {self.device}: {e}")

    def close(self):
        """Close the raw-gadget device."""
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *args):
        self.close()

    def init(self, driver_name: str, device_name: str, speed: USBSpeed = USBSpeed.USB_SPEED_HIGH):
        """
        Initialize the gadget with UDC driver and device.

        For dummy_hcd use:
            driver_name = "dummy_udc"
            device_name = "dummy_udc.0"
        """
        if self.fd is None:
            self.open()

        # Build usb_raw_init structure
        # struct usb_raw_init {
        #     __u8 driver_name[128];
        #     __u8 device_name[128];
        #     __u8 speed;
        # }
        driver_bytes = driver_name.encode('utf-8')[:127] + b'\x00' * (128 - min(len(driver_name), 127))
        device_bytes = device_name.encode('utf-8')[:127] + b'\x00' * (128 - min(len(device_name), 127))
        init_data = driver_bytes + device_bytes + bytes([speed])

        # Ensure exactly 257 bytes
        init_data = init_data[:257].ljust(257, b'\x00')

        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_INIT, init_data)
        except OSError as e:
            raise RawGadgetError(f"INIT failed: {e}")

    def run(self):
        """Start the gadget (begins USB connection process)."""
        if self.fd is None:
            raise RawGadgetError("Device not open")
        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_RUN)
        except OSError as e:
            raise RawGadgetError(f"RUN failed: {e}")

    def event_fetch(self, timeout_ms: int = 0) -> USBRawEvent:
        """
        Fetch the next USB event (blocking).

        Returns USBRawEvent with type and data.
        """
        if self.fd is None:
            raise RawGadgetError("Device not open")

        # struct usb_raw_event {
        #     __u32 type;
        #     __u32 length;
        #     __u8 data[];
        # }
        # CRITICAL: Must pre-set length field to tell kernel how much space we have
        buf = bytearray(8 + 1024)
        struct.pack_into('<II', buf, 0, 0, 1024)  # type=0, length=1024

        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_EVENT_FETCH, buf, True)  # mutate_flag=True
        except OSError as e:
            raise RawGadgetError(f"EVENT_FETCH failed: {e}")

        event_type, length = struct.unpack('<II', buf[:8])
        data = bytes(buf[8:8+length])

        return USBRawEvent(
            type=USBRawEventType(event_type),
            length=length,
            data=data
        )

    def ep0_write(self, data: bytes):
        """Write data to EP0 (control IN response)."""
        if self.fd is None:
            raise RawGadgetError("Device not open")

        # struct usb_raw_ep_io {
        #     __u16 ep;
        #     __u16 flags;
        #     __u32 length;
        #     __u8 data[];
        # }
        io_buf = bytearray(8 + len(data))
        struct.pack_into('<HHI', io_buf, 0, 0, 0, len(data))
        io_buf[8:8+len(data)] = data

        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_EP0_WRITE, io_buf, True)
        except OSError as e:
            raise RawGadgetError(f"EP0_WRITE failed: {e}")

    def ep0_read(self, length: int) -> bytes:
        """Read data from EP0 (control OUT data)."""
        if self.fd is None:
            raise RawGadgetError("Device not open")

        buf = bytearray(8 + length)
        struct.pack_into('<HHI', buf, 0, 0, 0, length)

        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_EP0_READ, buf, True)
        except OSError as e:
            raise RawGadgetError(f"EP0_READ failed: {e}")

        actual_len = struct.unpack_from('<I', buf, 4)[0]
        return bytes(buf[8:8+actual_len])

    def ep0_stall(self):
        """Stall EP0 (indicate error/unsupported request)."""
        if self.fd is None:
            raise RawGadgetError("Device not open")
        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_EP0_STALL)
        except OSError as e:
            raise RawGadgetError(f"EP0_STALL failed: {e}")

    def configure(self):
        """Configure the gadget (after SET_CONFIGURATION)."""
        if self.fd is None:
            raise RawGadgetError("Device not open")
        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_CONFIGURE)
            self.configured = True
        except OSError as e:
            raise RawGadgetError(f"CONFIGURE failed: {e}")

    def vbus_draw(self, ma: int):
        """Set VBUS current draw in mA."""
        if self.fd is None:
            raise RawGadgetError("Device not open")
        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_VBUS_DRAW, struct.pack('<I', ma))
        except OSError as e:
            raise RawGadgetError(f"VBUS_DRAW failed: {e}")

    def eps_info(self) -> List[USBEndpointInfo]:
        """Get information about available endpoints."""
        if self.fd is None:
            raise RawGadgetError("Device not open")

        # struct usb_raw_ep_info {
        #     __u8 name[16];
        #     __u32 addr;
        #     struct usb_raw_ep_caps caps;  // 4 bytes of bitfields
        #     struct usb_raw_ep_limits limits;  // 8 bytes
        # } -> 32 bytes per endpoint
        # struct usb_raw_eps_info has 30 of these
        buf = bytearray(30 * 32)

        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_EPS_INFO, buf)
        except OSError as e:
            raise RawGadgetError(f"EPS_INFO failed: {e}")

        eps = []
        for i in range(USB_RAW_EPS_NUM_MAX):
            offset = i * 32
            name = buf[offset:offset+16].rstrip(b'\x00').decode('utf-8', errors='ignore')
            if not name:
                continue

            addr = struct.unpack('<I', buf[offset+16:offset+20])[0]
            caps = struct.unpack('<I', buf[offset+20:offset+24])[0]
            maxpacket, max_streams = struct.unpack('<HH', buf[offset+24:offset+28])

            eps.append(USBEndpointInfo(
                name=name,
                addr=addr,
                type_control=bool(caps & 0x01),
                type_iso=bool(caps & 0x02),
                type_bulk=bool(caps & 0x04),
                type_int=bool(caps & 0x08),
                dir_in=bool(caps & 0x10),
                dir_out=bool(caps & 0x20),
                maxpacket_limit=maxpacket,
                max_streams=max_streams
            ))

        self.eps = eps
        return eps

    def ep_enable(self, ep_addr: int, ep_type: int, max_packet: int) -> int:
        """
        Enable an endpoint.

        ep_addr: Endpoint address (e.g., 0x81 for EP1 IN)
        ep_type: Endpoint type (1=iso, 2=bulk, 3=interrupt)
        max_packet: Maximum packet size

        Returns the endpoint number assigned by the kernel.
        """
        if self.fd is None:
            raise RawGadgetError("Device not open")

        # struct usb_endpoint_descriptor {
        #     __u8 bLength;
        #     __u8 bDescriptorType;
        #     __u8 bEndpointAddress;
        #     __u8 bmAttributes;
        #     __le16 wMaxPacketSize;
        #     __u8 bInterval;
        # }
        desc = struct.pack('<BBBBHB',
            7,  # bLength
            USB_DT_ENDPOINT,  # bDescriptorType
            ep_addr,
            ep_type,
            max_packet,
            1  # bInterval
        )

        try:
            result = fcntl.ioctl(self.fd, USB_RAW_IOCTL_EP_ENABLE, desc)
            ep_num = result if isinstance(result, int) else 0
            self._enabled_eps[ep_addr] = ep_num
            return ep_num
        except OSError as e:
            raise RawGadgetError(f"EP_ENABLE failed: {e}")

    def ep_disable(self, ep_num: int):
        """Disable an endpoint."""
        if self.fd is None:
            raise RawGadgetError("Device not open")
        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_EP_DISABLE, struct.pack('<I', ep_num))
        except OSError as e:
            raise RawGadgetError(f"EP_DISABLE failed: {e}")

    def ep_write(self, ep_num: int, data: bytes):
        """Write data to an endpoint (IN transfer)."""
        if self.fd is None:
            raise RawGadgetError("Device not open")

        io_data = struct.pack('<HHI', ep_num, 0, len(data)) + data
        io_data = io_data.ljust(8 + 1024, b'\x00')

        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_EP_WRITE, io_data)
        except OSError as e:
            raise RawGadgetError(f"EP_WRITE failed: {e}")

    def ep_read(self, ep_num: int, length: int) -> bytes:
        """Read data from an endpoint (OUT transfer)."""
        if self.fd is None:
            raise RawGadgetError("Device not open")

        buf = bytearray(struct.pack('<HHI', ep_num, 0, length) + b'\x00' * length)
        buf = buf.ljust(8 + 1024, b'\x00')

        try:
            fcntl.ioctl(self.fd, USB_RAW_IOCTL_EP_READ, buf)
        except OSError as e:
            raise RawGadgetError(f"EP_READ failed: {e}")

        actual_len = struct.unpack('<I', buf[4:8])[0]
        return bytes(buf[8:8+actual_len])


# ============================================
# Helper: Check if raw-gadget is available
# ============================================

def check_raw_gadget_available() -> Tuple[bool, str]:
    """
    Check if raw-gadget and dummy_hcd are available.

    Returns (available, message).
    """
    # Check for /dev/raw-gadget
    if not os.path.exists("/dev/raw-gadget"):
        return False, "raw-gadget module not loaded. Run: sudo modprobe raw_gadget"

    # Check for dummy_udc
    udc_path = "/sys/class/udc/dummy_udc.0"
    if not os.path.exists(udc_path):
        return False, "dummy_hcd module not loaded. Run: sudo modprobe dummy_hcd"

    # Check permissions
    try:
        fd = os.open("/dev/raw-gadget", os.O_RDWR)
        os.close(fd)
    except PermissionError:
        return False, "Permission denied. Run as root or add udev rule for /dev/raw-gadget"
    except OSError as e:
        return False, f"Cannot open /dev/raw-gadget: {e}"

    return True, "raw-gadget and dummy_hcd available"


if __name__ == "__main__":
    # Quick test
    available, msg = check_raw_gadget_available()
    print(f"Raw Gadget: {msg}")

    if available:
        print("\nTesting basic operations...")
        try:
            with RawGadget() as gadget:
                gadget.init("dummy_udc", "dummy_udc.0", USBSpeed.USB_SPEED_HIGH)
                print("Init: OK")

                eps = gadget.eps_info()
                print(f"Endpoints: {len(eps)} available")
                for ep in eps[:5]:
                    print(f"  {ep.name}: addr=0x{ep.addr:02X} bulk={ep.type_bulk} "
                          f"in={ep.dir_in} out={ep.dir_out}")

                print("\nReady to run gadget.run() and handle events")
        except RawGadgetError as e:
            print(f"Error: {e}")
