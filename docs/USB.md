# ASM2464PD USB Peripheral Documentation

This document describes the USB peripheral subsystem of the ASM2464PD chip based on reverse engineering of the firmware.

## Overview

### Device Role
The ASM2464PD operates as a USB Mass Storage device implementing the Bulk-Only Transport (BOT)
protocol. It bridges USB to NVMe/PCIe storage devices, presenting them as USB mass storage to
the host.

### USB Endpoints
| Endpoint | Type      | Direction | Max Packet Size         | Description           |
|----------|-----------|-----------|-------------------------|-----------------------|
| EP0      | Control   | Both      | 64B (FS), 512B (SS)     | Device configuration  |
| EP1      | Bulk      | IN        | 512B (HS), 1024B (SS)   | Data to host          |
| EP2      | Bulk      | OUT       | 512B (HS), 1024B (SS)   | Data from host        |

### USB Speed Support
- USB 3.x SuperSpeed (5 Gbps) - Primary mode
- USB 2.0 High-Speed (480 Mbps) - Fallback mode
- USB 1.1 Full-Speed (12 Mbps) - Supported but rarely used

### Interrupt Model
The USB peripheral uses two external interrupts:
- **INT0 (0x0003)**: Main USB interrupt handler (`int0` @ 0x0e5b)
  - Dispatches based on USB_MMIO_9101 (INT_DISPATCH) bits
  - Handles: link events, control transfers, bulk transfers, USB3 queue processing
- **INT1 (0x000b)**: Secondary interrupt handler (`int1` @ 0x4486)
  - PCIe/NVMe related interrupts
  - USB connection status handling
  - Power management events

Both interrupts are edge-triggered, low priority (PX0=0, PX1=0).

**INT1 Handler Dispatch (int1 @ 0x4486):**
```
Check MMIO_c806 bit 0: queue_b4ba()
Check MMIO_cc33 bit 2: write cc33=4, queue_cd10()
Check MMIO_c80a bit 6: queue_af5e()
If EXTMEM_USB_CONN_STATUS bits 0,1,7 set:
  Check c80a bit 5: FUN_CODE_061a()
  Check c80a bit 4: queue_c105()
  Check XRAM4_ec06 bit 0: set ec04=1, power sequence, queue_c0a5()
  Check c80a bits 0-3: queue_e911()
Check MMIO_c806 bit 4: queue_ef4e()
```

## Memory Map

The USB peripheral uses the following memory regions in XDATA space:

| Address Range   | Name              | Description                              |
|-----------------|-------------------|------------------------------------------|
| 0x8000-0x8FFF   | USB_DOUT          | USB data output buffer (4KB)             |
| 0x9000-0x93FF   | USB_MMIO          | Main USB MMIO registers (1KB)            |
| 0x9400-0x97FF   | USB_MMIO_MIRROR1  | Mirror of USB_MMIO                       |
| 0x9800-0x9BFF   | USB_MMIO_MIRROR2  | Mirror of USB_MMIO                       |
| 0x9C00-0x9DFF   | USB_MMIO_MIRROR3  | Mirror of USB_MMIO (512 bytes)           |
| 0x9E00-0x9FFF   | USB_CONTROL       | USB control packet area (512 bytes)      |

## USB MMIO Registers (0x9000-0x93FF)

### Core Status Registers

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x9000  | USB_MODE          | USB mode/status. Bit 0: 1=USB3 SuperSpeed, 0=USB2 High-Speed. Bit 2: Toggle bit for reset |
| 0x9002  | USB_PHY_MODE      | USB PHY mode control. Bit 1: PHY enable        |
| 0x9003  | USB_XFER_LEN_LO   | Transfer length low byte                       |
| 0x9004  | USB_XFER_LEN_HI   | Transfer length high byte                      |
| 0x9006  | USB_EP_READY      | Endpoint ready control. Bit 0: EP0 ready, Bit 7: ?? |
| 0x900B  | USB_EP_CTRL       | Endpoint control. Bits 0-3: Various EP flags   |
| 0x9010  | USB_MODE_CFG1     | USB mode config. Write 0xFE (USB3) or 0 (USB2) |
| 0x9018  | USB_MODE_CFG2     | USB mode config 2. Write 3 (USB3) or 2 (USB2)  |
| 0x901A  | USB_EP_INIT       | Endpoint init register. Write 0x0D on init     |

### Endpoint Ready Registers

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x905D  | USB_EP_BULK_OUT   | Bulk OUT endpoint ready. Bit 0: Ready          |
| 0x905F  | USB_EP_BULK_IN    | Bulk IN endpoint ready. Bit 0: Ready           |

### Endpoint Address Registers

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x900C  | USB_EP_ADDR_LO1   | Endpoint address low byte 1                    |
| 0x900D  | USB_EP_ADDR_HI1   | Endpoint address high byte 1                   |
| 0x900E  | USB_EP_ADDR_LO2   | Endpoint address low byte 2                    |
| 0x900F  | USB_EP_ADDR_HI2   | Endpoint address high byte 2                   |

### Control Endpoint Registers

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x9090  | USB_EP0_STATUS    | EP0 status. Bit 7: Busy/active (check for reset)|
| 0x9091  | USB_SS_INT_STATUS | USB3 SuperSpeed interrupt status register      |
|         |                   | Bit 0: Connect/disconnect event                |
|         |                   | Bit 1: Transfer complete event                 |
|         |                   | Bit 2: Error event                             |
|         |                   | Bit 3: Suspend/resume event                    |
|         |                   | Bit 4: Endpoint setup event                    |
|         |                   | Write bit value to clear interrupt             |
| 0x9092  | USB_XFER_CTRL     | Transfer control. Write to trigger transfers   |
|         |                   | Value 1: Trigger type 1                        |
|         |                   | Value 2: Clear/ack transfer                    |
| 0x9093  | USB_CTRL_EP_INT   | Control endpoint interrupt status              |
|         |                   | Bit 0: Setup complete                          |
|         |                   | Bit 1: Data stage complete (write 2 to ack)    |
|         |                   | Bit 2: Status stage complete                   |
|         |                   | Bit 3: Control transfer error (write 8 to ack) |
| 0x9094  | USB_CTRL_EP_CFG   | Control endpoint config. Values: 2, 8, 0x10    |
| 0x9096  | USB_RESET_INT     | USB reset interrupt status. Bit 0: Reset detected |
| 0x909E  | USB_STATUS        | USB status register. Bit 0: Status flag        |
| 0x90A0  | USB_BULK_OUT_ACK  | Bulk OUT acknowledge. Write 1 to ack           |
| 0x90A1  | USB_STATUS_TRIG   | Status stage trigger. Write 1 to trigger       |

### Bulk Transfer Registers

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x90E2  | USB_BULK_INT      | Bulk transfer interrupt status. Bit 0: Interrupt pending |
| 0x90E3  | USB_BULK_CTRL     | Bulk transfer control. Write 0x02 to clear     |

### Main Interrupt Status Register

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x9100  | USB_SPEED         | USB negotiated speed. Bits 0-1:                |
|         |                   |   0 = Disconnected                             |
|         |                   |   1 = Full Speed (USB 1.1)                     |
|         |                   |   2 = High Speed (USB 2.0)                     |
|         |                   |   3 = SuperSpeed (USB 3.x)                     |
| 0x9101  | USB_INT_STATUS    | Main interrupt status register                 |
|         |                   | Bit 0: Link events (check 0x91D1)              |
|         |                   | Bit 1: Secondary interrupt                     |
|         |                   | Bit 2: Control endpoint events (check 0x9093)  |
|         |                   | Bit 3: Link management (check 0x9301)          |
|         |                   | Bit 4: Endpoint events (check 0x9302)          |
|         |                   | Bit 5: USB3 queue processing / USB2 handling   |
|         |                   | Bit 6: Bulk transfer events (check 0x90E2)     |
| 0x9105  | USB_SETUP_PKT     | Setup packet byte (possibly bmRequestType)     |

### Setup Packet Registers (0x9105-0x910E)

The USB setup packet (8 bytes) is accessible in the 0x9105-0x910E region. Note that the
actual bmRequestType and bRequest are copied to EXTMEM_0001/0002 for processing.

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x9105  | USB_SETUP_BMREQ   | bmRequestType field. Checked against 0xFF in   |
|         |                   | usb_multi_status_check for valid setup packet  |
| 0x910D  | USB_SETUP_WLEN_LO | wLength low byte (setup packet byte 6)         |
| 0x910E  | USB_SETUP_WLEN_HI | wLength high byte (setup packet byte 7)        |

**Setup Packet Processing:**
The control transfer handler reads bmRequestType from EXTMEM_0002 and bRequest from
EXTMEM_0001. The following bmRequestType values are recognized:
- 0xE3 (-0x1d): Vendor IN request type 1
- 0xE1 (-0x1f): Vendor OUT request type 1
- 0xF9 (-7): Vendor request type 2
- 0xFB (-5): Vendor request type 3

### USB3 Queue Registers

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x9118  | USB3_QUEUE_IDX    | USB3 transfer queue index                      |
| 0x911D  | USB3_ADDR_LO      | USB3 address low byte                          |
| 0x911E  | USB3_ADDR_HI      | USB3 address high byte                         |

### USB Mass Storage CBW Registers (0x9119-0x9128)

The Command Block Wrapper (CBW) for USB Mass Storage BOT protocol is received at:

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x9119  | CBW_SIG_0         | CBW signature byte 0 (should be 0x00)          |
| 0x911A  | CBW_SIG_1         | CBW signature byte 1 (should be 0x1F)          |
| 0x911B  | CBW_SIG_2         | CBW signature byte 2 ('U' = 0x55)              |
| 0x911C  | CBW_SIG_3         | CBW signature byte 3 ('S' = 0x53)              |
| 0x911D  | CBW_SIG_4         | CBW signature byte 4 ('B' = 0x42)              |
| 0x911E  | CBW_SIG_5         | CBW signature byte 5 ('C' = 0x43)              |
| 0x911F  | CBW_DATA_0        | CBW data field 0 -> XRAM3_d804                 |
| 0x9120  | CBW_DATA_1        | CBW data field 1 -> XRAM3_d805                 |
| 0x9121  | CBW_DATA_2        | CBW data field 2 -> XRAM3_d806                 |
| 0x9122  | CBW_DATA_3        | CBW data field 3 -> XRAM3_d807                 |
| 0x9123  | CBW_XFER_LEN_0    | CBW data transfer length byte 0                |
| 0x9124  | CBW_XFER_LEN_1    | CBW data transfer length byte 1                |
| 0x9125  | CBW_XFER_LEN_2    | CBW data transfer length byte 2                |
| 0x9126  | CBW_XFER_LEN_3    | CBW data transfer length byte 3                |
| 0x9127  | CBW_FLAGS         | CBW flags. Bit 7: Direction (1=IN, 0=OUT)      |
| 0x9128  | CBW_LUN           | CBW LUN. Bits 0-3: Logical Unit Number         |

The CBW signature "USBC" is 0x43425355 in little-endian format.

### Link Status Registers

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x91C0  | USB_LINK_STATUS   | USB link status. Bit 1: Link up                |
| 0x91C4  | USB3_EP_CFG_LO    | USB3 endpoint config address low byte          |
| 0x91C5  | USB3_EP_CFG_HI    | USB3 endpoint config address high byte         |
| 0x91C6  | USB3_EP_CFG_CMD   | USB3 endpoint config command                   |
|         |                   |   1 = Config type 1                            |
|         |                   |   2 = Config type 2                            |
|         |                   |   4 = Config type 3                            |
|         |                   |   8 = Config type 4 (write 0x37 bytes)         |
| 0x91D1  | USB_LINK_EVENT    | USB link event status                          |
|         |                   | Bit 0: Link state change (write 1 to ack)      |
|         |                   | Bit 1: Speed negotiation (write 2 to ack)      |
|         |                   | Bit 2: Resume detected (write 4 to ack)        |
|         |                   | Bit 3: Suspend detected (write 8 to ack)       |

### USB Control Registers

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x9200  | USB_EP_STATUS     | Endpoint status. Bit 6: Clear/set NAK          |
| 0x9201  | USB_CTRL_REG      | USB control register. Bit 4: Toggle, Bits 0-1: Clear |
| 0x920C  | USB_BULK_STATUS   | Bulk endpoint status. Bits 0-1: Clear flags    |
| 0x920F  | USB_EP_RESET      | Endpoint reset. Bit 4: Toggle for reset        |
| 0x9220  | USB_PHY_CTRL3     | PHY control 3. Bit 2: Clear for reset          |
| 0x9241  | USB_EP_BUF_CFG    | Endpoint buffer config. Bit 4: Enable, Bits 6-7: Mode |
| 0x924C  | USB_MODE_CTRL     | USB mode control. Bit 0: Force USB3 mode       |

### PHY Control Registers

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x92C0  | USB_BULK_EP_CFG0  | Bulk endpoint config 0. Bit 0: Enable          |
| 0x92C1  | USB_PHY_CTRL1     | PHY control 1. Bit 0-1: Enable, Bit 4: PHY enable |
| 0x92C2  | USB_PHY_STATUS    | PHY status. Bit 6: Cable connected             |
| 0x92C4  | USB_SS_EP_CFG     | USB3 SuperSpeed endpoint config. Bit 0: Enable |
| 0x92C5  | USB_BULK_EP_CFG5  | Bulk endpoint config 5. Bit 2: Set for enable  |
| 0x92C6  | USB_EP_CFG_DATA   | Endpoint config data area (0x92C6-0x92C7)      |
| 0x92C8  | USB_PHY_CTRL2     | PHY control 2. Bits 0-1: PHY power control     |
|         |                   |   Bit 0: Clear for state '0'                   |
|         |                   |   Bit 1: Clear for state '1', Set|2 for link   |
| 0x92CF  | USB_PHY_LINK      | PHY link control. Bit 2: Link training enable  |
| 0x92E0  | USB_RESET_CTRL    | Reset control. Bit 1: Soft reset               |
| 0x92E1  | USB_PHY_CONNECT   | PHY connect control. Write 0x10 on connect     |
| 0x92F8  | USB_PHY_STAT2     | PHY status 2                                   |
|         |                   |   Bits 2-3: Link training status               |
|         |                   |   Bit 4: Link trained                          |
|         |                   |   Bit 5: Link status flag                      |
| 0x92FB  | USB_PHY_STAT3     | PHY status 3                                   |

### Endpoint Event Registers

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x9300  | USB_EP_EVENT1     | Endpoint event status 1. Bits 2-3: Events      |
| 0x9301  | USB_EP_EVENT2     | Endpoint event status 2                        |
|         |                   | Bit 6: IN token received                       |
|         |                   | Bit 7: OUT token received                      |
| 0x9302  | USB_EP_EVENT3     | Endpoint event status 3                        |
|         |                   | Bit 0: EP1 event                               |
|         |                   | Bit 1: EP2 event                               |
|         |                   | Bit 2: EP3 event                               |
|         |                   | Bit 3: Error event                             |
|         |                   | Bit 4: Stall event                             |
|         |                   | Bit 5: NAK event                               |

### USB Control Packet Buffer (0x9E00-0x9EFF)

This 256-byte region is used for USB control transfer data:

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0x9E00  | USB_CTRL_BUF      | Control transfer data buffer start             |
|         |                   | Used for GET_STATUS response (2 bytes)         |
|         |                   | Used for descriptor data (variable length)     |
| 0x9E02  | USB_DESC_TYPE     | Descriptor type byte (written by state machine)|
| 0x9E03  | USB_DESC_LEN      | Descriptor length byte                         |
| 0x9E07  | USB_DESC_DATA1    | Descriptor data field 1                        |
| 0x9E08  | USB_DESC_DATA2    | Descriptor data field 2                        |

**GET_STATUS Response Format (usb_build_status_response @ 0xb349):**
- Byte 0 (0x9E00): Status bits
  - Bit 0: Self-powered (device) or Halt (endpoint)
  - For endpoints: reads halt from 9006.7 (EP0), 905f.0 (IN), 905d.0 (OUT)
- Byte 1 (0x9E01): Reserved (0x00)

