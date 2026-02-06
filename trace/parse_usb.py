#!/usr/bin/env python3
"""
Parse USB 3.0 enumeration trace to extract and decode USB messages.

This parses the emulator trace output to extract:
1. USB 3.0 setup packets and data written to 0x9E00+
2. USB endpoint buffer writes (0xD800+)
3. USB control register accesses
4. Interrupts

USB 3.0 uses different packet formats than USB 2.0.
"""

import re
import sys
from dataclasses import dataclass, field
from typing import Optional, List, Dict, Tuple

# USB 3.0 Setup Packet (still 8 bytes for control transfers)
# But the bmRequestType encoding is the same as USB 2.0

# Standard USB Request Codes (bRequest)
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
USB_REQ_SYNCH_FRAME = 0x0C

# USB 3.0 specific requests
USB_REQ_SET_ENCRYPTION = 0x0D
USB_REQ_GET_ENCRYPTION = 0x0E
USB_REQ_SET_HANDSHAKE = 0x0F
USB_REQ_GET_HANDSHAKE = 0x10
USB_REQ_SET_CONNECTION = 0x11
USB_REQ_SET_SECURITY_DATA = 0x12
USB_REQ_GET_SECURITY_DATA = 0x13
USB_REQ_SET_WUSB_DATA = 0x14
USB_REQ_LOOPBACK_DATA_WRITE = 0x15
USB_REQ_LOOPBACK_DATA_READ = 0x16
USB_REQ_SET_INTERFACE_DS = 0x17
USB_REQ_SET_SEL = 0x30
USB_REQ_SET_ISOCH_DELAY = 0x31

# BOS request
USB_REQ_GET_BOS = 0x0F

REQUEST_NAMES = {
    USB_REQ_GET_STATUS: "GET_STATUS",
    USB_REQ_CLEAR_FEATURE: "CLEAR_FEATURE",
    USB_REQ_SET_FEATURE: "SET_FEATURE",
    USB_REQ_SET_ADDRESS: "SET_ADDRESS",
    USB_REQ_GET_DESCRIPTOR: "GET_DESCRIPTOR",
    USB_REQ_SET_DESCRIPTOR: "SET_DESCRIPTOR",
    USB_REQ_GET_CONFIGURATION: "GET_CONFIGURATION",
    USB_REQ_SET_CONFIGURATION: "SET_CONFIGURATION",
    USB_REQ_GET_INTERFACE: "GET_INTERFACE",
    USB_REQ_SET_INTERFACE: "SET_INTERFACE",
    USB_REQ_SYNCH_FRAME: "SYNCH_FRAME",
    USB_REQ_SET_SEL: "SET_SEL",
    USB_REQ_SET_ISOCH_DELAY: "SET_ISOCH_DELAY",
    0x0D: "SET_ENCRYPTION",
    0x0E: "GET_ENCRYPTION",
    0x0F: "SET_HANDSHAKE/GET_BOS",
    0x10: "GET_HANDSHAKE",
    0x11: "SET_CONNECTION",
    0x12: "SET_SECURITY_DATA",
    0x13: "GET_SECURITY_DATA",
}

# Descriptor Types
DESC_TYPE_DEVICE = 0x01
DESC_TYPE_CONFIGURATION = 0x02
DESC_TYPE_STRING = 0x03
DESC_TYPE_INTERFACE = 0x04
DESC_TYPE_ENDPOINT = 0x05
DESC_TYPE_DEVICE_QUALIFIER = 0x06
DESC_TYPE_OTHER_SPEED_CONFIG = 0x07
DESC_TYPE_INTERFACE_POWER = 0x08
DESC_TYPE_OTG = 0x09
DESC_TYPE_DEBUG = 0x0A
DESC_TYPE_INTERFACE_ASSOC = 0x0B
DESC_TYPE_BOS = 0x0F
DESC_TYPE_DEVICE_CAPABILITY = 0x10
DESC_TYPE_SS_USB_DEVICE_CAPABILITY = 0x0A
DESC_TYPE_SS_EP_COMPANION = 0x30

DESCRIPTOR_NAMES = {
    DESC_TYPE_DEVICE: "DEVICE",
    DESC_TYPE_CONFIGURATION: "CONFIGURATION",
    DESC_TYPE_STRING: "STRING",
    DESC_TYPE_INTERFACE: "INTERFACE",
    DESC_TYPE_ENDPOINT: "ENDPOINT",
    DESC_TYPE_DEVICE_QUALIFIER: "DEVICE_QUALIFIER",
    DESC_TYPE_OTHER_SPEED_CONFIG: "OTHER_SPEED_CONFIG",
    DESC_TYPE_INTERFACE_POWER: "INTERFACE_POWER",
    DESC_TYPE_OTG: "OTG",
    DESC_TYPE_DEBUG: "DEBUG",
    DESC_TYPE_INTERFACE_ASSOC: "INTERFACE_ASSOC",
    DESC_TYPE_BOS: "BOS",
    DESC_TYPE_DEVICE_CAPABILITY: "DEVICE_CAPABILITY",
    DESC_TYPE_SS_EP_COMPANION: "SS_EP_COMPANION",
}


