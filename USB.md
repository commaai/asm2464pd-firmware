# USB Enumeration Implementation Plan

## Overview

This document describes how to implement USB enumeration in our custom firmware
by analyzing the trace from the original firmware running via proxy.

## Required MMIO Ranges

Based on testing with `--proxy-mask`, these ranges MUST be proxied to real hardware:

| Range | Size | Description |
|-------|------|-------------|
| 0x9000-0x9400 | 1KB | USB Interface - control registers, endpoint config |
| 0x9E00-0x9F00 | 256B | USB Control Buffer - descriptor response buffer |
| 0xC800-0xC900 | 256B | Interrupt/DMA/Flash - interrupt status, flash control |
| 0xCC00-0xCD00 | 256B | Timer/CPU Control - timers, CPU mode |
| 0xE700-0xE800 | 256B | System Status - link status, PHY control |

## Proxy Command

Run with maximum masking (fastest, only required ranges proxied):
```bash
python3 emulate/emu.py --proxy \
  --proxy-mask 0x6000-0x9000 \
  --proxy-mask 0x9400-0x9E00 \
  --proxy-mask 0x9F00-0xC800 \
  --proxy-mask 0xC900-0xCC00 \
  --proxy-mask 0xCD00-0xE700 \
  --proxy-mask 0xE800-0xFFFF \
  fw.bin
```

---

## Implementation Phases

### Phase 1: Timer/CPU Init + Confirm Interrupts

**Goal**: Get basic hardware initialized and confirm INT0/INT1 interrupts fire.

#### Register Initialization Sequence (from trace)

**1. CPU/Timer Initial Setup (cycles ~26k):**
```c
REG_CPU_EXEC_STATUS = 0x01;      // 0xCC32
REG_CPU_MODE = 0x01;             // 0xCC30
REG_LINK_WIDTH_E710 = 0x04;      // 0xE710
REG_CPU_EXEC_STATUS_2 = 0x04;    // 0xCC33
REG_TIMER_CTRL_CC3B = 0x0C;      // 0xCC3B
REG_LINK_CTRL_E717 = 0x01;       // 0xE717
REG_CPU_CTRL_CC3E = 0x00;        // 0xCC3E
REG_LINK_STATUS_E716 = 0x03;     // 0xE716
REG_TIMER_CTRL_CC39 = 0x06;      // 0xCC39
REG_TIMER_ENABLE_B = 0x14;       // 0xCC3A
REG_TIMER_ENABLE_A = 0x44;       // 0xCC38
REG_CPU_CTRL_CC37 = 0x2C;        // 0xCC37
REG_SYS_CTRL_E780 = 0x00;        // 0xE780
```

**2. Link Status Toggle:**
```c
REG_LINK_STATUS_E716 = 0x00;     // Clear
REG_LINK_STATUS_E716 = 0x03;     // Set
```

**3. Timer0 Setup:**
```c
REG_TIMER0_CSR = 0x04;           // Stop timer
REG_TIMER0_CSR = 0x02;           // Clear timer
REG_TIMER0_DIV = 0x12;           // Divisor
REG_TIMER0_THRESHOLD_HI = 0x00;  // Threshold high
REG_TIMER0_THRESHOLD_LO = 0xC8;  // Threshold low (200 = 0xC8)
REG_TIMER0_CSR = 0x01;           // Start timer
```

**4. System Control:**
```c
REG_SYS_CTRL_E76C = 0x04;        // 0xE76C
REG_SYS_CTRL_E774 = 0x04;        // 0xE774
REG_SYS_CTRL_E77C = 0x04;        // 0xE77C
```

**5. Interrupt Enable (cycles ~51k):**
```c
REG_INT_AUX_STATUS = 0x02;       // 0xC805
REG_INT_ENABLE = 0x10;           // 0xC801
REG_INT_STATUS_C800 = 0x04;      // 0xC800
```

**6. More Interrupt Setup (cycles ~85k-172k):**
```c
REG_INT_ENABLE = 0x50;           // 0xC801 - more interrupts
REG_INT_CTRL = 0x08;             // 0xC809
REG_INT_CTRL = 0x0A;             // Later
REG_INT_CTRL = 0x2A;             // Even later
```