**GET_STATUS State Machine (usb_get_ep_state):**
| State | Behavior                                                            |
|-------|---------------------------------------------------------------------|
| 0     | Device status: Check EXTMEM_07b8 or speed-based self-powered bit    |
| 1     | Interface: Return EXTMEM_0acc * 2, or 5 if EXTMEM_0ad3 != 0         |
| 2     | Endpoint: Read halt from 905f.0 (type 4), 9006.0 (type 0x81),       |
|       | 905d.0 (type 0x83), 9006.7 (type 2)                                 |

**Endpoint Type Codes (EXTMEM_0ad3):**
| Type  | Endpoint    | Halt Register |
|-------|-------------|---------------|
| 0x04  | Bulk IN     | 905f.0        |
| 0x81  | EP0 IN      | 9006.0        |
| 0x83  | Bulk OUT    | 905d.0        |
| 0x02  | EP0 Status  | 9006.7        |

**Descriptor State Machine (usb_descriptor_state_machine @ 0x8756):**
- Writes descriptor data based on EXTMEM_0ad2 state
- High Speed: 9e07=0x80, 9e08=0x70
- Full Speed: 9e07=0x80, 9e08=0xFA

## Register Initialization Values

From `usb_full_register_init` (0xb1cb), these are the register values written during USB initialization:

| Register | Init Value | Purpose |
|----------|------------|---------|
| 0x92C0   | \|= 0x80   | Set bit 7: Power enable |
| 0x91D1   | 0x0F       | Clear link event interrupts (bits 0-3) |
| 0x9300   | 0x0C       | Clear EP event bits 2-3 |
| 0x9301   | 0xC0       | Clear IN/OUT token events (bits 6-7) |
| 0x9302   | 0xBF       | Clear EP status events (bits 0-5,7) |
| 0x9091   | 0x1F       | Clear USB3 SS events (bits 0-4) |
| 0x9093   | 0x0F       | Clear control EP events (bits 0-3) |
| 0x91C1   | 0xF0       | PHY config high nibble |
| 0x9303   | 0x33       | Endpoint config |
| 0x9304   | 0x3F       | Endpoint config |
| 0x9305   | 0x40       | Endpoint config |
| 0x9002   | 0xE0       | PHY mode (bits 5-7 set) |
| 0x9005   | 0xF0       | EP0 config high nibble |
| 0x90E2   | 0x01       | Clear bulk interrupt |
| 0x905E   | &= 0xFE    | Clear bit 0 |
| 0xc42c   | 0x01       | PHY transfer control clear |
| 0xc42d   | &= 0xFE    | Clear bit 0 |
| 0x91C3   | &= 0xDF    | Clear bit 5 |
| 0x91C0   | toggle 0   | PHY reset sequence (set then clear bit 0) |

**Full Init Sequence (usb_full_register_init):**
```
1. Set 92c0.7 (power enable)
2. Clear all interrupt status registers (91d1, 9300-9302, 9091, 9093)
3. Configure PHY (91c1, 9303-9305, 9002, 9005)
4. Clear bulk interrupt (90e2=1)
5. Clear misc registers (905e.0, c42c=1, c42d.0)
6. Call usb_mode_config(0) for USB3 mode
7. Clear 91c3.5, toggle 91c0.0 (PHY reset)
8. Call usb_clear_pending_state
9. Poll e318.4 or cc11.1 for PHY ready
10. Check 91c0 bits 3-4 for link status
```

**Note**: Writing the bit value to interrupt status registers clears that interrupt.

## Initialization Sequence

The USB peripheral is initialized during the boot sequence in `main?` (0x2f80):

### 1. Early Initialization (usb_init_sequence @ 0x4fb6)

```
1. usb_early_init() @ 0x5305 - Initial hardware setup
   - usb_clear_state() @ 0x4c40 - Clear USB state variables (EXTMEM_0ae2, EXTMEM_07e7)
   - Read MMIO2_e795 for hardware configuration
   - Set EXTMEM_07f6 = 1

2. thunk_usb_phy_analog_init (0x04b7) -> usb_phy_analog_init (0xe597)
   - Set c004.1, clear c007.3, set ca2e.0

3. thunk_usb_spiflash_config (0x04bc) -> usb_spiflash_config (0xe14b)
   - Initialize SPI flash access, configure SPIFLASH_7000

4. usb_hardware_init() @ 0x4be6 - USB hardware initialization
   - Set descriptor pointers at EXTMEM_07f0-07f5:
     - 0x7f0/0x7f1 = 0x0424 (CODE descriptor table 1)
     - 0x7f2/0x7f3 = 0x8517 (Bank 1 descriptor table)
     - 0x7f4/0x7f5 = 0x0000 (NULL)
   - Configure cc35 bit 0 = 0
   - Configure c801 bit 4 = 1
   - Configure c800 bit 2 = 1
   - Configure ca60 bits 0-2 = 6, bit 3 = 1
   - Initialize interrupt controller (cc3b bit 1 = 1)
   - Call usb_bulk_endpoint_config (0xcb37)

5. FUN_CODE_032c (0x032c) - Register setup via 0x92c5

6. thunk_usb_load_spiflash_config (0x0539) -> usb_load_spiflash_config (0x8d77)
   - Load USB configuration from SPI flash (0x7000 area)
   - Set EXTMEM_09f4-09f8 config values
   - Initialize EXTMEM_USB_CONN_STATUS based on config

7. thunk_usb_timer_regs_init (0x04f8) -> usb_timer_regs_init (0xde16)
   - Initialize timer/control registers (cd30, cc2a)
   - Clear EXTMEM_0b30-0b33

8. FUN_CODE_063d (0x063d) - Bank 1 call to 0xeef9

9. If EXTMEM 0ae3 (USB_PHY_CONFIG) != 0: Clear cc32 bit 0

10. Wait loop: Poll c6b3 bits 4-5 until non-zero (PHY ready)

11. thunk_usb_final_init (0x0462) -> usb_final_init (0xcd6c)
    - Clear ca06.4, call c8db

12. Set EXTMEM_06e6 = 1 (USB_INIT_DONE flag)

13. thunk_usb_pcie_queue_init (0x0435) -> usb_pcie_queue_init (0xd127)
    - Initialize USB/PCIe queue (b264-b26f, cef2, cef3, c807, b281)

14. thunk_usb_state_reset_init (0x0340) -> usb_state_reset (0xbf8e)
    - Reset USB state machine, clear state variables
    - Initialize endpoints via usb_endpoint_init (0x4904)
```

### 2. Interrupt Enable

After initialization, interrupts are enabled:
```c
PX0 = 0;    // INT0 priority low
PX1 = 0;    // INT1 priority low
EX0 = 1;    // Enable INT0 (USB interrupt)
EX1 = 1;    // Enable INT1
EA = 1;     // Global interrupt enable
```

### 3. USB Mode Configuration (usb_mode_config @ 0xd07f)

Configures USB2 vs USB3 mode based on parameter:

| Mode | R7 Param | USB_MMIO_9018 | USB_MMIO_9010 | IDATA 0x3e |
|------|----------|---------------|---------------|------------|
| USB3 | 0        | 0x03          | 0xFE          | 0xFF       |
| USB2 | != 0     | 0x02          | 0x00          | 0x00       |

Common initialization for both modes:
- Writes 0xFF to c430, c440, 9096, 9097
- Writes 3 to 9098
- Clears 9011

### 4. USB PHY Initialization (usb_phy_init @ 0xbf0f)

PHY hardware initialization sequence:
```
1. Call usb_mode_config(1) for initial USB2 mode setup
2. Configure MMIO_c428 analog settings (clear bit 2, set bit 5)
3. Configure MMIO_c42a PHY control (clear bits 1,2,3)
4. Set MMIO_c471 = 1 (bulk processing enable)
5. Clear MMIO_c472 bit 0
6. Toggle MMIO_c42a bit 4
7. If param==0 (USB3):
   - Clear USB_MMIO_900b bits 1,2 (PHY mode)
   - Clear USB_MMIO_9000 bit 0 (disable USB3 initially)
   - Clear USB_MMIO_924c bit 0 (EP config)
```

## Interrupt Handling

### INT0 Handler (int0 @ 0x0e5b)

The main USB interrupt handler dispatches based on `USB_INT_STATUS` (0x9101):

```
Check MMIO_c802 bit 0:
  If set, process USB interrupts

USB_INT_STATUS (0x9101) dispatch:
  Bit 5 (USB3 mode):
    If USB_MODE bit 0 = 1 (USB3):
      Process USB3 queue (0x9118-based loop)
      Handle USB3 bulk transfers
    Else (USB2):
      Check USB_RESET_INT (0x9096) for reset

  Bit 3 (Link management):
    Check USB_EP_EVENT2 (0x9301):
      Bit 6: IN token handler (FUN_CODE_035e)
      Bit 7: OUT token handler (FUN_CODE_0363 -> usb_state_reset)

  Bit 0 (Link events):
    Check USB_LINK_EVENT (0x91D1):
      Bit 3: usb3_link_handler (0x0345)
      Bit 0: usb3_link_status_handler (0x034a)
      Bit 1: FUN_CODE_034f
      Bit 2: FUN_CODE_0354

  Bit 1: FUN_CODE_033b

  Bit 2 (Control endpoint):
    Check USB_CTRL_EP_INT (0x9093):
      Bit 1: usb_control_transfer_handler (0x32a5)
      Bit 3: usb_ctrl_ep_event (0x4d44)
      Bit 0,2: FUN_CODE_5455

  Bit 6 (Bulk transfer):
    Check USB_BULK_INT (0x90E2) bit 0:
      If USB_MODE = USB2: usb_bus_reset_handler
      If USB_MODE = USB3: usb_handle_command

  Bit 4 (Endpoint events):
    Check USB_EP_EVENT3 (0x9302) bits
```

## USB Enumeration Flow

### Standard Request Handling

**SET_CONFIGURATION (usb_handle_set_configuration @ 0x3326):**
1. Check USB mode (9000.0): USB3 vs USB2
2. Speed validation via `usb_get_speed()`:
   - USB3 SuperSpeed: wLength < 2 check
   - USB2 High-Speed: wLength < 4 check
3. Read wValue from USB_MMIO_910d/910e → store to EXTMEM_0a81/0a82
4. If XRAM3_d80c == 1: Call `usb_enter_configured_state()`
5. Else: `usb_set_request_mode()`, setup transfer params, complete

**SET_ADDRESS:**
The address counter at EXTMEM_000a is incremented in `usb_handle_command` (0x180d).
Address takes effect after status stage completion.

### USB2 (High-Speed) Enumeration

1. Cable connect detected via USB_PHY_STATUS (0x92C2) bit 6
2. USB reset detected via USB_RESET_INT (0x9096)
3. `usb_reset_complete_handler` called
4. State machine (DAT_INTMEM_6a) transitions:
   - State 0: Disconnected
   - State 1: Reset received
   - State 2: Default state (address 0)
   - State 3-4: Address assigned
   - State 5: Configured

5. Control transfers handled by `usb_control_transfer_handler`

### USB State Machine (USB_STATE_ at IDATA 0x6a)

| State | Name | Description |
|-------|------|-------------|
| 0 | Disconnected/Reset | Initial state, awaiting bus reset |
| 1 | Default | Device reset complete, using default address |
| 2 | Default (check) | Used for control transfer validation |
| 3 | Addressed (USB2) | After SET_ADDRESS, no streaming |
| 4 | Addressed (USB3) | After SET_ADDRESS, with streaming |
| 5 | Configured | After SET_CONFIGURATION |
| 6 | Special | Used in command processing |
| 8 | Suspended | USB suspend state |
| 0x0b, 0x0c | Transfer | Active transfer states |

**State Transitions in usb_bus_reset_handler (0x3419):**
- Reset detected → USB_STATE_ = 0
- After init: USB_STATE_ = 3 (ce89.2=0, USB2 mode) or 4 (ce89.2=1, USB3/HS mode)
- SET_CONFIGURATION → USB_STATE_ = 5

**usb_bus_reset_handler Flow (0x3419):**
```
1. If EXTMEM_USB_POWER_FLAG != 0: call usb_conditional_disable
2. If USB_STATE_ != 0: return (not in reset state)
3. Clear transfer state variables:
   - EXTMEM_USB_QUEUE_READY = 0
   - EXTMEM_053b, 00c2, 0517, 014e, 00e5 = 0
4. MMIO_ce88 = 0, poll ce89.0 until set
5. Check ce89.1, ce86.4 for error conditions
6. If EXTMEM_USB3_MODE_FLAG == 1 (USB3):
   - usb_lun_config()
   - Read MMIO_ce55 → EXTMEM_009f (queue count)
   - Initialize queue pointers
   - usb_read_cbw_data()
   - USB_STATE_ = 3 (ce89.2=0) or 4 (ce89.2=1)
7. Else (USB2):
   - usb_check_cbw_signature()
   - usb_parse_full_cbw()
```

### USB State Dispatch Table (0x396e-0x39dc)

The USB state machine uses a dispatch table at 0x396e with handler functions called via `usb_state_call_*`:

| Address | Function Name | Description |
|---------|---------------|-------------|
| 0x396e | usb_state_call_set_memory | Set memory helper |
| 0x3973 | usb_state_call_link_state_machine | Link state machine handler |
| 0x3978 | usb_state_call_command_dispatch_wait | Command dispatch with wait |
| 0x397d | usb_state_call_config_e422 | Configure e422 register |
| 0x3982 | usb_state_call_copy_setup2 | Copy setup packet (variant 2) |
| 0x3987 | usb_state_call_cmd_ctrl_xfer | Command control transfer |
| 0x398c | usb_state_call_status_write | Write USB status, wait b296 |
| 0x3991 | usb_state_call_copy_setup3 | Copy setup packet (variant 3) |
| 0x3996 | usb_state_call_command_state | Command state handler |
| 0x399b | usb_state_call_pci_lane_config | PCI lane B436 configuration |
| 0x39a0 | usb_state_call_pcie_link_init | PCIe link init sequence |
| 0x39a5 | usb_state_call_copy_setup1 | Copy setup packet (variant 1) |
| 0x39aa | usb_state_call_setup_hs_transfer | Setup HS transfer |
| 0x39af | usb_state_call_conn_state_handler | Connection state handler |
| 0x39b4 | usb_state_call_noop | No-op (unused state) |
| 0x39b9 | usb_state_call_pcie_b220_setup | PCIe B220 transfer setup |
| 0x39be | usb_state_call_transfer_cmd | Transfer command handler |
| 0x39c3 | usb_state_call_data_manipulation | Data manipulation (0a5c/0a5b) |
| 0x39c8 | usb_state_call_cmd_50_51 | Command 0x50/0x51 handler |
| 0x39cd | usb_state_call_pcie_init_6_slots | PCIe init 6 slots |
| 0x39d2 | usb_state_call_usb4_vdm_config | USB4 VDM e420 configuration |
| 0x39d7 | usb_state_call_b210_transfer | B210 transfer loop |
| 0x39dc | usb_state_call_xfer_params_ctrl | Transfer params ctrl wrapper |

Each handler calls a Bank 1 thunk/trampoline function, then updates `USB_STATE_` with the return value in `BANK0_R7`.

### USB3 (SuperSpeed) Enumeration

1. Link training initiated via USB_PHY_LINK (0x92CF)
2. Link training complete when USB_PHY_STAT2 (0x92F8) bits 2-3 set
3. `usb3_link_handler` processes link events
4. USB3 queue-based transfers begin

## Control Transfers

Control transfers are handled in `usb_control_transfer_handler` (0x32a5):

### Setup Stage
1. Hardware receives SETUP packet, triggers USB_CTRL_EP_INT bit 1
2. Setup packet data is stored in IDATA 0x6b-0x6e (4 bytes)
3. `usb_read_idata_4bytes(0x6b)` copies to R4-R7
4. bmRequestType → EXTMEM 0x02, bRequest → EXTMEM 0x01
5. wLength read from USB_MMIO_910d/910e

### Data Stage
1. Response data built in USB control buffer (0x9E00-0x9EFF)
2. `usb_build_status_response` (0xb349) handles GET_STATUS
3. `usb_ctrl_response_build` (0xd278) handles other responses
4. Transfer length set in USB_MMIO_9003/9004 (low/high bytes)
5. Transfer address set in USB_MMIO_9007/9008