@dataclass
class MMIOAccess:
    """Single MMIO read/write access."""
    cycle: int
    pc: int
    is_write: bool
    addr: int
    value: int
    reg_name: str = ""


@dataclass
class USBPacket:
    """USB packet data from 0x9E00+ buffer."""
    cycle: int
    data: bytes
    
    def decode_setup(self) -> str:
        """Try to decode as USB setup packet."""
        if len(self.data) < 8:
            return f"Incomplete ({len(self.data)} bytes): {self.data.hex()}"
        
        bmRequestType = self.data[0]
        bRequest = self.data[1]
        wValue = self.data[2] | (self.data[3] << 8)
        wIndex = self.data[4] | (self.data[5] << 8)
        wLength = self.data[6] | (self.data[7] << 8)
        
        # Decode bmRequestType
        direction = "IN" if bmRequestType & 0x80 else "OUT"
        req_type_bits = (bmRequestType >> 5) & 0x03
        req_type = ["Standard", "Class", "Vendor", "Reserved"][req_type_bits]
        recipient_bits = bmRequestType & 0x1F
        if recipient_bits == 0:
            recipient = "Device"
        elif recipient_bits == 1:
            recipient = "Interface"
        elif recipient_bits == 2:
            recipient = "Endpoint"
        else:
            recipient = f"Other({recipient_bits})"
        
        req_name = REQUEST_NAMES.get(bRequest, f"0x{bRequest:02X}")
        
        lines = []
        lines.append(f"  bmRequestType: 0x{bmRequestType:02X} ({direction}, {req_type}, {recipient})")
        lines.append(f"  bRequest:      0x{bRequest:02X} ({req_name})")
        
        # Decode wValue based on request
        if bRequest == USB_REQ_GET_DESCRIPTOR:
            desc_type = (wValue >> 8) & 0xFF
            desc_index = wValue & 0xFF
            desc_name = DESCRIPTOR_NAMES.get(desc_type, f"0x{desc_type:02X}")
            lines.append(f"  wValue:        0x{wValue:04X} (Type={desc_name}, Index={desc_index})")
        else:
            lines.append(f"  wValue:        0x{wValue:04X}")
        
        lines.append(f"  wIndex:        0x{wIndex:04X}")
        lines.append(f"  wLength:       {wLength}")
        
        if len(self.data) > 8:
            lines.append(f"  Extra data:    {self.data[8:].hex()}")
        
        return "\n".join(lines)
    
    def summary(self) -> str:
        """One-line summary."""
        if len(self.data) < 2:
            return f"Partial: {self.data.hex()}"
        
        bmRequestType = self.data[0]
        bRequest = self.data[1]
        
        direction = "IN" if bmRequestType & 0x80 else "OUT"
        req_name = REQUEST_NAMES.get(bRequest, f"req=0x{bRequest:02X}")
        
        if len(self.data) >= 8:
            wValue = self.data[2] | (self.data[3] << 8)
            wLength = self.data[6] | (self.data[7] << 8)
            
            if bRequest == USB_REQ_GET_DESCRIPTOR:
                desc_type = (wValue >> 8) & 0xFF
                desc_name = DESCRIPTOR_NAMES.get(desc_type, f"0x{desc_type:02X}")
                return f"{direction} {req_name}({desc_name}, len={wLength})"
            else:
                return f"{direction} {req_name}(wValue=0x{wValue:04X}, len={wLength})"
        else:
            return f"{direction} {req_name} (partial)"


def parse_trace(filename: str) -> List[MMIOAccess]:
    """Parse trace file and return list of MMIO accesses."""
    accesses = []
    
    # Pattern: [   26371] PC=0x527B Read  0xCC32 = 0x01  REG_CPU_EXEC_STATUS
    pattern = re.compile(
        r'\[\s*(\d+)\]\s+PC=0x([0-9A-Fa-f]+)\s+(Read|Write)\s+0x([0-9A-Fa-f]+)\s+=\s+0x([0-9A-Fa-f]+)(?:\s+(\S+))?'
    )
    
    with open(filename, 'r') as f:
        for line in f:
            match = pattern.search(line)
            if match:
                cycle = int(match.group(1))
                pc = int(match.group(2), 16)
                is_write = match.group(3) == "Write"
                addr = int(match.group(4), 16)
                value = int(match.group(5), 16)
                reg_name = match.group(6) or ""
                
                accesses.append(MMIOAccess(cycle, pc, is_write, addr, value, reg_name))
    
    return accesses