**7. USB Buffer Config (cycles ~105k):**
```c
REG_BUF_CFG_9300 = 0x0C;         // 0x9300
REG_BUF_CFG_9301 = 0xC0;         // 0x9301
REG_BUF_CFG_9302 = 0xBF;         // 0x9302
REG_BUF_CFG_9303 = 0x33;         // 0x9303
REG_BUF_CFG_9304 = 0x3F;         // 0x9304
REG_BUF_CFG_9305 = 0x40;         // 0x9305
REG_USB_CONFIG = 0xE0;           // 0x9002
REG_USB_EP0_LEN_H = 0xF0;        // 0x9005
```

**8. 8051 IE SFR (enable interrupts):**
```c
IE = 0x85;  // EA=1 (global), EX0=1 (INT0), ET0=1 (Timer0)
// Or: IE = 0x87 to also enable EX1 (INT1)
```

#### ISR Implementation

```c
void int0_isr(void) __interrupt(0) {
    // INT0 = USB/SCSI interrupt
    uint8_t sys_status = REG_INT_SYSTEM;     // 0xC806
    uint8_t usb_status = REG_INT_USB_STATUS; // 0xC802
    uart_puts("[INT0]\n");
    // TODO: Handle USB setup packets
}

void int1_isr(void) __interrupt(2) {
    // INT1 = PD/Power interrupt
    uint8_t pcie_status = REG_INT_PCIE_NVME; // 0xC80A
    uart_puts("[INT1]\n");
}

void timer0_isr(void) __interrupt(1) {
    // Timer0 overflow
    // Re-arm timer if needed
}
```

#### Test Procedure
1. Build: `cd clean && make`
2. Flash: `make flash-proxy`
3. Run emulator with debug: `python3 emulate/emu.py --proxy --proxy-debug 1 fw.bin`
4. Watch for `[INT0]` and `[INT1]` on UART output
5. Verify in proxy debug log that interrupts fire and RETI completes

---

### Phase 2: USB Setup Packet Handling

**Goal**: Read and parse USB setup packets in INT0 ISR.

#### Setup Packet Location
USB setup packets arrive at 0x9104-0x910B:
```c
uint8_t bmRequestType = REG_USB_SETUP_BMREQ;  // 0x9104
uint8_t bRequest = REG_USB_SETUP_BREQ;        // 0x9105
uint8_t wValueL = REG_USB_SETUP_WVAL_L;       // 0x9106
uint8_t wValueH = REG_USB_SETUP_WVAL_H;       // 0x9107
uint8_t wIndexL = REG_USB_SETUP_WIDX_L;       // 0x9108
uint8_t wIndexH = REG_USB_SETUP_WIDX_H;       // 0x9109
uint8_t wLengthL = REG_USB_SETUP_WLEN_L;      // 0x910A
uint8_t wLengthH = REG_USB_SETUP_WLEN_H;      // 0x910B
```

#### Standard USB Requests
- `bmRequestType=0x80, bRequest=0x06` = GET_DESCRIPTOR
  - `wValueH=0x01` = Device descriptor
  - `wValueH=0x02` = Configuration descriptor
  - `wValueH=0x03` = String descriptor
- `bmRequestType=0x00, bRequest=0x05` = SET_ADDRESS
- `bmRequestType=0x00, bRequest=0x09` = SET_CONFIGURATION

---

### Phase 3: USB Descriptor Response

**Goal**: Respond to GET_DESCRIPTOR with device descriptor.

#### Device Descriptor (18 bytes)
```c
__code const uint8_t device_descriptor[] = {
    0x12,        // bLength
    0x01,        // bDescriptorType (Device)
    0x10, 0x02,  // bcdUSB (USB 2.0)
    0x00,        // bDeviceClass
    0x00,        // bDeviceSubClass
    0x00,        // bDeviceProtocol
    0x40,        // bMaxPacketSize0 (64 bytes)
    0xBB, 0xAA,  // idVendor (0xAABB)
    0xDD, 0xCC,  // idProduct (0xCCDD)
    0x01, 0x00,  // bcdDevice
    0x02,        // iManufacturer
    0x03,        // iProduct
    0x01,        // iSerialNumber
    0x01         // bNumConfigurations
};
```

#### Response Buffer
Write descriptor to 0x9E00-0x9E07 (first 8 bytes for short packet):
```c
for (i = 0; i < len; i++) {
    XDATA8(0x9E00 + i) = device_descriptor[i];
}
```