### Status Stage
1. `usb_ctrl_ep_ack_data` (0x3267): Write 9093=2, 9094=0x10
2. `usb_ctrl_ep_ack_error` (0x1cfc): Write 9093=8, 9094=2
3. Status complete triggers USB_CTRL_EP_INT bit 2

### Request Dispatch
- USB_STATE_ must be 2 (default state) or handler stalls
- Dispatch based on bmRequestType:
  - 0xE3 (-0x1d): Vendor IN type 1 → `usb_read_idata_4bytes`, vendor handler
  - 0xF9 (-7): Vendor type 2 → same flow
  - 0xFB (-5): Vendor type 3 → same flow
  - 0xE1 (-0x1f): Vendor OUT type 1 → `usb_set_request_mode`, `trampoline_vendor_request`
  - Other: Stall endpoint via `usb_stall_endpoint` (0x33ff)

### Stall/Error Handling
- `usb_stall_endpoint` (0x33ff): Sets stall condition on EP0
- Stall status readable from USB_MMIO_9006 bit 7 (EP0)
- Bulk EP stall: USB_MMIO_905f bit 0 (IN), USB_MMIO_905d bit 0 (OUT)

## Bulk Transfers

### Bulk IN (Device to Host)

Handled by `usb_bulk_in_handler` (0x3e81). Called from INT0 when c520.0 is set (USB3 mode):

```
1. Read queue index from c516 (bits 0-5) → slot stored in IDATA 0x38
2. Compute EXTMEM address 0x17+slot, increment counter
3. Check EXTMEM 0x9f+slot matches counter
4. Check EXTMEM 0xc2+slot matches counter
5. If match: set transfer params (3, 0x47, 0xb)
6. Handle cef3.3 or (5ac!=0 && cef2.7) → write cef3=8, call transfer_queue_handler
7. Configure c508 with slot, call 53a7 for setup
8. Write 0xFF to 0x171+slot (clear entry)
9. Read c512 → EXTMEM 578, call 45d0 with slot
10. Clear c512=0xFF (ack interrupt)
11. Loop while c520.0 still set
```

### Bulk OUT (Host to Device)

Handled by `usb_bulk_out_handler` (0x488f). Called from INT0 when c520.1 is set:

```
1. Set EXTMEM 6e6=1 (USB_INIT_DONE)
2. Loop counter = 0, max 32 iterations
3. While c520.1 set:
   a. Clear EXTMEM 464, 465
   b. USB3 mode: Read c51a, call 4b25, read slot from c51e (bits 0-5)
   c. USB2 mode: Read slot from c51e (bits 0-5), call 3da1
   d. Set IDATA 0x3a = 0x75
   e. If 0x171+slot != 0: set 0x3a |= 0x80
   f. Write 0x3a to EXTMEM 0x08+slot
   g. Increment counter at EXTMEM 0x17+slot
   h. Clear c51a=0xFF (ack interrupt)
```

### Core Data Transfer (usb_transfer_data @ 0xba06)

The core USB data transfer mechanism implements DMA-based transfers:

```
1. Read chunk size from EXTMEM 0xad8-0xad9 (XFER_SIZE)
2. Read remaining bytes from EXTMEM 0xade-0xadf (REMAIN)
3. Write min(chunk, remaining) to USB_MMIO_9003/9004 (transfer length)
4. Write 4 to USB_MMIO_9092 (trigger DMA transfer)
5. Poll USB_MMIO_9092 bit 2 until clear (DMA complete)
6. Read actual transferred length back from 9003/9004
7. Call FUN_CODE_0c64 to update EXTMEM 0xada-0xadb (transfer address)
8. Subtract transferred bytes from remaining (0xade-0xadf)
9. If remaining==0: set EXTMEM_07e4=4 (USB_STATE_INIT), return 4
10. Otherwise: Copy next chunk data via FUN_CODE_a647 loop
```

**DMA Transfer Registers:**
| Register | Purpose |
|----------|---------|
| 0x9003   | Transfer length low byte (read/write) |
| 0x9004   | Transfer length high byte (read/write) |
| 0x9092   | Transfer control: write 4 to trigger, poll bit 2 for completion |

**Transfer Completion:**
- Returns 4 when transfer complete (remaining==0)
- Sets EXTMEM_USB_STATE_INIT (0x07e4) = 4 on completion
- Data copied to/from 0x9E00 buffer area

### Bulk Transfer EXTMEM Variables

| Address     | Name              | Description                              |
|-------------|-------------------|------------------------------------------|
| 0x0ad8-0xad9| XFER_SIZE         | Transfer chunk size (low/high)           |
| 0x0ada-0xadb| XFER_ADDR         | Current transfer address (low/high)      |
| 0x0ade-0xadf| REMAIN            | Remaining bytes (low/high)               |
| 0x0017+slot | SLOT_COUNTER      | Per-slot transfer counter                |
| 0x0171+slot | SLOT_STATUS       | Per-slot status (0xFF = clear)           |

### Transfer Queue Handler (usb_transfer_queue_handler @ 0x2608)

Processes USB transfer queue entries and manages state transitions:

```
1. Initialize via FUN_CODE_1687, read queue status
2. Loop through transfer queue (up to 0x1f entries):
   a. Check EXTMEM_0464 for queue offset (0 or 0x20)
   b. Write c8d5/c8d6 for transfer control
   c. Read XRAM_b80c-b80f for transfer info
   d. Process based on transfer type flags (DAT_INTMEM_54):
      - Bit 4: Transfer pending flag
      - Bit 6: Bulk transfer indicator
      - Bit 2: Completion flag
   e. For bulk: set c508 slot, call 53a7
   f. For control: write USB_MMIO_90a1=1 (trigger status)
3. On completion: set USB_STATE_ = 5 (configured)
4. Clear slot entries (0x171+slot = 0xFF)
```

**Queue State Transitions:**
| From State | To State | Condition |
|------------|----------|-----------|
| 4          | 5        | Transfer queue processing complete |

**Queue Registers:**
| Address | Name | Description |
|---------|------|-------------|
| 0xc8d5  | QUEUE_CTRL1 | Queue control byte 1 |
| 0xc8d6  | QUEUE_CTRL2 | Queue control byte 2 (bit 0 cleared) |
| 0xc508  | SLOT_CONFIG | Slot config (bits 0-5 = slot, bits 6-7 preserved) |

### SCSI Command Processing

The `usb_process_request` function (0x3cb8) processes SCSI commands from the CBW:

| Opcode | Command          | R7 Value | Description                    |
|--------|------------------|----------|--------------------------------|
| 0x00   | TEST_UNIT_READY  | N/A      | Check device ready             |
| 0x28   | READ_10          | 3        | Read with 32-bit LBA           |
| 0x2A   | WRITE_10         | 1        | Write with 32-bit LBA          |
| 0x88   | READ_16          | 2        | Read with 64-bit LBA           |
| 0x8A   | WRITE_16         | 0        | Write with 64-bit LBA          |

The command opcode is read from EXTMEM 0x02 (copied from CBW CDB field).

## Key Functions

### Interrupt Handling
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0x0e5b  | int0                        | Main USB interrupt handler           |

### Initialization
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0x4fb6  | usb_init_sequence           | Main USB initialization              |
| 0x5305  | usb_early_init              | Early USB initialization             |
| 0x4be6  | usb_hardware_init           | Initialize USB hardware              |
| 0x4c40  | usb_clear_state             | Clear USB state variables            |
| 0xbf8e  | usb_state_reset             | Reset USB state machine              |

### Control Endpoint
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0x32a5  | usb_control_transfer_handler| Control transfer processing          |
| 0x3326  | usb_handle_set_configuration| Handle SET_CONFIGURATION request     |
| 0x33e8  | usb_enter_configured_state  | Enter configured state (USB2/USB3)   |
| 0x3cb8  | usb_process_request         | Process USB request                  |
| 0x33ff  | usb_stall_endpoint          | Stall the endpoint                   |
| 0x4ddc  | usb_wait_for_ctrl_transfer  | Wait for control transfer completion |
| 0x4d44  | usb_ctrl_ep_event           | Control endpoint event handler       |
| 0x1cfc  | usb_ctrl_ep_ack_error       | Ack control EP error (9093=8,9094=2) |
| 0x1d07  | usb_ctrl_ep_ack_data2       | Ack control EP data (9093=2,9094=16) |
| 0x3267  | usb_ctrl_ep_ack_data        | Ack control EP data (9093=2,9094=16) |

### Bulk Endpoints
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0x3e81  | usb_bulk_in_handler         | Bulk IN transfer handler             |
| 0x488f  | usb_bulk_out_handler        | Bulk OUT transfer handler            |
| 0x4784  | usb_ep_state_handler        | Endpoint state machine handler       |
| 0xa6ad  | usb_bulk_ep_clear_ready     | Clear bulk EP ready bits (905d,905f) |

### Endpoint Control
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0x312a  | usb_set_endpoint_ready      | Set endpoint ready (9006 bit 0)      |
| 0x3130  | usb_ep0_set_ready           | Set EP0 ready (9006 bit 0)           |
| 0x3219  | usb2_set_configured         | Set USB2 configured (90a1=1)         |
| 0x01ea  | usb2_trigger_status         | Trigger USB2 status stage            |
| 0xa940  | usb_endpoint_control        | Endpoint control state machine       |
| 0x9d91  | usb3_endpoint_config        | USB3 endpoint configuration          |

### Link/PHY Management
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0x180d  | usb_handle_command          | USB command/request handler          |
| 0x3419  | usb_bus_reset_handler       | USB bus reset handler                |
| 0x52a7  | usb_reset_complete_handler  | Reset completion handler             |
| 0x5409  | usb_disconnect_handler      | USB disconnect handler               |
| 0x9c2b  | usb3_link_handler           | USB3 link state handler              |
| 0xb571  | usb_reset_handler           | USB reset handler (9090,9000,9200)   |
| 0xc66a  | usb3_link_status_handler    | USB3 link status handler             |
| 0xca0d  | usb_connection_state_handler| Connection state machine             |
| 0xced1  | usb_check_link_status       | Check USB link status registers      |
| 0xd78a  | usb3_link_state_update      | Update USB3 link state (9090,92e1)   |
| 0xd916  | usb_link_reset_sequence     | USB link reset sequence              |
| 0xd9d5  | usb_state_machine_handler   | Handle USB state machine             |
| 0xdf15  | usb_state_transition        | Handle USB state transitions         |
| 0xdfab  | usb_speed_check_handler     | Check USB speed and handle transitions|

### PHY Control
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0xbf0f  | usb_phy_init                | Initialize USB PHY                   |
| 0xcad6  | usb_phy_set_connected       | Set PHY connected (92c2.6, 92e1=16)  |
| 0xcadf  | usb_set_92e1_0x10           | Set USB_MMIO_92e1 to 0x10            |
| 0xcb23  | usb_phy_enable_termination  | Enable PHY termination (92c2.6)      |
| 0xd07f  | usb_mode_config             | Configure USB mode (9010,9018)       |

### Status/Speed
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0x328a  | usb_get_speed               | Get USB speed from 9100 (bits 0-1)   |
| 0xa666  | usb_get_speed_bank1         | Get USB speed (bank 1 version)       |
| 0x3181  | usb_get_setup_wLength       | Read wLength from setup packet       |
| 0xa679  | usb_disable_endpoint        | Disable endpoint (clr 9000.0, read 924c) |
| 0xa71b  | usb_clear_9003              | Clear USB_MMIO_9003                  |
| 0xa732  | usb_get_9090_status         | Read USB_MMIO_9090 & 0x7F            |
| 0xcae6  | usb_clear_9090_toggle_9000  | Clear 9090.7, toggle 9000.2          |
| 0xda8f  | usb_multi_status_check      | Check 91d1, 9301, 9091, 9105         |
| 0xe239  | usb_check_9090_write_ctrl   | Check 9090.7, write 9e00             |

### Data Transfer
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0xba06  | usb_transfer_data           | USB data transfer with 9003/9004     |
| 0xd3ed  | usb_parse_incoming_packet   | Parse incoming USB packet at 0x8000  |

### USB3 SuperSpeed Event Handling
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0xcf7f  | usb3_event_dispatcher       | Dispatch USB3 events from 9091 bits  |
| 0xa740  | usb3_bit0_handler           | Handle 9091 bit0 (connect/disconnect)|
| 0xd21e  | usb3_bit1_handler           | Handle 9091 bit1 event               |
| 0xdeb1  | usb3_bit2_handler           | Handle 9091 bit2 event               |
| 0xb28c  | usb3_bit3_handler           | Handle 9091 bit3 event               |
| 0x9d91  | usb3_endpoint_setup         | Handle 9091 bit4 (endpoint setup)    |
| 0xa687  | usb3_enable_bit1            | Enable USB3 bit1 (9002, 9092=2)      |
| 0xa692  | usb3_write_9092             | Write param to USB_MMIO_9092         |
| 0xa714  | usb3_set_9092_bit0          | Set USB_MMIO_9092 = 1                |

**USB3 Event Dispatch Flow (usb3_event_dispatcher @ 0xcf7f):**
```
1. If 9091.0 set AND 9091.2 not set: call usb3_bit0_handler (connect)
2. If 9002.1 not set AND 9091.1 set: call usb3_bit1_handler, write 9091=2
3. If 9091.2 set: call usb3_bit2_handler, write 9091=4
4. If 9091.3 set: call usb3_bit3_handler, write 9091=8
5. If 9091.4 set: call usb3_endpoint_setup, write 9091=0x10
```

**USB3 Event Handler Details:**

| Handler | Event | Description |
|---------|-------|-------------|
| usb3_bit0_handler (0xa740) | Connect/Disconnect | Clears PHY mode (9002.1), disables SS EP (92c4.0), power sequence (cc17=4,2), clears 9220.2, loops writing 9091=1 until bit cleared |
| usb3_bit1_handler (0xd21e) | Transfer Complete | If EXTMEM_07e4==5 (configured): trigger via usb3_set_9092_bit0, else dispatch return |
| usb3_bit2_handler (0xdeb1) | Error | Simply calls usb_dispatch_return - error recovery delegated |
| usb3_bit3_handler (0xb28c) | Suspend/Resume | Same as bit1: if configured trigger transfer, else dispatch return |
| usb3_endpoint_setup (0xb6cf) | EP Setup | Configures 9206/9207 (bits 0-1=3, 2-3=1), sets link training bits based on 92f8.4, configures 9208-920b based on 92f8 bits 2-3, writes 9092=8 |

**usb3_bit0_handler State Updates:**
```
EXTMEM_07e4 = 0      (USB_STATE_INIT)
EXTMEM_07ec = 1      (USB_STATE_FLAG6)
EXTMEM_07ee = 0      (USB_LINK_TRAIN_FLAG)
EXTMEM_0ad7 = 0
USB_MMIO_9002 &= ~2  (clear PHY SS mode)
USB_MMIO_92c4 &= ~1  (disable SS endpoint)
MMIO_cc17 = 4, then 2 (power sequence)
```

**usb3_endpoint_setup Register Config (when EXTMEM_07e4==4 AND 07ee!=0):**
```
USB_MMIO_9206/9207: bits 0-1 = 3, bits 2-3 = 1
If 92f8.4 set: 9206/9207 |= 0x10
If 92f8 bits 2-3 == 0:
  9208=0, 9209=5, 920a=0, 920b=5
Else:
  9208=0, 9209=10, 920a=0, 920b=10
Clear 9202.1, set 9220.2
Write 9092=8 (trigger EP setup)
Set EXTMEM_07e4=1, call usb_idle_state_handler
```

**USB3 Link Status Check (usb_check_link_status @ 0xced1):**

| Return | Condition                                           |
|--------|-----------------------------------------------------|
| 0      | e716 bits 0-1 == 0 (external link down)             |
| 0      | 92c2.6 set AND 91c0.1 set (cable+link OK)           |
| 1      | 9100 bits 0-1 != 2 (not High Speed)                 |
| 2      | 92f8.5 not set (training not active)                |
| 3      | 92f8.5 set but 92f8.2-3==0 OR 92f8.4==0             |
| 4      | 92f8.5 set AND 92f8.2-3!=0 AND 92f8.4 set (trained) |