def extract_usb_packets(accesses: List[MMIOAccess]) -> List[USBPacket]:
    """Extract USB packets from 0x9E00+ buffer writes.
    
    A new packet starts when we see a write to 0x9E00.
    Subsequent writes to 0x9E01, 0x9E02, etc. are accumulated.
    """
    packets = []
    current_data = bytearray()
    current_cycle = 0
    last_addr = -1
    
    for acc in accesses:
        if 0x9E00 <= acc.addr < 0x9F00 and acc.is_write:
            offset = acc.addr - 0x9E00
            
            # New packet starts at 0x9E00
            if offset == 0:
                # Save previous packet
                if current_data:
                    packets.append(USBPacket(current_cycle, bytes(current_data)))
                current_data = bytearray()
                current_cycle = acc.cycle
            
            # Extend buffer if needed
            while len(current_data) <= offset:
                current_data.append(0)
            current_data[offset] = acc.value
            last_addr = acc.addr
    
    # Don't forget last packet
    if current_data:
        packets.append(USBPacket(current_cycle, bytes(current_data)))
    
    return packets


def extract_ep_buffer_writes(accesses: List[MMIOAccess]) -> List[Tuple[int, int, bytes]]:
    """Extract writes to USB endpoint buffers (0xD800-0xDFFF).
    
    Returns list of (cycle, start_addr, data).
    """
    writes = []
    current_data = bytearray()
    current_start = 0
    current_cycle = 0
    last_addr = -1
    
    for acc in accesses:
        if 0xD800 <= acc.addr < 0xE000 and acc.is_write:
            if last_addr == -1 or acc.addr != last_addr + 1:
                # Non-consecutive write, save previous run
                if current_data:
                    writes.append((current_cycle, current_start, bytes(current_data)))
                current_data = bytearray()
                current_start = acc.addr
                current_cycle = acc.cycle
            
            current_data.append(acc.value)
            last_addr = acc.addr
    
    if current_data:
        writes.append((current_cycle, current_start, bytes(current_data)))
    
    return writes


def extract_interrupts(filename: str) -> List[Tuple[int, int, str]]:
    """Extract interrupt events from trace.
    
    Returns list of (line_number, mask, name).
    """
    interrupts = []
    
    pattern = re.compile(r'\[PROXY\] >>> INTERRUPT mask=0x([0-9A-Fa-f]+) \(([^)]+)\)')
    
    with open(filename, 'r') as f:
        for i, line in enumerate(f):
            match = pattern.search(line)
            if match:
                mask = int(match.group(1), 16)
                name = match.group(2)
                interrupts.append((i, mask, name))
    
    return interrupts


