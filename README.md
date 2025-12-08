# ASM2464PD Firmware Reimplementation

Open-source C firmware for the ASM2464PD USB4/Thunderbolt to NVMe bridge controller.

## Target Hardware

**ASM2464PD** - Multi-interface bridge IC:
- USB 3.2 Gen2x2 / USB4 / Thunderbolt host interface
- PCIe 4.0 x4 NVMe storage interface
- 8051 CPU core (~114 MHz, 1T architecture)
- 98KB firmware in two 64KB code banks

## Building

### Prerequisites

* [SDCC](https://sdcc.sourceforge.net/) (Small Device C Compiler)
* GNU Make
* Python 3 (for the wrapper step)

On Debian/Ubuntu the toolchain can be installed with:

```bash
sudo apt-get update
sudo apt-get install -y sdcc make python3
```

### Build steps

```bash
make              # Build build/firmware.bin
make wrapped      # Build with ASM2464 header (checksum + CRC)
make compare      # Compare against original fw.bin
make clean        # Remove build artifacts
```

## Memory Map

### Code Banks
```
Bank 0: 0x00000-0x0FFFF  (64KB, direct access)
Bank 1: 0x10000-0x17F12  (32KB, via DPX register, mapped at 0x8000)
```

### XDATA Memory Map
```
0x0000-0x5FFF  RAM (globals, work areas, stack)
0x6000-0xFFFF  Memory-mapped I/O registers
```

### Hardware Register Blocks
```
0x7000-0x7FFF  Flash Buffer (4KB)
0x8000-0x8FFF  USB/SCSI Buffer (4KB)
0x9000-0x93FF  USB Interface Controller
0x9E00-0x9FFF  USB Control Buffer
0xA000-0xAFFF  NVMe I/O Submission Queue (4KB)
0xB000-0xB0FF  NVMe Admin Submission Queue
0xB100-0xB1FF  NVMe Admin Completion Queue
0xB200-0xB4FF  PCIe Passthrough / TLP Engine
0xB800-0xB80F  PCIe Queue Control
0xC000-0xC00F  UART Controller (921600 baud)
0xC200-0xC2FF  Link / PHY Control
0xC400-0xC5FF  NVMe Interface Controller
0xC600-0xC6FF  PHY Extended Registers
0xC800-0xC80F  Interrupt Controller
0xC870-0xC87F  I2C Controller
0xC89F-0xC8AE  SPI Flash Controller
0xC8B0-0xC8D9  DMA Engine
0xCA00-0xCAFF  CPU Mode Control
0xCC10-0xCC24  Hardware Timers (4 channels)
0xCC30-0xCCFF  CPU Control Extended
0xCE00-0xCE9F  SCSI DMA / Transfer Control
0xCEB0-0xCEB3  USB Descriptor Validation
0xCEF0-0xCEFF  CPU Link Control
0xD800-0xD810  USB Endpoint Buffer / CSW
0xE300-0xE3FF  PHY Completion / Debug
0xE400-0xE4FF  Command Engine
0xE600-0xE6FF  Debug / Interrupt Status
0xE700-0xE7FF  System Status / Link Control
0xEC00-0xEC0F  NVMe Event Controller
0xEF00-0xEFFF  System Control
0xF000-0xFFFF  NVMe Data Buffer (4KB)
```

## Project Structure

```
src/
├── main.c           # Entry, init, main loop, ISRs
├── registers.h      # Hardware register definitions
├── globals.h        # Global variables
├── drivers/
│   ├── usb.c        # USB protocol and endpoints
│   ├── nvme.c       # NVMe command interface
│   ├── pcie.c       # PCIe/Thunderbolt interface
│   ├── dma.c        # DMA engine
│   ├── flash.c      # SPI flash
│   ├── timer.c      # Hardware timers
│   ├── uart.c       # Debug UART
│   ├── phy.c        # PHY/link layer
│   └── power.c      # Power management
└── app/
    ├── scsi.c       # SCSI/Mass Storage handler
    ├── protocol.c   # Protocol state machine
    ├── buffer.c     # Buffer management
    └── dispatch.c   # Bank switching stubs
```

## Reference Materials

- `fw.bin` - Original firmware (98,012 bytes)
- `ghidra.c` - Ghidra decompilation reference
- `usb-to-pcie-re/` - Reverse engineering docs

## Development

1. Each function matches one in original firmware
2. Include address comments: `/* 0xABCD-0xABEF */`
3. Use `REG_` for registers, `G_` for globals
4. Analyze with radare2 or Ghidra

## License

Reverse engineering project for educational and interoperability purposes.