### Bulk Endpoint Configuration
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0xcb37  | usb_bulk_endpoint_config    | Configure bulk endpoints (9201,92c0-c5,920c,9241) |
| 0xd47f  | usb_power_setup             | USB power management (92c4,9201)     |

### Endpoint Initialization
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0x4904  | usb_endpoint_init           | Initialize endpoints (900b,901a)     |
| 0x4e25  | usb_endpoint_reset          | Reset endpoints (900b,920f)          |
| 0x8925  | usb_speed_init_handler      | Speed-dependent init (9e01,9e02)     |

### Control Transfer Response
| Address | Name                        | Description                          |
|---------|-----------------------------|--------------------------------------|
| 0xd278  | usb_ctrl_response_build     | Build control transfer response      |
| 0xe03c  | usb_ctrl_check_state        | Check control state (9003,9004,9e00) |
| 0xe5e5  | usb_ctrl_set_speed_status   | Set speed status in 9e00             |

## State Machine

The USB state is tracked in `DAT_INTMEM_6a`:

| State | Description                                    |
|-------|------------------------------------------------|
| 0     | Disconnected / Idle                            |
| 1     | Reset received, waiting for address            |
| 2     | Default state (control transfers enabled)      |
| 3     | USB2 mode, address assigned                    |
| 4     | USB2 mode, configured                          |
| 5     | USB3 mode, active transfers                    |
| 6     | Special state (error recovery?)                |
| 8     | USB3 link training                             |
| 11    | Suspended                                      |
| 12    | Resume pending                                 |

### State Machine Handler (usb_state_machine_handler @ 0xd9d5)

The `usb_state_machine_handler` manages endpoint state based on EXTMEM_0ad1:

| EXTMEM_0ad1 | Action |
|-------------|--------|
| 1           | Write CSW signature "USBS" (0x55534253) to XRAM3_d800 |
|             | Write 0x0D to USB_MMIO_901a (endpoint init command) |
|             | Call usb_clear_bulk_int, usb_bulk_ep_clear_ready |
|             | Set 9090.7 (busy flag) |
| 0           | Get 9090 status |
| other       | Return immediately |

After state handling, calls `usb_disable_endpoint` to finalize.

### USB3 Endpoint Configuration State Machine (EXTMEM_0ad1)

The USB3 endpoint configuration is controlled by `usb3_endpoint_config` (0x9d91) based on
the state variable at EXTMEM 0x0ad1:

| State | Description                                                           |
|-------|-----------------------------------------------------------------------|
| 0     | Check ep_state, configure 905f/9006/905d based on endpoint type       |
| 2     | Configure USB_MMIO_91c6 based on EXTMEM_0ad4 (1,2,4,8 commands)       |
| '0'   | (0x30) Check 9090.7 busy flag, access 92c8                            |
| '1'   | (0x31) Set 92c8.1|2, check 9000.0/9101.6/c471.0                       |
| '2'   | (0x32) Check 9090.7 busy flag                                         |

**USB_MMIO_91c6 EP Config Commands (when state=2):**
| EXTMEM_0ad4 | 91c6 Value | Description                                     |
|-------------|------------|-------------------------------------------------|
| 1           | 0x01       | Config type 1                                   |
| 2           | 0x02       | Config type 2                                   |
| 3           | 0x04       | Config type 3 (also sets MMIO2_e742.3)          |
| 4           | 0x08       | Config type 4 - Writes 0x37 bytes from CODE:5ced to 91c4/91c5 |

### Bulk Endpoint Configuration Sequence

The `usb_bulk_endpoint_config` (0xcb37) initializes bulk endpoints:

```
1. USB_MMIO_92c6 = 5, USB_MMIO_92c7 = 0      (endpoint counts)
2. USB_MMIO_9201 &= ~0x03                     (disable bulk EPs)
3. USB_MMIO_92c1 |= 0x02                      (PHY control bit 1)
4. USB_MMIO_920c &= ~0x03                     (clear bulk EP control)
5. MMIO_c20c |= 0x40                          (DMA control)
6. MMIO_c208 &= ~0x10                         (DMA control)
7. USB_MMIO_92c0 |= 0x01                      (bulk config enable)
8. USB_MMIO_92c1 |= 0x01                      (PHY bulk enable)
9. USB_MMIO_92c5 |= 0x04                      (flow control enable)
10. USB_MMIO_9241 |= 0x10                     (buffer enable)
11. USB_MMIO_9241 |= 0xC0                     (buffer mode = bulk)
```

### USB State Reset Sequence (usb_state_reset @ 0xbf8e)

When USB state is reset, the following variables are cleared:

| Address     | Name                | Cleared To |
|-------------|---------------------|------------|
| 0x0003      | UNK_EXTMEM_0003     | 0          |
| 0x0006      | EXTMEM_0006         | 0          |
| 0x07e4      | USB_STATE_INIT      | 5          |
| 0x07e5      | USB_STATE_FLAG2     | 0          |
| 0x07e6      | USB_STATE_FLAG3     | 0          |
| 0x07e8      | USB_CBW_READY       | 0          |
| 0x07e9      | USB_STATE_FLAG4     | 0          |
| 0x07ea      | USB_HS_MODE         | 0          |
| 0x07eb      | USB_STATE_FLAG5     | 0          |
| 0x07ec      | USB_STATE_FLAG6     | 0          |
| 0x07ed      | USB_STATE_FLAG7     | 0          |
| 0x07ee      | USB_LINK_TRAIN_FLAG | 0          |
| 0x0acb      | USB_EP_STATE        | 0          |
| 0x0acc      | USB_EP_PENDING      | 0          |
| 0x0af2      | USB_EP_READY_FLAG   | 0          |
| 0x0af6      | USB_RESET_STATE     | 0          |
| 0x0b2e      | USB_ACTIVE          | 0          |
| 0x0b2f      | USB_CMD_FLAG        | 0          |
| 0x0b3c      | EXTMEM_0b3c         | 0          |
| 0x0b3d      | EXTMEM_0b3d         | 0          |
| 0x0b3e      | EXTMEM_0b3e         | 0          |

After clearing, the reset sequence:
1. Calls FUN_545c
2. Sets c6a8 bit 0
3. Clears USB_MMIO_92c8 bits 0-1 (PHY link control)
4. Writes 4 then 2 to MMIO_cd31
5. Calls usb_power_setup, FUN_d559, FUN_e19e
6. Calls usb_init_cleanup
7. Jumps to usb_endpoint_init

## Key Global Variables

| Address       | Name          | Description                          |
|---------------|---------------|--------------------------------------|
| INTMEM 0x6a   | usb_state     | USB state machine state              |
| EXTMEM 0x000a | usb_addr_cnt  | USB address counter                  |
| EXTMEM 0x0052 | usb_flags     | USB operation flags                  |
| EXTMEM 0x06e6 | usb_init_done | USB initialization complete flag     |
| EXTMEM 0x09f9 | conn_status   | Connection status (bit 6=connected)  |
| EXTMEM 0x0acd | link_state1   | Link state variable 1                |
| EXTMEM 0x0ace | link_state2   | Link state variable 2                |
| EXTMEM 0x0ad1 | link_phase    | Link training phase                  |
| EXTMEM 0x0ad2 | link_status   | Link status                          |
| EXTMEM 0x0ad3 | reset_counter | Reset counter                        |
| EXTMEM 0x0ae2 | usb_conn_stat | USB connection status                |
| EXTMEM 0x0af1 | link_flags    | Link flags (bit 1 checked)           |
| EXTMEM 0x0af2 | ep_ready_flag | Endpoint ready flag                  |
| EXTMEM 0x0af6 | reset_state   | Reset state (0/1)                    |
| EXTMEM 0x0af8 | usb3_mode     | USB3 mode flag                       |
| EXTMEM 0x0b2e | usb_active    | USB active flag                      |
| EXTMEM 0x0b2f | usb_busy      | USB busy flag                        |

## Detailed Register Bit Definitions

This section provides detailed bit-level definitions based on firmware analysis.

### USB_MMIO_9000 - Mode/Status Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | USB3_MODE     | 1=USB3 SuperSpeed active, 0=USB2 High-Speed    |
| 2   | TOGGLE        | Toggle bit for reset sequences                 |
| 7   | (reserved)    | Read-only, preserved in RMW operations         |

### USB_MMIO_9090 - EP0 Status Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0-6 | LINK_STATE    | Link state value (written during state update) |
| 7   | BUSY          | EP0 busy/reset pending flag                    |

### USB_MMIO_9091 - SuperSpeed Event Status (Write-to-Clear)
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | CONNECT       | Connect/disconnect event                       |
| 1   | XFER_DONE     | Transfer complete event                        |
| 2   | ERROR         | Error event                                    |
| 3   | SUSPEND       | Suspend/resume event                           |
| 4   | EP_SETUP      | Endpoint setup event                           |

### USB_MMIO_9093 - Control EP Interrupt Status (Write-to-Clear)
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | SETUP_DONE    | Setup stage complete                           |
| 1   | DATA_DONE     | Data stage complete (write 2 to ack)           |
| 2   | STATUS_DONE   | Status stage complete                          |
| 3   | CTRL_ERROR    | Control transfer error (write 8 to ack)        |

### USB_MMIO_9100 - Link Speed Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0-1 | SPEED         | Negotiated speed:                              |
|     |               |   0 = Disconnected                             |
|     |               |   1 = Full Speed (12 Mbps)                     |
|     |               |   2 = High Speed (480 Mbps)                    |
|     |               |   3 = SuperSpeed (5 Gbps)                      |

### USB_MMIO_9101 - Main Interrupt Dispatch Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | LINK_EVENT    | Link event pending (check 0x91D1)              |
| 1   | SS_EVENT      | SuperSpeed event pending                       |
| 2   | CTRL_EP       | Control endpoint event (check 0x9093)          |
| 3   | LINK_MGMT     | Link management event (check 0x9301)           |
| 4   | EP_EVENT      | Endpoint event (check 0x9302)                  |
| 5   | QUEUE_EVENT   | USB3 queue / USB2 event                        |
| 6   | BULK_EVENT    | Bulk transfer event (check 0x90E2)             |

### USB_MMIO_91C0 - Link Status Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 1   | LINK_UP       | Link established successfully                  |

### USB_MMIO_91D0 - Link Control Register
| Value | Description                                           |
|-------|-------------------------------------------------------|
| 0x02  | Trigger link state update                             |
| 0x08  | USB3 link power active (set by usb3_set_link_power)   |
| 0x10  | USB3 link power low (set by usb3_set_link_power)      |

### USB_MMIO_91D1 - Link Event Status (Write-to-Clear)
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | STATE_CHG     | Link state change (write 1 to ack)             |
| 1   | SPEED_NEG     | Speed negotiation complete (write 2 to ack)    |
| 2   | RESUME        | Resume detected (write 4 to ack)               |
| 3   | SUSPEND       | Suspend detected (write 8 to ack)              |

### USB_MMIO_9200 - EP NAK Control Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 6   | NAK_CTRL      | NAK control after reset                        |

### USB_MMIO_9201 - Bulk EP Control Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | BULK_EN0      | Bulk endpoint enable 0 (clear to disable)      |
| 1   | BULK_EN1      | Bulk endpoint enable 1 (clear to disable)      |

### USB_MMIO_920C - Bulk EP Status Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | BULK_FLAG0    | Bulk status flag 0                             |
| 1   | BULK_FLAG1    | Bulk status flag 1                             |

### USB_MMIO_92C0 - Bulk Config Register 0
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | BULK_CFG_EN   | Bulk configuration enable                      |
| 7   | POWER_EN      | Power enable (set during init)                 |

### USB_MMIO_92C1 - PHY Control Register 1
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | PHY_EN        | PHY enable                                     |
| 1   | BULK_PHY_EN   | Bulk PHY enable                                |
| 4   | PHY_POWER     | PHY power control                              |

### USB_MMIO_92C2 - PHY Status Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 6   | CABLE_CONN    | Cable/device connected status                  |

### USB_MMIO_92C5 - Bulk Config Register 5
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 2   | FLOW_CTRL     | Bulk endpoint flow control enable              |

### USB_MMIO_92C8 - PHY Link Control Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | PHY_LINK0     | PHY link control bit 0                         |
| 1   | PHY_LINK1     | PHY link control bit 1                         |

### USB_MMIO_92E1 - PHY Connect Signal Register
| Value | Description                                           |
|-------|-------------------------------------------------------|
| 0x10  | Trigger PHY connection sequence                       |

### USB_MMIO_92F8 - Link Training Status Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 2   | TRAIN_PHASE0  | Link training phase bit 0                      |
| 3   | TRAIN_PHASE1  | Link training phase bit 1 (in progress)        |
| 4   | TRAIN_DONE    | Link training complete                         |
| 5   | TRAIN_ACTIVE  | Link training active                           |

### USB_MMIO_9092 - Transfer Control Register
| Value | Description                                           |
|-------|-------------------------------------------------------|
| 0x01  | Trigger transfer (usb3_set_9092_bit0)                 |
| 0x02  | Ack/clear transfer (usb3_enable_bit1)                 |
| 0x04  | Data transfer (usb_transfer_data polls bit 2)         |
| 0x08  | Endpoint setup trigger (usb3_endpoint_setup)          |

### USB_MMIO_9002 - PHY Mode Control Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 1   | PHY_SS_EN     | SuperSpeed PHY enable                          |

### USB_MMIO_9206/9207 - Endpoint Config Registers
| Bits  | Description                                          |
|-------|------------------------------------------------------|
| 0-1   | Endpoint config (set to 3 during setup)              |
| 2-3   | Endpoint mode (set to 1 during setup)                |
| 4     | Link training flag (set based on 92f8.4)             |

### USB_MMIO_9208-920B - Endpoint Config Data
| Register | Description                                         |
|----------|-----------------------------------------------------|
| 0x9208   | EP config low 1 (0 or 0)                            |
| 0x9209   | EP config high 1 (5 or 10 based on 92f8.2-3)        |
| 0x920A   | EP config low 2 (0 or 0)                            |
| 0x920B   | EP config high 2 (5 or 10 based on 92f8.2-3)        |

### USB_MMIO_9202 - EP Status Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 1   | EP_STATUS     | Endpoint status (cleared during setup)         |

### USB_MMIO_9220 - EP Control Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 2   | EP_CTRL2      | EP control bit 2 (cleared in bit0_handler)     |
| 4   | EP_SETUP_EN   | EP setup enable (set during endpoint setup)    |

### USB_MMIO_9241 - EP Buffer Config Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 4   | BUF_EN        | Buffer enable                                  |
| 6-7 | BUF_MODE      | Buffer mode (set 0xC0 for bulk)                |

### USB_MMIO_9301 - EP Token Event Register (Write-to-Clear)
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 6   | IN_TOKEN      | IN token received (write 0x40 to ack)          |
| 7   | OUT_TOKEN     | OUT token received (write 0x80 to ack)         |

### USB_MMIO_9302 - EP Event Register (Write-to-Clear)
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | EP1_EVENT     | Endpoint 1 event                               |
| 1   | EP2_EVENT     | Endpoint 2 event                               |
| 2   | EP3_EVENT     | Endpoint 3 event                               |
| 3   | ERROR_EVENT   | Error event                                    |
| 4   | STALL_EVENT   | Stall event                                    |
| 5   | NAK_EVENT     | NAK event                                      |

### USB_MMIO_905D/905F - Endpoint Stall/Ready Registers
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | EP_READY      | Endpoint ready                                 |
| 3   | EP_STALL      | Endpoint stall (set to stall endpoint)         |

### USB_MMIO_9006 - EP0 Control Register
| Bit | Name          | Description                                    |
|-----|---------------|------------------------------------------------|
| 0   | EP0_READY     | EP0 ready for transfer (set to enable)         |
| 7   | EP0_STALL     | EP0 stall status                               |

### USB_MMIO_9094 - Control EP Config Register
| Value | Description                                           |
|-------|-------------------------------------------------------|
| 0x01  | NAK control packet                                    |
| 0x02  | Signal error (used with 9093=8)                       |
| 0x08  | Clear NAK / reset EP0                                 |
| 0x10  | Signal data phase complete (used with 9093=2)         |