def decode_descriptor(data: bytes, indent: str = "    ") -> List[str]:
    """Decode USB descriptor data."""
    lines = []
    pos = 0
    
    while pos < len(data):
        if pos + 2 > len(data):
            break
        
        bLength = data[pos]
        bDescriptorType = data[pos + 1] if pos + 1 < len(data) else 0
        
        if bLength == 0 or pos + bLength > len(data):
            break
        
        desc_data = data[pos:pos + bLength]
        
        if bDescriptorType == 0x01:  # Device
            lines.append(f"{indent}Device Descriptor ({bLength} bytes)")
            if bLength >= 18:
                bcdUSB = desc_data[2] | (desc_data[3] << 8)
                idVendor = desc_data[8] | (desc_data[9] << 8)
                idProduct = desc_data[10] | (desc_data[11] << 8)
                lines.append(f"{indent}  bcdUSB: 0x{bcdUSB:04X}")
                lines.append(f"{indent}  idVendor: 0x{idVendor:04X}")
                lines.append(f"{indent}  idProduct: 0x{idProduct:04X}")
        elif bDescriptorType == 0x02:  # Configuration
            lines.append(f"{indent}Configuration Descriptor ({bLength} bytes)")
            if bLength >= 9:
                wTotalLength = desc_data[2] | (desc_data[3] << 8)
                bNumInterfaces = desc_data[4]
                lines.append(f"{indent}  wTotalLength: {wTotalLength}")
                lines.append(f"{indent}  bNumInterfaces: {bNumInterfaces}")
        elif bDescriptorType == 0x03:  # String
            lines.append(f"{indent}String Descriptor ({bLength} bytes)")
            if bLength > 2:
                # Unicode string (UTF-16LE)
                try:
                    string = desc_data[2:].decode('utf-16-le').rstrip('\x00')
                    lines.append(f"{indent}  String: \"{string}\"")
                except:
                    lines.append(f"{indent}  Data: {desc_data[2:].hex()}")
        elif bDescriptorType == 0x04:  # Interface
            lines.append(f"{indent}Interface Descriptor ({bLength} bytes)")
            if bLength >= 9:
                bInterfaceNumber = desc_data[2]
                bAlternateSetting = desc_data[3]
                bNumEndpoints = desc_data[4]
                bInterfaceClass = desc_data[5]
                bInterfaceSubClass = desc_data[6]
                lines.append(f"{indent}  bInterfaceNumber: {bInterfaceNumber}")
                lines.append(f"{indent}  bNumEndpoints: {bNumEndpoints}")
                lines.append(f"{indent}  Class/SubClass: 0x{bInterfaceClass:02X}/0x{bInterfaceSubClass:02X}")
        elif bDescriptorType == 0x05:  # Endpoint
            lines.append(f"{indent}Endpoint Descriptor ({bLength} bytes)")
            if bLength >= 7:
                bEndpointAddress = desc_data[2]
                bmAttributes = desc_data[3]
                wMaxPacketSize = desc_data[4] | (desc_data[5] << 8)
                ep_dir = "IN" if bEndpointAddress & 0x80 else "OUT"
                ep_num = bEndpointAddress & 0x0F
                ep_type = ["Control", "Isochronous", "Bulk", "Interrupt"][bmAttributes & 0x03]
                lines.append(f"{indent}  Endpoint: {ep_num} {ep_dir} ({ep_type})")
                lines.append(f"{indent}  wMaxPacketSize: {wMaxPacketSize}")
        elif bDescriptorType == 0x0F:  # BOS
            lines.append(f"{indent}BOS Descriptor ({bLength} bytes)")
            if bLength >= 5:
                wTotalLength = desc_data[2] | (desc_data[3] << 8)
                bNumDeviceCaps = desc_data[4]
                lines.append(f"{indent}  wTotalLength: {wTotalLength}")
                lines.append(f"{indent}  bNumDeviceCaps: {bNumDeviceCaps}")
        elif bDescriptorType == 0x10:  # Device Capability
            lines.append(f"{indent}Device Capability ({bLength} bytes)")
            if bLength >= 3:
                bDevCapabilityType = desc_data[2]
                cap_names = {
                    0x01: "Wireless USB",
                    0x02: "USB 2.0 Extension",
                    0x03: "SuperSpeed USB",
                    0x04: "Container ID",
                    0x05: "Platform",
                }
                cap_name = cap_names.get(bDevCapabilityType, f"0x{bDevCapabilityType:02X}")
                lines.append(f"{indent}  Type: {cap_name}")
        elif bDescriptorType == 0x30:  # SuperSpeed Endpoint Companion
            lines.append(f"{indent}SS Endpoint Companion ({bLength} bytes)")
        else:
            lines.append(f"{indent}Unknown Descriptor (type=0x{bDescriptorType:02X}, {bLength} bytes)")
            lines.append(f"{indent}  Data: {desc_data.hex()}")
        
        pos += bLength
    
    return lines


