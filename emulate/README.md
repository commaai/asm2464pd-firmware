# ASM2464PD Emulator

8051 CPU emulator for testing and analyzing the ASM2464PD firmware.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Linux Host                               │
│                                                                  │
│  ┌──────────┐     USB      ┌──────────────────────────────────┐ │
│  │ USB App  │◄────────────►│         raw-gadget               │ │
│  │(usb.py)  │  (real USB)  │     (fake USB device)            │ │
│  └──────────┘              └──────────────┬───────────────────┘ │
│                                           │                      │
│                                      MMIO │ registers            │
│                                           ▼                      │
│                            ┌──────────────────────────────────┐ │
│                            │           Emulator               │ │
│                            │  ┌────────────────────────────┐  │ │
│                            │  │      fw.bin (8051)         │  │ │
│                            │  │   - USB interrupt handler  │  │ │
│                            │  │   - Vendor cmd processing  │  │ │
│                            │  │   - State machine          │  │ │
│                            │  └────────────────────────────┘  │ │
│                            │  ┌────────────────────────────┐  │ │
│                            │  │    Hardware Emulation      │  │ │
│                            │  │   - MMIO registers         │  │ │
│                            │  │   - DMA emulation          │  │ │
│                            │  │   - PCIe/NVMe stubs        │  │ │
│                            │  └────────────────────────────┘  │ │
│                            └──────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

## USB Device Passthrough (raw-gadget)

The `usb_device.py` creates a fake USB device using Linux's raw-gadget kernel module.

**CRITICAL: The fake USB device is a passthrough, NOT a handler.**

**NOTE: NEVER implement USB request handling in Python.** The firmware (fw.bin) handles ALL USB requests including GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION, and vendor commands. The emulator only sets MMIO registers to inject requests; the 8051 firmware code processes them. If USB enumeration doesn't work, fix the MMIO register configuration or investigate firmware flow - do NOT add Python code to handle USB requests directly.

When the host sends USB control messages:

**All USB Control Transfers** (standard and vendor):
1. **raw-gadget receives** the USB setup packet
2. **usb_device.py injects** the request into emulator MMIO registers
3. **Firmware processes** the request via USB interrupt handler (0x0E33)
4. **Firmware writes** response to appropriate registers/buffers
5. **usb_device.py reads** the response from emulator
6. **raw-gadget sends** the response back to the host

### Control Transfer Flow

```
Host                    raw-gadget/usb_device.py              Emulator (fw.bin)
  │                              │                                   │
  │──USB control transfer───────►│                                   │
  │                              │──inject to MMIO registers────────►│
  │                              │  (0x9E00-0x9E07 = setup packet)   │
  │                              │──trigger USB interrupt───────────►│
  │                              │                                   │
  │                              │                    [firmware runs]│
  │                              │                    [reads MMIO]   │
  │                              │                    [processes req]│
  │                              │                    [writes resp]  │
  │                              │                                   │
  │                              │◄──read response from XDATA/MMIO───│
  │◄─────response data──────────│                                   │
```

### USB Descriptor Handling

**Descriptors are handled entirely by firmware through MMIO:**

1. Firmware receives GET_DESCRIPTOR request via setup packet at 0x9E00-0x9E07
2. Firmware reads descriptor data from flash ROM mirror (XDATA 0xE4xx → Code ROM)
   - The hardware maps XDATA reads at 0xE400-0xE500 to code ROM with offset 0xDDFC
   - Formula: `code_addr = xdata_addr - 0xDDFC`
3. Firmware writes response to EP0 FIFO (0xC001)
4. Firmware triggers DMA (writes 0x04 to 0x9092)
5. Hardware DMA copies FIFO data to USB buffer at 0x8000
6. usb_device.py reads response from 0x8000

**IMPORTANT:** The emulator does NOT hardcode descriptor addresses. The `_flash_rom_mirror_read`
callback provides the MMIO hardware behavior - it maps XDATA reads to code ROM. The firmware
determines which addresses to read based on its own descriptor tables.

### Key MMIO Registers

| Register | Purpose |
|----------|---------|
| 0xC802   | Interrupt status (bit 0 = USB interrupt pending) |
| 0x9000   | USB connection status |
| 0x9101   | USB interrupt flags (bit 5 = vendor command path) |
| 0x9E00-0x9E07 | USB setup packet (8 bytes) |
| 0x910D-0x9112 | CDB data for SCSI/vendor commands |
| 0x90E3   | Bulk EP command (0x01=activate, 0x02=ack/arm CBW) |
| 0x8000+  | USB data buffer (for responses) |

### USB Setup Packet Registers (0x9E00-0x9E07)

The firmware handles ALL USB control transfers via these registers:

| Offset | Register | USB Setup Field |
|--------|----------|-----------------|
| 0x9E00 | bmRequestType | Request type (direction, type, recipient) |
| 0x9E01 | bRequest | Request code (e.g., 0x06 = GET_DESCRIPTOR) |
| 0x9E02 | wValue low | Value field low byte |
| 0x9E03 | wValue high | Value field high byte |
| 0x9E04 | wIndex low | Index field low byte |
| 0x9E05 | wIndex high | Index field high byte |
| 0x9E06 | wLength low | Data length low byte |
| 0x9E07 | wLength high | Data length high byte |

The firmware processes standard USB requests (GET_DESCRIPTOR, SET_ADDRESS, etc.)
as well as vendor-specific requests (E4/E5) through the USB interrupt handler at 0x0E33.

### Vendor Commands (E4/E5)

Vendor commands follow the same passthrough model:

1. Host sends UAS command packet on EP4 OUT
2. raw-gadget receives bulk transfer
3. usb_device.py injects CDB into registers 0x910D+
4. Firmware processes via vendor handler (0x5333 → 0x4583)
5. For E4 reads: firmware DMAs data to 0x8000
6. usb_device.py reads response buffer
7. raw-gadget sends data back on EP1 IN

## Files

- `emu.py` - Main Emulator class
- `cpu.py` - 8051 CPU emulation
- `memory.py` - Memory system (code, xdata, idata, sfr)
- `hardware.py` - MMIO register emulation
- `raw_gadget.py` - Python wrapper for raw-gadget kernel module
- `usb_device.py` - USB device passthrough (connects raw-gadget to emulator)

## Running Tests

```bash
# Test firmware handling (without real USB)
pytest test/test_emulator.py -v

# Test with real USB device (requires root, raw-gadget module)
sudo modprobe dummy_hcd raw_gadget
sudo python emulate/emu.py --usb-device fw.bin
```

## Requirements for USB Device Mode

1. Linux kernel with dummy_hcd and raw_gadget modules
2. Root privileges
3. No conflicting gadget drivers

```bash
# Load required modules
sudo modprobe dummy_hcd
sudo modprobe raw_gadget

# Verify
ls /dev/raw-gadget
ls /sys/class/udc/
```