## Renamed Symbols (Ghidra)

This section documents symbols renamed in Ghidra during reverse engineering.

### USB_MMIO Register Renames

| Old Name        | New Name                | Address | Description                    |
|-----------------|-------------------------|---------|--------------------------------|
| USB_MMIO_9000   | USB_MMIO_MODE_STATUS    | 0x9000  | USB mode status (bit0=USB3)    |
| USB_MMIO_9002   | USB_MMIO_PHY_MODE       | 0x9002  | PHY mode control               |
| USB_MMIO_9003   | USB_MMIO_XFER_LEN_LO    | 0x9003  | Transfer length low byte       |
| USB_MMIO_9004   | USB_MMIO_XFER_LEN_HI    | 0x9004  | Transfer length high byte      |
| USB_MMIO_9005   | USB_MMIO_EP0_CFG        | 0x9005  | EP0 configuration              |
| USB_MMIO_9006   | USB_MMIO_EP0_CTRL       | 0x9006  | EP0 control                    |
| USB_MMIO_9007   | USB_MMIO_XFER_ADDR_LO   | 0x9007  | Transfer address low byte      |
| USB_MMIO_9008   | USB_MMIO_XFER_ADDR_HI   | 0x9008  | Transfer address high byte     |
| USB_MMIO_900b   | USB_MMIO_EP_PHY_CTRL    | 0x900B  | Endpoint PHY control           |
| USB_MMIO_9010   | USB_MMIO_MODE_CFG1      | 0x9010  | USB mode config 1              |
| USB_MMIO_9018   | USB_MMIO_MODE_CFG2      | 0x9018  | USB mode config 2              |
| USB_MMIO_901a   | USB_MMIO_EP_INIT_CMD    | 0x901A  | Endpoint init command          |
| USB_MMIO_905d   | USB_MMIO_BULK_OUT_CTRL  | 0x905D  | Bulk OUT endpoint control      |
| USB_MMIO_905e   | USB_MMIO_EP_MISC        | 0x905E  | Endpoint miscellaneous         |
| USB_MMIO_905f   | USB_MMIO_BULK_IN_CTRL   | 0x905F  | Bulk IN endpoint control       |
| USB_MMIO_9090   | USB_MMIO_EP0_STATUS     | 0x9090  | EP0 status register            |
| USB_MMIO_9091   | USB_MMIO_SS_EVENT       | 0x9091  | SuperSpeed event status        |
| USB_MMIO_9092   | USB_MMIO_XFER_CTRL      | 0x9092  | Transfer control register      |
| USB_MMIO_9093   | USB_MMIO_CTRL_EP_INT    | 0x9093  | Control EP interrupt status    |
| USB_MMIO_9094   | USB_MMIO_CTRL_EP_CFG    | 0x9094  | Control EP configuration       |
| USB_MMIO_9096   | USB_MMIO_RESET_INT      | 0x9096  | USB reset interrupt status     |
| USB_MMIO_909e   | USB_MMIO_STATUS         | 0x909E  | USB status register            |
| USB_MMIO_90a0   | USB_MMIO_BULK_OUT_ACK   | 0x90A0  | Bulk OUT acknowledge           |
| USB_MMIO_90a1   | USB_MMIO_CONFIGURED     | 0x90A1  | USB configured status          |
| USB_MMIO_90e2   | USB_MMIO_BULK_INT       | 0x90E2  | Bulk transfer interrupt        |
| USB_MMIO_90e3   | USB_MMIO_BULK_CTRL      | 0x90E3  | Bulk transfer control          |
| USB_MMIO_9100   | USB_MMIO_LINK_SPEED     | 0x9100  | USB link speed register        |
| USB_MMIO_9101   | USB_MMIO_INT_DISPATCH   | 0x9101  | Main interrupt dispatch        |
| USB_MMIO_9119   | USB_MMIO_CBW_SIG0       | 0x9119  | CBW signature byte 0           |
| USB_MMIO_911a   | USB_MMIO_CBW_SIG1       | 0x911A  | CBW signature byte 1           |
| USB_MMIO_911b   | USB_MMIO_CBW_SIG2       | 0x911B  | CBW signature byte 2 ('U')     |
| USB_MMIO_911c   | USB_MMIO_CBW_SIG3       | 0x911C  | CBW signature byte 3 ('S')     |
| USB_MMIO_911d   | USB_MMIO_CBW_SIG4       | 0x911D  | CBW signature byte 4 ('B')     |
| USB_MMIO_911e   | USB_MMIO_CBW_SIG5       | 0x911E  | CBW signature byte 5 ('C')     |
| USB_MMIO_9123   | USB_MMIO_CBW_DLEN0      | 0x9123  | CBW data length byte 0         |
| USB_MMIO_9124   | USB_MMIO_CBW_DLEN1      | 0x9124  | CBW data length byte 1         |
| USB_MMIO_9125   | USB_MMIO_CBW_DLEN2      | 0x9125  | CBW data length byte 2         |
| USB_MMIO_9126   | USB_MMIO_CBW_DLEN3      | 0x9126  | CBW data length byte 3         |
| USB_MMIO_9127   | USB_MMIO_CBW_FLAGS      | 0x9127  | CBW flags (bit7=direction)     |
| USB_MMIO_9128   | USB_MMIO_CBW_LUN        | 0x9128  | CBW LUN field                  |
| USB_MMIO_9105   | USB_MMIO_SETUP_BMREQ    | 0x9105  | Setup packet bmRequestType     |
| USB_MMIO_910d   | USB_MMIO_SETUP_WLEN_LO  | 0x910D  | Setup wLength low byte         |
| USB_MMIO_910e   | USB_MMIO_SETUP_WLEN_HI  | 0x910E  | Setup wLength high byte        |
| USB_MMIO_9118   | USB_MMIO_QUEUE_IDX      | 0x9118  | USB3 queue index               |
| USB_MMIO_91c0   | USB_MMIO_LINK_STATUS    | 0x91C0  | Link status register           |
| USB_MMIO_91c1   | USB_MMIO_LINK_CFG1      | 0x91C1  | Link config 1                  |
| USB_MMIO_91c2   | USB_MMIO_LINK_CFG2      | 0x91C2  | Link config 2                  |
| USB_MMIO_91c3   | USB_MMIO_LINK_CFG3      | 0x91C3  | Link config 3                  |
| USB_MMIO_91c4   | USB_MMIO_EP_CFG_ADDR_LO | 0x91C4  | EP config address low          |
| USB_MMIO_91c5   | USB_MMIO_EP_CFG_ADDR_HI | 0x91C5  | EP config address high         |
| USB_MMIO_91c6   | USB_MMIO_EP_CFG_CMD     | 0x91C6  | EP config command              |
| USB_MMIO_91d0   | USB_MMIO_LINK_STATE_TRIG| 0x91D0  | Link state trigger             |
| USB_MMIO_91d1   | USB_MMIO_LINK_EVENT     | 0x91D1  | Link event status              |
| USB_MMIO_9201   | USB_MMIO_BULK_EP_ENABLE | 0x9201  | Bulk EP enable                 |
| USB_MMIO_920c   | USB_MMIO_BULK_EP_STATUS | 0x920C  | Bulk EP status                 |
| USB_MMIO_920f   | USB_MMIO_EP_RESET_CTRL  | 0x920F  | EP reset control               |
| USB_MMIO_9241   | USB_MMIO_EP_BUF_CFG     | 0x9241  | EP buffer config               |
| USB_MMIO_924c   | USB_MMIO_MODE_CTRL      | 0x924C  | USB mode control               |
| USB_MMIO_92c1   | USB_MMIO_PHY_ENABLE     | 0x92C1  | PHY enable                     |
| USB_MMIO_92c2   | USB_MMIO_PHY_CABLE_STATUS| 0x92C2 | PHY cable status               |
| USB_MMIO_92c4   | USB_MMIO_SS_EP_ENABLE   | 0x92C4  | SuperSpeed EP enable           |
| USB_MMIO_92c5   | USB_MMIO_BULK_FLOW_CTRL | 0x92C5  | Bulk flow control              |
| USB_MMIO_92c6   | USB_MMIO_EP_COUNT_OUT   | 0x92C6  | EP count OUT                   |
| USB_MMIO_92c7   | USB_MMIO_EP_COUNT_IN    | 0x92C7  | EP count IN                    |
| USB_MMIO_92c8   | USB_MMIO_PHY_LINK_CTRL  | 0x92C8  | PHY link control               |
| USB_MMIO_92cf   | USB_MMIO_PHY_TRAIN_CTRL | 0x92CF  | PHY training control           |
| USB_MMIO_92e0   | USB_MMIO_SOFT_RESET     | 0x92E0  | Soft reset                     |
| USB_MMIO_92e1   | USB_MMIO_PHY_CONNECT_TRIG| 0x92E1 | PHY connect trigger            |
| USB_MMIO_92f8   | USB_MMIO_LINK_TRAIN_STATUS| 0x92F8| Link training status           |
| USB_MMIO_92fb   | USB_MMIO_PHY_STATUS2    | 0x92FB  | PHY status 2                   |
| USB_MMIO_9200   | USB_MMIO_HS_STATUS      | 0x9200  | High-Speed status              |
| USB_MMIO_9202   | USB_MMIO_HS_CTRL        | 0x9202  | High-Speed control             |
| USB_MMIO_9206   | USB_MMIO_HS_DMA_ADDR0   | 0x9206  | HS DMA address byte 0          |
| USB_MMIO_9207   | USB_MMIO_HS_DMA_ADDR1   | 0x9207  | HS DMA address byte 1          |
| USB_MMIO_9208   | USB_MMIO_HS_DMA_ADDR2   | 0x9208  | HS DMA address byte 2          |
| USB_MMIO_9209   | USB_MMIO_HS_DMA_ADDR3   | 0x9209  | HS DMA address byte 3          |
| USB_MMIO_920a   | USB_MMIO_HS_DMA_LEN_LO  | 0x920A  | HS DMA length low byte         |
| USB_MMIO_920b   | USB_MMIO_HS_DMA_LEN_HI  | 0x920B  | HS DMA length high byte        |
| USB_MMIO_9220   | USB_MMIO_HS_EP0_STATUS  | 0x9220  | HS EP0 status                  |
| USB_MMIO_92c0   | USB_MMIO_HS_EP_CFG0     | 0x92C0  | HS endpoint config 0           |
| USB_MMIO_9300   | USB_MMIO_PHY_CFG0       | 0x9300  | PHY configuration 0            |
| USB_MMIO_9301   | USB_MMIO_PHY_CFG1       | 0x9301  | PHY configuration 1            |
| USB_MMIO_9302   | USB_MMIO_PHY_CFG2       | 0x9302  | PHY configuration 2            |
| USB_MMIO_9303   | USB_MMIO_PHY_CFG3       | 0x9303  | PHY configuration 3            |
| USB_MMIO_9304   | USB_MMIO_PHY_CFG4       | 0x9304  | PHY configuration 4            |
| USB_MMIO_9305   | USB_MMIO_PHY_CFG5       | 0x9305  | PHY configuration 5            |
| USB_CONTROL_9e00| USB_MMIO_SETUP_PACKET   | 0x9E00  | Setup packet buffer            |
| USB_CONTROL_9e08| USB_MMIO_CTRL_DATA_BUF  | 0x9E08  | Control data buffer            |

### EXTMEM Global Renames

| Old Name      | New Name                | Address | Description                    |
|---------------|-------------------------|---------|--------------------------------|
| EXTMEM_01b4   | EXTMEM_USB_EP_INDEX     | 0x01B4  | USB endpoint index             |
| EXTMEM_01b6   | EXTMEM_USB_REINIT_FLAG  | 0x01B6  | USB reinitialization flag      |
| EXTMEM_0465   | EXTMEM_USB_XFER_STATE   | 0x0465  | USB transfer state             |
| EXTMEM_0470   | EXTMEM_USB_XFER_FLAGS   | 0x0470  | USB transfer flags             |
| EXTMEM_0473   | EXTMEM_USB_XFER_COUNT   | 0x0473  | USB transfer count             |
| EXTMEM_0474   | EXTMEM_USB_XFER_IDX     | 0x0474  | USB transfer index             |
| EXTMEM_0475   | EXTMEM_USB_XFER_NEXT    | 0x0475  | USB transfer next pointer      |
| EXTMEM_0476   | EXTMEM_USB_XFER_CHUNKS  | 0x0476  | USB transfer chunk count       |
| EXTMEM_053a   | EXTMEM_USB_XFER_PENDING | 0x053A  | USB transfer pending flag      |
| EXTMEM_0578   | EXTMEM_USB_BULK_STATUS  | 0x0578  | USB bulk transfer status       |
| EXTMEM_05ac   | EXTMEM_USB_BULK_FLAG    | 0x05AC  | USB bulk transfer flag         |
| EXTMEM_06e6   | EXTMEM_USB_INIT_DONE    | 0x06E6  | USB initialization done flag   |
| EXTMEM_07e7   | EXTMEM_USB_CTRL_FLAG    | 0x07E7  | USB control flag               |
| EXTMEM_07f0   | EXTMEM_USB_DESC_PTR0    | 0x07F0  | USB descriptor pointer 0       |
| EXTMEM_07f1   | EXTMEM_USB_DESC_PTR1    | 0x07F1  | USB descriptor pointer 1       |
| EXTMEM_07f2   | EXTMEM_USB_DESC_PTR2    | 0x07F2  | USB descriptor pointer 2       |
| EXTMEM_07f3   | EXTMEM_USB_DESC_PTR3    | 0x07F3  | USB descriptor pointer 3       |
| EXTMEM_07f4   | EXTMEM_USB_DESC_PTR4    | 0x07F4  | USB descriptor pointer 4       |
| EXTMEM_07f5   | EXTMEM_USB_DESC_PTR5    | 0x07F5  | USB descriptor pointer 5       |
| EXTMEM_0ae2   | EXTMEM_USB_STATE_FLAG   | 0x0AE2  | USB state flag                 |
| EXTMEM_0ae3   | EXTMEM_USB_PHY_CONFIG   | 0x0AE3  | USB PHY configuration flag     |
| EXTMEM_000a   | EXTMEM_USB_ADDR_COUNT   | 0x000A  | USB address counter            |
| EXTMEM_07e4   | EXTMEM_USB_STATE_INIT   | 0x07E4  | USB state init                 |
| EXTMEM_07e5   | EXTMEM_USB_STATE_FLAG2  | 0x07E5  | USB state flag 2               |
| EXTMEM_07e6   | EXTMEM_USB_STATE_FLAG3  | 0x07E6  | USB state flag 3               |
| EXTMEM_07e8   | EXTMEM_USB_CBW_READY    | 0x07E8  | USB CBW ready flag             |
| EXTMEM_07e9   | EXTMEM_USB_STATE_FLAG4  | 0x07E9  | USB state flag 4               |
| EXTMEM_07ea   | EXTMEM_USB_HS_MODE      | 0x07EA  | USB high-speed mode flag       |
| EXTMEM_07eb   | EXTMEM_USB_STATE_FLAG5  | 0x07EB  | USB state flag 5               |
| EXTMEM_07ec   | EXTMEM_USB_STATE_FLAG6  | 0x07EC  | USB state flag 6               |
| EXTMEM_07ed   | EXTMEM_USB_STATE_FLAG7  | 0x07ED  | USB state flag 7               |
| EXTMEM_07ee   | EXTMEM_USB_LINK_TRAIN_FLAG| 0x07EE| USB link training flag         |
| EXTMEM_09f9   | EXTMEM_USB_CONN_STATUS  | 0x09F9  | USB connection status          |
| EXTMEM_09fa   | EXTMEM_USB_CONN_FLAGS   | 0x09FA  | USB connection flags           |
| EXTMEM_0acb   | EXTMEM_USB_EP_STATE     | 0x0ACB  | USB endpoint state             |
| EXTMEM_0acc   | EXTMEM_USB_EP_PENDING   | 0x0ACC  | USB endpoint pending           |
| EXTMEM_0acd   | EXTMEM_USB_LINK_STATE1  | 0x0ACD  | USB link state 1               |
| EXTMEM_0ace   | EXTMEM_USB_LINK_STATE2  | 0x0ACE  | USB link state 2               |
| EXTMEM_0ad1   | EXTMEM_USB_LINK_PHASE   | 0x0AD1  | USB link phase                 |
| EXTMEM_0ad2   | EXTMEM_USB_LINK_STATUS  | 0x0AD2  | USB link status                |
| EXTMEM_0ad3   | EXTMEM_USB_EP_TYPE      | 0x0AD3  | USB endpoint type              |
| EXTMEM_0ad4   | EXTMEM_USB_EP_CFG_MODE  | 0x0AD4  | USB EP config mode             |
| EXTMEM_0ad8   | EXTMEM_USB_XFER_SIZE_LO | 0x0AD8  | USB transfer size low          |
| EXTMEM_0ad9   | EXTMEM_USB_XFER_SIZE_HI | 0x0AD9  | USB transfer size high         |
| EXTMEM_0ada   | EXTMEM_USB_XFER_ADDR_LO | 0x0ADA  | USB transfer addr low          |
| EXTMEM_0adb   | EXTMEM_USB_XFER_ADDR_HI | 0x0ADB  | USB transfer addr high         |
| EXTMEM_0ade   | EXTMEM_USB_REMAIN_LO    | 0x0ADE  | USB remaining bytes low        |
| EXTMEM_0adf   | EXTMEM_USB_REMAIN_HI    | 0x0ADF  | USB remaining bytes high       |
| EXTMEM_0ae5   | EXTMEM_USB_POWER_MODE   | 0x0AE5  | USB power mode                 |
| EXTMEM_0aeb   | EXTMEM_USB_SUSPEND_STATE| 0x0AEB  | USB suspend state              |
| EXTMEM_0af1   | EXTMEM_USB_LINK_FLAGS   | 0x0AF1  | USB link flags                 |
| EXTMEM_0af2   | EXTMEM_USB_EP_READY_FLAG| 0x0AF2  | USB EP ready flag              |
| EXTMEM_0af3   | EXTMEM_USB_CBW_DIR      | 0x0AF3  | USB CBW direction (IN/OUT)     |
| EXTMEM_0af4   | EXTMEM_USB_CBW_LUN      | 0x0AF4  | USB CBW LUN                    |
| EXTMEM_0af5   | EXTMEM_USB_SLOT_IDX     | 0x0AF5  | USB slot index                 |
| EXTMEM_0af6   | EXTMEM_USB_RESET_STATE  | 0x0AF6  | USB reset state                |
| EXTMEM_0af8   | EXTMEM_USB3_MODE_FLAG   | 0x0AF8  | USB3 mode flag                 |
| EXTMEM_0aff   | EXTMEM_USB_QUEUE_SIZE   | 0x0AFF  | USB queue size                 |
| EXTMEM_0b00   | EXTMEM_USB_QUEUE_COUNT  | 0x0B00  | USB queue count                |
| EXTMEM_0b01   | EXTMEM_USB_QUEUE_READY  | 0x0B01  | USB queue ready                |
| EXTMEM_0b2e   | EXTMEM_USB_ACTIVE       | 0x0B2E  | USB active flag                |
| EXTMEM_0b2f   | EXTMEM_USB_CMD_FLAG     | 0x0B2F  | USB command flag               |
| EXTMEM_0b41   | EXTMEM_USB_POWER_FLAG   | 0x0B41  | USB power flag                 |