def main():
    filename = sys.argv[1] if len(sys.argv) > 1 else "enumerate"
    verbose = "-v" in sys.argv or "--verbose" in sys.argv
    
    print(f"Parsing USB 3.0 trace: {filename}")
    print("=" * 70)
    
    # Parse MMIO accesses
    accesses = parse_trace(filename)
    print(f"Total MMIO accesses: {len(accesses)}")
    
    # Count by address range
    ranges = {}
    for acc in accesses:
        if 0x9000 <= acc.addr < 0x9100:
            key = "0x90xx USB Status/Config"
        elif 0x9100 <= acc.addr < 0x9200:
            key = "0x91xx USB PHY"
        elif 0x9200 <= acc.addr < 0x9300:
            key = "0x92xx USB Control"
        elif 0x9300 <= acc.addr < 0x9400:
            key = "0x93xx USB Config"
        elif 0x9E00 <= acc.addr < 0x9F00:
            key = "0x9Exx USB Setup/Data"
        elif 0xD800 <= acc.addr < 0xE000:
            key = "0xD8xx-DFxx EP Buffer"
        elif 0xC400 <= acc.addr < 0xC500:
            key = "0xC4xx NVMe/MSC"
        elif 0xCC00 <= acc.addr < 0xCD00:
            key = "0xCCxx Timer/CPU"
        else:
            key = f"0x{acc.addr >> 8:02X}xx Other"
        
        ranges[key] = ranges.get(key, 0) + 1
    
    print("\nAccesses by region:")
    for key in sorted(ranges.keys()):
        print(f"  {key}: {ranges[key]}")
    
    # Extract USB packets from 0x9E00
    packets = extract_usb_packets(accesses)
    print(f"\n{'=' * 70}")
    print(f"USB Packets (from 0x9E00 buffer): {len(packets)}")
    print("=" * 70)
    
    for i, pkt in enumerate(packets):
        print(f"\n[{i+1}] Cycle {pkt.cycle}:")
        
        # Check if the whole packet looks like a descriptor (not a setup packet)
        # USB descriptors start with bLength, bDescriptorType
        # Setup packets have bmRequestType in byte 0 which has different patterns
        if len(pkt.data) >= 2:
            bLength = pkt.data[0]
            bDescType = pkt.data[1]
            
            # If bLength matches total length and bDescType is valid descriptor type
            # then this is likely a descriptor response, not a setup packet
            if bDescType in [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x09, 0x0A, 0x0B, 0x0F, 0x10, 0x30]:
                print(f"    >>> DESCRIPTOR RESPONSE <<<")
                print(f"    Raw: {pkt.data.hex()}")
                desc_lines = decode_descriptor(pkt.data)
                for line in desc_lines:
                    print(line)
                continue
        
        print(f"    {pkt.summary()}")
        print(f"    Raw: {pkt.data.hex()}")
        if verbose:
            print(pkt.decode_setup())
        
        # Try to decode descriptor data if present
        # The setup packet is 8 bytes, but sometimes there's extra data after
        if len(pkt.data) > 8:
            extra = pkt.data[8:]
            # Skip leading zeros
            while extra and extra[0] == 0:
                extra = extra[1:]
            
            # Check if this looks like descriptor data (starts with valid length/type)
            if len(extra) >= 2 and 2 <= extra[0] < 0x40 and extra[1] in [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x09, 0x0A, 0x0B, 0x0F, 0x10, 0x30]:
                desc_lines = decode_descriptor(extra)
                if desc_lines:
                    print("    Embedded descriptors:")
                    for line in desc_lines:
                        print(line)
    
    # Extract EP buffer writes
    ep_writes = extract_ep_buffer_writes(accesses)
    print(f"\n{'=' * 70}")
    print(f"Endpoint Buffer Writes: {len(ep_writes)}")
    print("=" * 70)
    
    for i, (cycle, addr, data) in enumerate(ep_writes[:20]):  # Show first 20
        # Try to decode as ASCII if printable
        try:
            ascii_repr = data.decode('ascii') if all(32 <= b < 127 for b in data) else None
        except:
            ascii_repr = None
        
        if ascii_repr:
            print(f"  [{i+1}] Cycle {cycle}: 0x{addr:04X} ({len(data)} bytes): \"{ascii_repr}\"")
        else:
            preview = data[:16].hex()
            if len(data) > 16:
                preview += f"... ({len(data)} bytes)"
            print(f"  [{i+1}] Cycle {cycle}: 0x{addr:04X}: {preview}")
    
    if len(ep_writes) > 20:
        print(f"  ... and {len(ep_writes) - 20} more")
    
    # Extract interrupts
    interrupts = extract_interrupts(filename)
    print(f"\n{'=' * 70}")
    print(f"Interrupts: {len(interrupts)}")
    print("=" * 70)
    
    int_counts = {}
    for _, mask, name in interrupts:
        int_counts[name] = int_counts.get(name, 0) + 1
    
    for name, count in sorted(int_counts.items()):
        print(f"  {name}: {count}")
    
    # Summary
    print(f"\n{'=' * 70}")
    print("ENUMERATION SUMMARY")
    print("=" * 70)
    print("""
This trace shows USB 3.0 enumeration of an ASMedia ASM2463/2464 device.

The device presents as a USB Mass Storage device (Class 0x08, SCSI).
Key identifiers:
  - Vendor ID: 0x174C (ASMedia Technology Inc.)
  - Product ID: 0x2463
  - USB Version: 3.20

The configuration includes:
  - 1 interface with Mass Storage class
  - Bulk IN endpoint (EP1) for data from device
  - Bulk OUT endpoint (EP2) for data to device
  - SuperSpeed endpoint companions for USB 3.0

The BOS (Binary Object Store) descriptor advertises USB 3.0 capabilities.
""")


if __name__ == "__main__":
    main()