#### Arm EP0
```c
REG_BUF_CFG_9300 = 0x08;  // Arm EP0 for IN
// or
REG_BUF_CFG_9301 = 0xC0;  // EP0 ready
```

---

### Phase 4: Full Enumeration

**Goal**: Handle SET_ADDRESS, configuration descriptor, SET_CONFIGURATION.

#### SET_ADDRESS Handling
```c
if (bRequest == 0x05) {  // SET_ADDRESS
    uint8_t addr = wValueL;
    // Hardware may handle this automatically
    // Send zero-length status packet
}
```

#### Configuration Descriptor
Must include:
- Configuration descriptor (9 bytes)
- Interface descriptor (9 bytes)
- Endpoint descriptors (7 bytes each)

---

## Key Register Reference

### USB Interface (0x9000-0x9400)
| Address | Name | Description |
|---------|------|-------------|
| 0x9000 | REG_USB_STATUS | USB status register |
| 0x9002 | REG_USB_CONFIG | USB configuration |
| 0x9003 | REG_USB_EP0_STATUS | EP0 status |
| 0x9004-5 | REG_USB_EP0_LEN | EP0 transfer length |
| 0x9091 | REG_USB_CTRL_PHASE | USB control phase |
| 0x9100 | REG_USB_LINK_STATUS | USB link status |
| 0x9104-B | USB Setup Packet | 8-byte setup packet |
| 0x9300-5 | REG_BUF_CFG | Buffer configuration |

### USB Control Buffer (0x9E00-0x9F00)
| Address | Name | Description |
|---------|------|-------------|
| 0x9E00-07 | USB Response | Descriptor data written here |

### Interrupt (0xC800-0xC900)
| Address | Name | Description |
|---------|------|-------------|
| 0xC800 | REG_INT_STATUS | Main interrupt status |
| 0xC801 | REG_INT_ENABLE | Interrupt enable |
| 0xC802 | REG_INT_USB_STATUS | USB interrupt status |
| 0xC805 | REG_INT_AUX_STATUS | Auxiliary status |
| 0xC806 | REG_INT_SYSTEM | System interrupt status |
| 0xC809 | REG_INT_CTRL | Interrupt control |
| 0xC80A | REG_INT_PCIE_NVME | PCIe/NVMe interrupt |

### Timer/CPU (0xCC00-0xCD00)
| Address | Name | Description |
|---------|------|-------------|
| 0xCC10 | REG_TIMER0_DIV | Timer0 divisor |
| 0xCC11 | REG_TIMER0_CSR | Timer0 control (0x04=stop, 0x02=clear, 0x01=start) |
| 0xCC12-13 | REG_TIMER0_THRESHOLD | Timer0 threshold |
| 0xCC30 | REG_CPU_MODE | CPU mode |
| 0xCC32 | REG_CPU_EXEC_STATUS | Execution status |
| 0xCC37-3F | CPU Control | Various CPU control registers |

### System Status (0xE700-0xE800)
| Address | Name | Description |
|---------|------|-------------|
| 0xE710 | REG_LINK_WIDTH | Link width |
| 0xE712 | REG_USB_EP0_COMPLETE | EP0 transfer complete |
| 0xE716 | REG_LINK_STATUS | Link status |
| 0xE717 | REG_LINK_CTRL | Link control |
| 0xE76C,74,7C | System Control | Various system control |
| 0xE780 | REG_SYS_CTRL | System control |

---

## Interrupt Flow

1. Hardware sets interrupt pending flag
2. Proxy firmware detects interrupt, sends `0x7E <mask>` to emulator
3. Emulator queues interrupt, runs ISR code
4. ISR reads status registers (C802, C806), handles event
5. ISR returns (RETI)
6. Emulator sends `CMD_INT_ACK` to proxy
7. Proxy clears sent_int_mask, interrupt can fire again

## VID/PID Patching

The emulator patches VID/PID during descriptor writes at PC=0xB495:
- 0x9E08: VID low 0x4C -> 0xBB
- 0x9E09: VID high 0x17 -> 0xAA
- 0x9E0A: PID low 0x63 -> 0xDD
- 0x9E0B: PID high 0x24 -> 0xCC