### Function Renames (This Session)

| Old Name        | New Name                  | Address | Description                    |
|-----------------|---------------------------|---------|--------------------------------|
| FUN_CODE_0016   | usb_cmd_state_processor   | 0x0016  | USB command/state processor    |
| FUN_CODE_0def   | usb_dispatch_return       | 0x0def  | USB dispatch return handler    |
| FUN_CODE_38d4   | usb_scsi_opcode_dispatch  | 0x38d4  | SCSI opcode dispatch (R/W)     |
| FUN_CODE_3da1   | usb2_bulk_out_process     | 0x3da1  | USB2 bulk OUT processing       |
| FUN_CODE_4b25   | usb3_bulk_out_process     | 0x4b25  | USB3 bulk OUT processing       |
| FUN_CODE_545c   | usb_clear_pending_state   | 0x545c  | Clear pending USB state        |
| FUN_CODE_e396   | usb_vendor_setup_init     | 0xe396  | Vendor request setup init      |
| int1            | int1                      | 0x4486  | Secondary interrupt handler    |
| FUN_CODE_d357   | usb_vendor_command_dispatch| 0xd357 | USB vendor command dispatcher  |
| FUN_CODE_e95b   | usb_vendor_request_handler| 0xe95b  | USB vendor request handler     |
| FUN_CODE_a740   | usb3_bit0_handler         | 0xa740  | USB3 connect/disconnect handler|
| FUN_CODE_d21e   | usb3_bit1_handler         | 0xd21e  | USB3 transfer complete handler |
| FUN_CODE_deb1   | usb3_bit2_handler         | 0xdeb1  | USB3 error handler             |
| FUN_CODE_b28c   | usb3_bit3_handler         | 0xb28c  | USB3 suspend/resume handler    |
| FUN_CODE_b6cf   | usb3_endpoint_setup       | 0xb6cf  | USB3 endpoint setup (9091.4)   |
| FUN_CODE_cf7f   | usb3_event_dispatcher     | 0xcf7f  | USB3 SuperSpeed event dispatch |

## Bank 1 USB Functions

These functions in Bank 1 handle USB commands and transfers:

| Address   | Name                      | Description                              |
|-----------|---------------------------|------------------------------------------|
| B1::bc5e  | usb_cmd_handler_b1        | USB command handler for 0x50/0x51        |
| B1::bd76  | usb_b1_cmd_query_handler  | USB command query (opcodes 0x3f,5,8,0x1c)|
| B1::c65f  | usb_b1_cmd_xfer_handler   | USB command transfer handler             |
| B1::c98d  | usb_b1_mode_check         | USB mode check (P/Q commands)            |
| B1::ca52  | usb_b1_cmd_power_handler  | USB command power handler                |
| B1::d440  | usb_xfer_handler_b1       | USB transfer handler                     |
| B1::da30  | usb_cmd_pq_b1             | USB command P/Q handler                  |
| B1::dbe7  | usb_cmd_50_51_b1          | USB command 0x50/0x51 handler            |
| B1::ddb0  | usb_cmd_pq_setup_b1       | USB command P/Q setup                    |
| B1::dd7e  | usb_b1_mode_handler       | USB mode handler                         |
| B1::dde0  | usb_dispatch_b1           | USB command dispatch and wait            |
| B1::e12b  | usb_b1_cmd_table_handler  | USB command table handler (0x3f,5)       |
| B1::e17b  | usb_dispatch_xfer_b1      | USB command dispatch with transfer       |
| B1::e632  | usb_b1_cmd_data_handler   | USB command data handler                 |

These Bank 1 functions:
- Check USB_MMIO_9000 bit 0 for USB3 vs USB2 mode
- Handle commands 'P' (0x50) and 'Q' (0x51)
- Poll USB_MMIO_9093 bit 3 for completion
- Write to USB_MMIO_9007/9008 for transfer setup
- Call usb_set_transfer_params, usb_command_dispatcher

## USB Caller Functions

These functions call USB core functions and form the upper layer of the USB stack:

### Interrupt/Event Handlers
| Address | Name                      | Description                              |
|---------|---------------------------|------------------------------------------|
| 0x1196  | usb_int0_cmd_handler      | INT0 USB command handler                 |
| 0xc694  | usb3_link_event_handler   | USB3 link event handler                  |
| 0xdc9d  | usb_conditional_ep_init   | Conditional endpoint initialization      |

### Configuration Handlers
| Address | Name                      | Description                              |
|---------|---------------------------|------------------------------------------|
| 0x32e9  | usb_complete_set_config   | Complete SET_CONFIGURATION request       |
| 0x33d8  | usb_ctrl_ack_only         | Simple control ACK wrapper               |
| 0x544c  | usb_set_config_params     | Set config transfer params (0,0x24,5)    |

### Link/State Management
| Address | Name                      | Description                              |
|---------|---------------------------|------------------------------------------|
| 0xa840  | usb_pcie_link_config      | USB/PCIe link configuration              |
| 0xc7a5  | usb_link_state_poll       | USB link state polling                   |
| 0xc00d  | usb_init_cleanup          | USB init cleanup                         |

### Transfer Setup
| Address | Name                      | Description                              |
|---------|---------------------------|------------------------------------------|
| 0x280a  | usb_set_xfer_params_bulk  | Set bulk transfer params (3,0x47,0xb)    |
| 0x1cf0  | usb_set_xfer_params_ctrl  | Set ctrl transfer params (0,0x20,5)      |
| 0x3f4a  | usb_state_mode_handler    | USB state/mode handler                   |
| 0x49e9  | usb_transfer_queue_process| USB transfer queue processing            |
| 0x50a2  | usb_setup_hs_transfer     | USB HS transfer setup                    |
| 0x5069  | usb_setup_state_transfer  | USB state-dependent transfer setup       |
| 0x425f  | usb_xfer_state_setup      | USB transfer state setup (speed-based)   |

### Command Handlers
| Address | Name                      | Description                              |
|---------|---------------------------|------------------------------------------|
| 0x4abf  | usb_cmd_data_copy         | USB command data copy handler            |
| 0x53e6  | usb_cmd_ctrl_xfer         | USB command control transfer             |
| 0x5455  | usb_ep_ready_setup        | USB EP ready setup                       |
| 0x3969  | usb_cmd_copy_set_state    | Command copy and set USB state           |

### State Machines and Main Handlers
| Address | Name                      | Description                              |
|---------|---------------------------|------------------------------------------|
| 0x9037  | usb_pcie_state_machine    | Main USB/PCIe state machine              |
| 0x2bea  | usb_mode_setup            | USB mode/HS setup                        |
| 0x2814  | usb_transfer_setup        | USB transfer setup                       |
| 0xe6fc  | usb_trigger_init_cleanup  | Triggers USB init cleanup                |
| 0xadb0  | pcie_link_init            | PCIe link init with USB cleanup          |
| 0x92bb  | usb_init_done_cleanup     | Simple init done wrapper                 |
| 0xe8e4  | usb_cmd_done_cleanup      | Command done cleanup                     |

### Bank 1 Transfer/Mode Handlers
| Address   | Name                      | Description                              |
|-----------|---------------------------|------------------------------------------|
| B1::d440  | usb_b1_xfer_handler       | Bank 1 transfer handler                  |
| B1::ec9b  | usb_b1_mode_dispatch      | Bank 1 mode dispatcher                   |
| B1::bc5e  | usb_b1_cmd_pq_handler     | Bank 1 P/Q command handler               |
| B1::e3d7  | usb_b1_xfer_complete      | Bank 1 transfer complete                 |

## Trampoline Functions (INT0 Dispatch)

These trampolines are called from int0 to dispatch to actual handlers:

| Address | Name                          | Target Function              | Trigger                  |
|---------|-------------------------------|------------------------------|--------------------------|
| 0x0345  | thunk_usb3_link_handler       | usb3_link_handler (0x9c2b)   | 91D1 bit 3 (link suspend)|
| 0x034a  | thunk_usb3_link_status        | usb3_link_status_handler     | 91D1 bit 0 (link state)  |
| 0x034f  | trampoline_link_speed_neg     | usb_link_speed_negotiation   | 91D1 bit 1 (speed neg)   |
| 0x0354  | trampoline_link_resume        | usb_link_resume_handler      | 91D1 bit 2 (resume)      |
| 0x035e  | trampoline_in_token           | usb_in_token_handler         | 9301 bit 6 (IN token)    |
| 0x0363  | thunk_usb_state_reset         | usb_state_reset              | 9301 bit 7 (OUT token)   |
| 0x0368  | trampoline_usb_state_transition| usb_state_transition        | 9300 bit 2 (PHY event)   |
| 0x036d  | trampoline_ep1_event          | usb_ep1_event_stub           | 9302 bit 0 (EP1 event)   |
| 0x04a3  | trampoline_vendor_request     | usb_vendor_request_handler   | Vendor control request   |

## Descriptor Handling

### USB Descriptors in ROM

USB descriptors are stored in CODE ROM at the following locations:

| Address     | Type                    | Description                                      |
|-------------|-------------------------|--------------------------------------------------|
| CODE:064f   | Device Descriptor       | 18 bytes, USB 2.1, VID=0x174C, PID=0x2462        |
| CODE:5870   | Configuration Desc      | 44 bytes, 1 interface, self-powered              |
| CODE:5879   | Interface Descriptor    | Mass Storage (08/06/50), 2 endpoints             |
| CODE:5880   | Endpoint 81 (Bulk IN)   | 1024 byte max packet + SS Companion              |
| CODE:588d   | Endpoint 02 (Bulk OUT)  | 1024 byte max packet + SS Companion              |

**Device Descriptor (CODE:064f):**
```
12 01 10 02 00 00 00 40 4c 17 62 24 01 00 02 03 01 01
  |  |  |     |  |  |  |  |     |     |     |  |  |  bNumConfigurations=1
  |  |  |     |  |  |  |  |     |     |     |  |  iSerialNumber=1
  |  |  |     |  |  |  |  |     |     |     |  iProduct=3
  |  |  |     |  |  |  |  |     |     |     iManufacturer=2
  |  |  |     |  |  |  |  |     |     bcdDevice=0x0001
  |  |  |     |  |  |  |  |     idProduct=0x2462
  |  |  |     |  |  |  |  idVendor=0x174C (ASMedia)
  |  |  |     |  |  |  bMaxPacketSize0=64
  |  |  |     |  |  bDeviceProtocol=0
  |  |  |     |  bDeviceSubClass=0
  |  |  |     bDeviceClass=0 (per-interface)
  |  |  bcdUSB=0x0210 (USB 2.1)
  |  bDescriptorType=1 (Device)
  bLength=18
```

**String Descriptors:**
- String 2 (Manufacturer): "Asmedia" (CODE:067d)
- String 3 (Product): "AS2462" (CODE:06a6)
- String 1 (Serial): "v000000000000" (CODE:0663)

The USB descriptor state machine (0x8756) builds descriptors in the 0x9E00 control buffer area:

### Descriptor State Values (EXTMEM_0ad2)
| State | Description                                           |
|-------|-------------------------------------------------------|
| 1     | Initial descriptor setup (9e02=0x20, 9e03=3, 9e07=9) |
| 2     | Speed-dependent config (HS: 0x79, FS: 0x55/0x20)     |
| 3     | Error/abort state                                     |
| 6     | Intermediate descriptor stage                         |
| 7     | Speed init handler called                             |
| 15    | Final descriptor stage                                |

### Speed-Dependent Configuration
- High Speed (USB 2.0 480Mbps): 9e07=0x80, 9e08=0x70
- Full Speed (USB 1.1 12Mbps): 9e07=0x80, 9e08=0xFA

## Open Questions / Unconfirmed Areas

This section documents areas requiring further investigation.

### 1. Setup Packet Full Layout (0x9100-0x9112)
**RESOLVED** - Setup packet handling clarified:

The USB setup packet (8 bytes) is received by hardware and partially accessible via MMIO:
- **0x9105**: Checked against 0xFF to validate setup packet presence (0xFF = invalid/no packet)
- **0x910D-0x910E**: wLength field (low/high bytes) - read by `usb_get_setup_wLength()`

**Key finding**: The setup packet fields (bmRequestType, bRequest) are NOT directly read from
MMIO 0x9106-0x910C. Instead:
1. Setup packet data is stored in IDATA at addresses 0x6b-0x6e (4 bytes)
2. `usb_read_idata_4bytes(0x6b)` reads these into R4-R7
3. Request type/code are copied to EXTMEM:
   - **EXTMEM 0x01**: bRequest (command code)
   - **EXTMEM 0x02**: bmRequestType (request type)
4. Control transfer handler (`usb_control_transfer_handler`) reads from EXTMEM 0x01/0x02

This explains why direct MMIO accesses to 0x9106-0x910C are not found - the firmware uses
IDATA as an intermediate buffer for setup packet parsing

### 2. MMIO_c4xx PHY Analog Configuration
**PARTIALLY RESOLVED** - Key registers identified:

| Address | Name          | Usage                                           |
|---------|---------------|-------------------------------------------------|
| 0xc428  | PHY_ANALOG1   | Bit 2: cleared in phy_init, Bit 5: set in init  |
| 0xc42a  | PHY_ANALOG2   | Bits 1-4: cleared in phy_init, Bit 4: toggled   |
| 0xc471  | BULK_ENABLE   | Bit 0: 1=bulk processing active (checked in int0)|
|         |               | Set to 1 in usb_phy_init                        |
| 0xc472  | PHY_CTRL      | Bit 0: cleared in usb_phy_init                  |
| 0xc47b  | BULK_PENDING  | Non-zero: bulk transfer pending (int0 check)    |

**c471 in INT0:** When c802.2 set (queue processing), loops while c471.0==1 AND c520.1==1,
calling usb_bulk_out_handler and usb_int0_cmd_handler.

### 3. MMIO_c5xx/c6xx Transfer Queue Registers
**PARTIALLY RESOLVED** - Now documented in "Auxiliary USB Registers" section.

Key registers:
- 0xc512: Bulk IN clear (write 0xFF)
- 0xc516: Queue index (bits 0-5)
- 0xc51a: Bulk OUT clear (write 0xFF)
- 0xc51e: Packet info (bits 0-5 = slot)
- 0xc520: Transfer status (bit 0 = IN ready, bit 1 = OUT ready)
- 0xc6b3: PHY ready (bits 4-5 polled during init)

Still unknown: 0xc6a8 usage details.

### 4. USB3 Queue Table (0x5a6a)
**PARTIALLY RESOLVED** - Queue table data documented.

Table at CODE:5a6a (priority selector):
```
08 00 01 00 02 00 01 00 03 00 01 00 02 00 01 00  (repeating pattern)
04 00 01 00 02 00 01 00 03 00 01 00 02 00 01 00
05 00 01 00 02 00 01 00 03 00 01 00 02 00 01 00
```
Values 0-7 are slot indices, 8 means "skip this entry".

Table at CODE:5b6a (bit masks and offsets):
```
01 02 04 08 10 20 40 80  (bit masks for 8 slots)
00 08 10 18 20 28 30 38  (byte offsets: 0, 8, 16, 24, 32, 40, 48, 56)
```

Queue algorithm: Read index from 9118, lookup slot in 5a6a, compute MMIO 0x9096+slot,
read and process via 5b72/5b6a lookup, loop up to 32 times.

### 5. Vendor Request Codes
**PARTIALLY RESOLVED** - Vendor request dispatch flow documented:

The control transfer handler recognizes these bmRequestType values:
- 0xE3 (-0x1d): Vendor IN request type 1
- 0xF9 (-7): Vendor request type 2
- 0xFB (-5): Vendor request type 3
- 0xE1 (-0x1f): Vendor OUT request type 1

**Vendor Request Dispatch Flow:**
1. `usb_control_transfer_handler` (0x32a5) checks bmRequestType from EXTMEM 0x02
2. For vendor types: calls `trampoline_vendor_request` (0x04a3)
3. Which calls `usb_vendor_request_handler` (0xe95b) → `usb_vendor_command_dispatch` (0xd357)
4. `usb_vendor_command_dispatch` calls:
   - `usb_vendor_setup_init` (0xe396): Initializes b8b9, b833, sets param to 3, be02, sets 0b21=0x80, 0b24=0xd8, 0b25=0x20
5. Dispatch reads EXTMEM 0x213:
   - Value 0: IDATA 0x4d = 0, calls FUN_CODE_c73c
   - Value 1: IDATA 0x4d = 0x80
   - Other: Returns immediately
6. If EXTMEM 0x04 != 0: calls FUN_CODE_b8a2 with direction byte (returns 0)
7. Clears EXTMEM_0aa8
8. Calls FUN_CODE_b850(0x80, 0xaa8), FUN_CODE_b104 for completion

**Vendor Request Functions:**
| Address | Name                      | Description                              |
|---------|---------------------------|------------------------------------------|
| 0x04a3  | trampoline_vendor_request | Entry trampoline from control handler    |
| 0xd357  | usb_vendor_command_dispatch| Main vendor command dispatch             |
| 0xe396  | usb_vendor_setup_init     | Setup EXTMEM params (0b21,0b24,0b25)     |
| 0xe95b  | usb_vendor_request_handler| Wrapper that calls dispatch              |
| 0xb8a2  | FUN_CODE_b8a2             | Direction handler (returns 0)            |
| 0xb850  | FUN_CODE_b850             | Transfer setup (param=0x80)              |
| 0xb104  | FUN_CODE_b104             | Completion handler                       |
| 0xc73c  | FUN_CODE_c73c             | Handler for EXTMEM_0213==0 case          |

The specific vendor command opcodes and their implementations in the b8xx/c7xx functions
need further analysis.

### 6. USB State Machine Values
The USB_STATE_ (INTMEM 0x6a) has documented states 0-5, 8, 11-12, but:
- State 6 purpose is unclear (error recovery?)
- Transitions between states need full mapping
- The value 0x0b triggers special handling in main loop

### 7. Descriptor Pointer Setup (0x07F0-0x07F5)
**PARTIALLY RESOLVED** - Descriptor data locations identified:

usb_hardware_init sets these values:
- 0x07F0 = 0x24, 0x07F1 = 0x04 → CODE:0424 (function pointer table)
- 0x07F2 = 0x17, 0x07F3 = 0x85 → CODE:8517 (Bank 1 descriptor table)
- 0x07F4 = 0x00, 0x07F5 = 0x00

**USB ID Data at CODE:0830:**
| Offset | Value    | Description                    |
|--------|----------|--------------------------------|
| 0x0833 | 0x174C   | Vendor ID (ASMedia)            |
| 0x0835 | 0x2464   | Product ID (ASM2464PD)         |
| 0x0864 | 0x81     | EP1 IN address                 |
| 0x086c | 0x82     | EP2 address                    |

Note: USB descriptors appear to be built dynamically in RAM (0x9E00 buffer)
rather than stored as static structures in code ROM.

### 8. BOT Protocol CSW Generation
**RESOLVED** - CSW buffer location confirmed:

The USB Mass Storage BOT protocol requires CSW (Command Status Wrapper) responses.
`usb_endpoint_init` (0x4904) writes the CSW signature "USBS" (0x55534253) to XRAM3_d800.

**CSW Buffer Layout (XRAM3_d800):**
| Offset | Field        | Value/Description                           |
|--------|--------------|---------------------------------------------|
| 0x00   | dCSWSignature| 0x53425355 ("USBS" little-endian)           |
| 0x04   | dCSWTag      | Copied from CBW dCBWTag                     |
| 0x08   | dCSWDataResidue | Difference from dCBWDataTransferLength   |
| 0x0C   | bCSWStatus   | 0x00=Passed, 0x01=Failed, 0x02=Phase Error  |

The CBW is received at USB_MMIO 0x9119-0x9128 with signature "USBC" (0x43425355).

### 9. Transfer Queue MMIO (0xc5xx region)
The bulk transfer handlers use these non-USB_MMIO registers:
- 0xc508: Queue config (bits 6-7 preserved, bits 0-5 = slot)
- 0xc512: Bulk IN interrupt clear (write 0xFF)
- 0xc516: Queue index read (bits 0-5)
- 0xc51a: Bulk OUT interrupt clear (write 0xFF)
- 0xc51e: Packet info (bits 0-5 = slot)
- 0xc520: Transfer status (bit 0 = IN ready, bit 1 = OUT ready)

## Auxiliary USB Registers (0xc4xx-0xcfxx)

These registers are outside the main USB_MMIO region but are used by USB functions:

### Interrupt Control (0xc8xx)
| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0xc802  | USB_INT_ENABLE    | USB interrupt enable. Bit 0: INT0 enable       |
|         |                   | Bit 2: Transfer queue processing enable        |
| 0xc806  | USB_INT_STATUS2   | Secondary interrupt status. Bit 5: Secondary pending |

### PHY Analog (0xc4xx)
| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0xc42c  | PHY_XFER_STATUS   | PHY transfer status. Bit 0: Transfer pending   |
| 0xc471  | PHY_BULK_ENABLE   | PHY bulk enable. Bit 0: Bulk processing active |
| 0xc47b  | PHY_BULK_STATUS   | PHY bulk status. Non-zero: Bulk pending        |

### Transfer Queue (0xc5xx)
| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0xc508  | QUEUE_CONFIG      | Bits 0-5: Slot, Bits 6-7: Mode (preserved)     |
| 0xc512  | QUEUE_IN_CLEAR    | Write 0xFF to clear bulk IN interrupt          |
| 0xc516  | QUEUE_INDEX       | Bits 0-5: Current queue index                  |
| 0xc51a  | QUEUE_OUT_CLEAR   | Write 0xFF to clear bulk OUT interrupt         |
| 0xc51e  | QUEUE_PKT_INFO    | Bits 0-5: Packet slot info (USB2)              |
| 0xc520  | QUEUE_STATUS      | Bit 0: IN ready, Bit 1: OUT ready              |

### Secondary Event (0xcexx)
| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0xcef2  | SEC_EVENT1        | Bit 7: Event pending (write 0x80 to ack)       |
| 0xcef3  | SEC_EVENT2        | Bit 3: Event pending (write 0x8 to ack)        |

### USB Config (0xc8xx/0xcaxx/0xccxx)
| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0xc800  | USB_CFG0          | Bit 2: Set during init                         |
| 0xc801  | USB_CFG1          | Bit 4: Set during init                         |
| 0xca60  | USB_PHY_CFG       | Bits 0-2: Set to 6, Bit 3: Set during init     |
| 0xcc32  | USB_MISC_CFG      | Bit 0: Cleared if PHY_CONFIG set               |
| 0xcc35  | USB_MISC_CFG2     | Bit 0: Cleared during init                     |
| 0xcc3b  | USB_INT_CFG       | Bit 1: Interrupt enable (set during init)      |
| 0xc6b3  | USB_PHY_READY     | Bits 4-5: PHY ready status (polled during init)|

## INT0 Complete Dispatch Table

The INT0 handler (0x0e5b) uses a hierarchical dispatch based on USB_INT_STATUS (0x9101):

### First Level: 0x9101 Bits
| Bit | Condition          | Handler/Sub-dispatch                          |
|-----|--------------------|--------------------------------------------- |
| 5   | Queue/Mode         | USB3: Queue processing, USB2: Reset check     |
| 3   | Link management    | 9301/9302 token handlers                      |
| 0   | Link events        | 91d1 link state handlers                      |
| 1   | SS event           | 033b SuperSpeed event                         |
| 2   | Control EP         | 9093 control transfer handlers                |
| 6   | Bulk transfer      | 90e2 bulk handlers                            |
| 4   | EP events          | 9302/9300 endpoint event handlers             |

### Second Level: Control EP (0x9093)
| Bit | Handler  | Ack Value | Description                            |
|-----|----------|-----------|----------------------------------------|
| 1   | 0x32a5   | 0x02      | Data stage complete                    |
| 3   | 0x4d44   | 0x08      | Control error                          |
| 0   | 0x5455   | 0x01      | Setup stage complete                   |
| 2   | 0x5455   | 0x04      | Status stage complete                  |

### Second Level: Link Events (0x91D1)
| Bit | Handler  | Ack Value | Description                            |
|-----|----------|-----------|----------------------------------------|
| 3   | 0x0345   | 0x08      | Suspend detected                       |
| 0   | 0x034a   | 0x01      | Link state change                      |
| 1   | 0x034f   | 0x02      | Speed negotiation                      |
| 2   | 0x0354   | 0x04      | Resume detected                        |

### Second Level: EP Events (0x9302/0x9300)
| Reg   | Bit | Handler  | Ack Value | Description                      |
|-------|-----|----------|-----------|----------------------------------|
| 9302  | 3   | 0x037c   | 0x08      | Error event                      |
| 9302  | 4   | 0x0381   | 0x10      | Stall event                      |
| 9302  | 5   | 0x0386   | 0x20      | NAK event                        |
| 9302  | 0   | 0x036d   | 0x01      | EP1 event                        |
| 9302  | 1   | 0x0372   | 0x02      | EP2 event                        |
| 9302  | 2   | 0x0377   | 0x04      | EP3 event                        |
| 9300  | 2   | 0x0368   | 0x04      | PHY state transition             |
| 9300  | 3   | 0x038b   | -         | PHY event                        |

### USB3 Queue Processing
When 9101.5 is set and 9000.0=1 (USB3 mode):
1. Read queue index from 0x9118
2. Lookup in table at CODE:0x5a6a to get slot
3. If slot < 8, compute MMIO address = 0x9096 + slot
4. Read MMIO, lookup in 0x5a6a again for sub-slot
5. If sub-slot < 8, process via lookup tables at 0x5b72/0x5b6a
6. Call 0x5442 with computed parameters
7. Loop up to 32 times
8. Check 909e.0, if set: write 909e=1, 90e3=2

## Pseudo-Code: USB Initialization to Enumeration

This section provides minimal pseudo-code for initializing the USB peripheral and handling
enumeration. This is a simplified version based on firmware analysis.

### 1. USB Hardware Initialization

```c
void usb_init(void) {
    // 1. Clear USB state
    EXTMEM[0x0ae2] = 0;  // USB_STATE_FLAG
    EXTMEM[0x07e7] = 0;  // USB_CTRL_FLAG

    // 2. PHY analog init
    MMIO[0xc004] |= 0x02;   // Set bit 1
    MMIO[0xc007] &= ~0x08;  // Clear bit 3
    MMIO[0xca2e] |= 0x01;   // Set bit 0

    // 3. Set descriptor pointers
    EXTMEM[0x07f0] = 0x24;  // Descriptor table low
    EXTMEM[0x07f1] = 0x04;  // Descriptor table high -> CODE:0424

    // 4. Configure USB registers
    MMIO[0xcc35] &= 0xFE;   // Clear bit 0
    MMIO[0xc801] |= 0x10;   // Set bit 4
    MMIO[0xc800] |= 0x04;   // Set bit 2
    MMIO[0xca60] = (MMIO[0xca60] & 0xF0) | 0x0E;  // bits 0-3 = 0x0E
    MMIO[0xcc3b] |= 0x02;   // Enable interrupts (bit 1)

    // 5. Configure bulk endpoints
    usb_bulk_endpoint_config();

    // 6. Wait for PHY ready
    while ((MMIO[0xc6b3] & 0x30) == 0) {
        // Poll until bits 4-5 are non-zero
    }

    // 7. Final init
    MMIO[0xca06] &= ~0x10;  // Clear bit 4

    // 8. Initialize USB/PCIe queue
    MMIO[0xcef3] = 0x08;
    MMIO[0xcef2] = 0x80;
    MMIO[0xc807] |= 0x04;   // Set bit 2

    // 9. Set USB init done flag
    EXTMEM[0x06e6] = 1;

    // 10. Reset USB state machine
    usb_state_reset();
}

void usb_bulk_endpoint_config(void) {
    USB_MMIO[0x92c6] = 5;     // EP count OUT
    USB_MMIO[0x92c7] = 0;     // EP count IN
    USB_MMIO[0x9201] &= ~0x03; // Disable bulk EPs initially
    USB_MMIO[0x92c1] |= 0x02;  // PHY control bit 1
    USB_MMIO[0x920c] &= ~0x03; // Clear bulk EP control
    MMIO[0xc20c] |= 0x40;      // DMA control
    MMIO[0xc208] &= ~0x10;     // DMA control
    USB_MMIO[0x92c0] |= 0x01;  // Bulk config enable
    USB_MMIO[0x92c1] |= 0x01;  // PHY bulk enable
    USB_MMIO[0x92c5] |= 0x04;  // Flow control enable
    USB_MMIO[0x9241] |= 0xD0;  // Buffer enable + mode
}

void usb_state_reset(void) {
    // Clear state variables
    EXTMEM[0x07e4] = 5;       // USB_STATE_INIT = configured
    EXTMEM[0x0af6] = 0;       // USB_RESET_STATE
    EXTMEM[0x07ee] = 0;       // USB_LINK_TRAIN_FLAG
    EXTMEM[0x0af2] = 0;       // USB_EP_READY_FLAG
    EXTMEM[0x0acb] = 0;       // USB_EP_STATE
    EXTMEM[0x0b2f] = 0;       // USB_CMD_FLAG

    // Clear PHY link control
    USB_MMIO[0x92c8] &= ~0x03;

    // Power sequence
    MMIO[0xcd31] = 4;
    MMIO[0xcd31] = 2;

    // Initialize endpoints
    usb_endpoint_init();
}

void usb_endpoint_init(void) {
    // Clear endpoint PHY control
    USB_MMIO[0x900b] &= ~0x07;  // Clear bits 0-2
    MMIO[0xc42a] &= ~0x1F;      // Clear bits 0-4

    // Set USB state to disconnected
    IDATA[0x6a] = 0;           // USB_STATE_ = 0

    // Write CSW signature "USBS" to buffer
    XRAM[0xd800] = 0x55;       // 'U'
    XRAM[0xd801] = 0x53;       // 'S'
    XRAM[0xd802] = 0x42;       // 'B'
    XRAM[0xd803] = 0x53;       // 'S'

    // Send endpoint init command
    USB_MMIO[0x901a] = 0x0D;

    // Clear transfer status
    MMIO[0xc42c] = 1;
    MMIO[0xc42d] &= ~0x01;
}
```

### 2. USB Mode Configuration

```c
void usb_mode_config(uint8_t mode) {
    // mode: 0 = USB3 SuperSpeed, non-zero = USB2 High-Speed

    // Common init
    MMIO[0xc430] = 0xFF;
    MMIO[0xc440] = 0xFF;
    USB_MMIO[0x9096] = 0xFF;
    USB_MMIO[0x9097] = 0xFF;
    USB_MMIO[0x9098] = 0x03;
    USB_MMIO[0x9011] = 0x00;

    if (mode == 0) {
        // USB3 SuperSpeed mode
        IDATA[0x3e] = 0xFF;        // Mode flag
        USB_MMIO[0x9018] = 0x03;   // USB3 mode config
        USB_MMIO[0x9010] = 0xFE;   // USB3 enable
    } else {
        // USB2 High-Speed mode
        IDATA[0x3e] = 0x00;        // Mode flag
        USB_MMIO[0x9018] = 0x02;   // USB2 mode config
        USB_MMIO[0x9010] = 0x00;   // USB2 mode
    }
}

void usb_full_register_init(void) {
    // Power enable
    USB_MMIO[0x92c0] |= 0x80;

    // Clear all interrupt status (write-to-clear)
    USB_MMIO[0x91d1] = 0x0F;   // Clear link events
    USB_MMIO[0x9300] = 0x0C;   // Clear EP events
    USB_MMIO[0x9301] = 0xC0;   // Clear IN/OUT token events
    USB_MMIO[0x9302] = 0xBF;   // Clear EP status events
    USB_MMIO[0x9091] = 0x1F;   // Clear USB3 SS events
    USB_MMIO[0x9093] = 0x0F;   // Clear control EP events

    // PHY configuration
    USB_MMIO[0x91c1] = 0xF0;
    USB_MMIO[0x9303] = 0x33;
    USB_MMIO[0x9304] = 0x3F;
    USB_MMIO[0x9305] = 0x40;
    USB_MMIO[0x9002] = 0xE0;
    USB_MMIO[0x9005] = 0xF0;

    // Clear bulk interrupt
    USB_MMIO[0x90e2] = 0x01;

    // Configure USB mode (USB3 by default)
    usb_mode_config(0);

    // PHY reset sequence
    USB_MMIO[0x91c3] &= ~0x20;
    USB_MMIO[0x91c0] |= 0x01;   // Set reset
    USB_MMIO[0x91c0] &= ~0x01;  // Clear reset

    // Wait for PHY ready
    while ((MMIO[0xe318] & 0x10) == 0 && (MMIO[0xcc11] & 0x02) == 0) {
        // Poll until PHY ready
    }
}
```

### 3. Interrupt Handling

```c
void int0_handler(void) {
    // Check if USB interrupt pending
    if ((MMIO[0xc802] & 0x01) == 0) return;

    uint8_t status = USB_MMIO[0x9101];  // INT_DISPATCH

    // USB3 queue processing (bit 5)
    if (status & 0x20) {
        if (USB_MMIO[0x9000] & 0x01) {
            // USB3 mode: process queue
            usb3_queue_process();
        } else {
            // USB2 mode: check for reset
            if (USB_MMIO[0x9096] & 0x01) {
                usb_reset_complete_handler();
                USB_MMIO[0x9096] = 0x01;  // Clear
            }
        }
    }

    // Link events (bit 0)
    if (status & 0x01) {
        uint8_t link_event = USB_MMIO[0x91d1];
        if (link_event & 0x08) {  // Suspend
            usb3_link_handler();
            USB_MMIO[0x91d1] = 0x08;
        }
        if (link_event & 0x01) {  // Link state change
            usb3_link_status_handler();
            USB_MMIO[0x91d1] = 0x01;
        }
    }

    // Control endpoint (bit 2)
    if (status & 0x04) {
        uint8_t ctrl_int = USB_MMIO[0x9093];
        if (ctrl_int & 0x02) {  // Data stage complete
            USB_MMIO[0x9093] = 0x02;
            usb_control_transfer_handler();
        }
        if (ctrl_int & 0x08) {  // Error
            usb_ctrl_ep_event();
            USB_MMIO[0x9093] = 0x08;
        }
    }

    // Bulk transfer (bit 6)
    if (status & 0x40) {
        if (USB_MMIO[0x90e2] & 0x01) {
            if (USB_MMIO[0x9000] & 0x01) {
                // USB3 mode
                usb_handle_command(2);
            } else {
                // USB2 mode
                usb_bus_reset_handler();
            }
            USB_MMIO[0x90e2] = 0x01;  // Clear
        }
    }

    // Transfer queue processing
    if (MMIO[0xc802] & 0x04) {
        if (USB_MMIO[0x9000] & 0x01) {
            // USB3 bulk handlers
            if (MMIO[0xc520] & 0x01) usb_bulk_in_handler();
            if (MMIO[0xc520] & 0x02) usb_bulk_out_handler();
        } else {
            // USB2 handlers
            if (MMIO[0xc520] & 0x02) usb_ep_state_handler();
            if (MMIO[0xc520] & 0x01) usb_transfer_queue_process();
        }
    }
}
```

### 4. Control Transfer Handling

```c
void usb_control_transfer_handler(void) {
    // Check USB state
    if (IDATA[0x6a] != 2) {  // Not in default state
        usb_stall_endpoint();
        return;
    }

    // Read request type from EXTMEM
    uint8_t bmRequestType = EXTMEM[0x02];
    uint8_t bRequest = EXTMEM[0x01];

    // Vendor request handling
    switch (bmRequestType) {
        case 0xE3:  // Vendor IN type 1
        case 0xF9:  // Vendor type 2
        case 0xFB:  // Vendor type 3
            usb_read_idata_4bytes(0x6b);  // Read setup packet
            usb_get_setup_wLength();
            // Process vendor command...
            break;

        case 0xE1:  // Vendor OUT type 1
            usb_set_request_mode();
            usb_vendor_request_handler();
            break;

        default:
            usb_stall_endpoint();
            return;
    }
}

void usb_enter_configured_state(void) {
    if (USB_MMIO[0x9000] & 0x01) {
        // USB3 mode
        usb3_config_stub();
        usb_setup_transfer();
    } else {
        // USB2 mode
        USB_MMIO[0x90a1] = 0x01;  // Set configured
    }

    IDATA[0x6a] = 5;  // USB_STATE_ = configured
}

void usb_stall_endpoint(void) {
    usb_set_endpoint_ready();

    if (USB_MMIO[0x9000] & 0x01) {
        // USB3: stall bulk endpoints
        USB_MMIO[0x905f] |= 0x01;  // Bulk IN stall
        USB_MMIO[0x905d] |= 0x01;  // Bulk OUT stall
    }
}

void usb_ctrl_ep_ack_data(void) {
    USB_MMIO[0x9093] = 0x02;  // Clear data stage interrupt
    USB_MMIO[0x9094] = 0x10;  // Signal data phase complete
}

void usb_ctrl_ep_ack_error(void) {
    USB_MMIO[0x9093] = 0x08;  // Clear error interrupt
    USB_MMIO[0x9094] = 0x02;  // Signal error
}
```

### 5. USB3 SuperSpeed Event Handling

```c
void usb3_event_dispatcher(void) {
    uint8_t events = USB_MMIO[0x9091];

    // Bit 0: Connect/disconnect (only if bit 2 not set)
    if ((events & 0x01) && !(events & 0x04)) {
        usb3_bit0_handler();
    }

    // Bit 1: Transfer complete (only if PHY bit 1 clear)
    if (!(USB_MMIO[0x9002] & 0x02) && (events & 0x02)) {
        usb3_bit1_handler();
        USB_MMIO[0x9091] = 0x02;  // Clear
    }

    // Bit 2: Error
    if (events & 0x04) {
        usb3_bit2_handler();
        USB_MMIO[0x9091] = 0x04;  // Clear
    }

    // Bit 3: Suspend/Resume
    if (events & 0x08) {
        usb3_bit3_handler();
        USB_MMIO[0x9091] = 0x08;  // Clear
    }

    // Bit 4: Endpoint setup
    if (events & 0x10) {
        usb3_endpoint_setup();
        USB_MMIO[0x9091] = 0x10;  // Clear
    }
}

void usb3_bit0_handler(void) {
    EXTMEM[0x07e4] = 0;   // USB_STATE_INIT = 0
    EXTMEM[0x07ec] = 1;   // USB_STATE_FLAG6 = 1

    // Clear PHY SS mode
    USB_MMIO[0x9002] &= ~0x02;

    if (EXTMEM[0x0ae5] == 0) {  // USB_POWER_MODE
        // Disable SS endpoint
        USB_MMIO[0x92c4] &= ~0x01;

        // Power sequence
        MMIO[0xcc17] = 4;
        MMIO[0xcc17] = 2;
    }

    EXTMEM[0x07ee] = 0;   // USB_LINK_TRAIN_FLAG

    if (USB_MMIO[0x9220] & 0x04) {
        USB_MMIO[0x9220] &= ~0x04;
    }

    EXTMEM[0x0ad7] = 0;

    // Clear connect interrupt
    do {
        USB_MMIO[0x9091] = 0x01;
    } while (USB_MMIO[0x9091] & 0x01);
}

void usb3_endpoint_setup(void) {
    if (EXTMEM[0x07e4] == 4 && EXTMEM[0x07ee] != 0) {
        // Configure endpoint registers
        USB_MMIO[0x9206] = (USB_MMIO[0x9206] & 0xF0) | 0x07;
        USB_MMIO[0x9207] = (USB_MMIO[0x9207] & 0xF0) | 0x07;

        // Check link training status
        if (USB_MMIO[0x92f8] & 0x10) {
            USB_MMIO[0x9206] |= 0x10;
            USB_MMIO[0x9207] |= 0x10;
        }

        // Set DMA parameters based on training status
        if ((USB_MMIO[0x92f8] & 0x0C) == 0) {
            USB_MMIO[0x9208] = 0;
            USB_MMIO[0x9209] = 5;
            USB_MMIO[0x920a] = 0;
            USB_MMIO[0x920b] = 5;
        } else {
            USB_MMIO[0x9208] = 0;
            USB_MMIO[0x9209] = 10;
            USB_MMIO[0x920a] = 0;
            USB_MMIO[0x920b] = 10;
        }

        USB_MMIO[0x9202] &= ~0x02;
        USB_MMIO[0x9220] |= 0x04;
    }

    // Trigger endpoint setup
    USB_MMIO[0x9092] = 0x08;

    EXTMEM[0x07e4] = 1;
    usb_idle_state_handler();
}
```

### 6. USB Speed Detection

```c
uint8_t usb_get_speed(void) {
    // Returns:
    //   0 = Disconnected
    //   1 = Full Speed (12 Mbps)
    //   2 = High Speed (480 Mbps)
    //   3 = SuperSpeed (5 Gbps)
    return USB_MMIO[0x9100] & 0x03;
}

bool usb_is_usb3_mode(void) {
    return (USB_MMIO[0x9000] & 0x01) != 0;
}
```

### 7. Enable Interrupts (Main Loop Entry)

```c
void main(void) {
    // Initialize USB
    usb_init();

    // Enable interrupts
    PX0 = 0;    // INT0 low priority
    PX1 = 0;    // INT1 low priority
    EX0 = 1;    // Enable INT0 (USB)
    EX1 = 1;    // Enable INT1 (PCIe/power)
    EA = 1;     // Global interrupt enable

    // Main loop
    while (1) {
        EA = 0;  // Disable interrupts for state check

        uint8_t state = EXTMEM[0x0ae2];  // USB_STATE_FLAG
        if (state != 0 && state != 0x10) {
            // Process USB state machine
            // ... state machine logic ...
        }

        EA = 1;  // Re-enable interrupts

        // Check cable connection
        if ((USB_MMIO[0x92c2] & 0x40) != 0) {  // Cable connected
            if ((USB_MMIO[0x91c0] & 0x02) != 0) {  // Link up
                // Process link events
            }
        }

        // Handle USB state 0x0b (special state)
        if (IDATA[0x6a] == 0x0b) {
            // Handle suspend/resume or mode switch
            if (USB_MMIO[0x9000] & 0x01) {
                usb_handle_command(0);
            } else {
                usb_bus_reset_handler();
            }
        }
    }
}
```

---

## USB4 VDM e42x Register Block (Bank 1)

The e42x registers are accessed by Bank 1 functions and control USB4 Vendor Defined Message (VDM) and link training configuration.

### e42x Register Map

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0xe420  | USB4_VDM_CTRL     | VDM control register, configured via link training |
| 0xe424  | USB4_VDM_CFG1     | VDM config 1, paired with e425               |
| 0xe425  | USB4_VDM_CFG2     | VDM config 2, set by usb_e424_e425_config    |
| 0xe428  | USB4_VDM_PARAM1   | VDM parameter, varies by 07ca state          |
| 0xe432  | USB4_VDM_PARAM2   | VDM parameter, configured with e435          |
| 0xe435  | USB4_VDM_PARAM3   | VDM parameter, values: 0x6d, varies          |

### Key e42x Configuration Functions (Bank 1)

```c
// b1_usb_e42x_vdm_full_config (0xaa63) - Full e42x config based on 07ca state
// Configures e428-e435 registers based on connection state at EXTMEM_07ca

// b1_usb_e42x_link_training_config (0xacd4) - e420 config with link training
// Uses cc10/cc11/cc12/cc13 link training status registers

// b1_usb_e420_e424_config (0xab0d) - Calls usb_e420_config_inner + usb_e424_e425_config

// b1_usb_e435_set_6d (0xaaed) - Sets e435 = 0x6d

// b1_usb_set_07c4_by_07ca (0xaafb) - Sets 07c4 to 0x16 or 0x12 based on 07ca state
```

### Link Training cc1x Registers

| Address | Name              | Description                                    |
|---------|-------------------|------------------------------------------------|
| 0xcc10  | LINK_TRAIN_CTL1   | Link training control 1                        |
| 0xcc11  | LINK_TRAIN_CTL2   | Link training control 2                        |
| 0xcc12  | LINK_TRAIN_CTL3   | Link training control 3                        |
| 0xcc13  | LINK_TRAIN_CTL4   | Link training control 4                        |
| 0xcc23  | LINK_TRAIN_TOGGLE | Toggle 4->2 during init                        |
| 0xcc89  | LINK_TRAIN_STATUS | Polled during PHY init                         |

### USB4 PHY e40x Init Sequence

The `b1_usb_phy_e40x_init_sequence` (0xae87) performs USB e40x PHY initialization with cc89 polling:

```c
void b1_usb_phy_e40x_init_sequence(void) {
    // Configure e40x PHY registers
    // Poll cc89 for completion
    while ((MMIO[0xcc89] & mask) != expected) {
        // Wait for PHY ready
    }
    // Continue with link training
}
```
