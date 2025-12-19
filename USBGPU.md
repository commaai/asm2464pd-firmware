# ASM2464PD as USBGPU - Hardware Analysis

## Architecture

```
USB Host ←──USB3──→ ASM2464PD ←──PCIe──→ GPU
                    (bridge)
                       │
                    8051 CPU
                       │
                   XDATA (64KB)
```

The ASM2464PD bridges USB3 to PCIe. Data flows:
- **Host → GPU**: USB bulk → XDATA buffer → PCIe TLP write → GPU BAR
- **GPU → Host**: PCIe TLP read → XDATA buffer → USB bulk → Host

## Executive Summary

For a USB3 "GPU" with fast copies in/out, the **E4/E5 vendor commands are too slow**.
They're designed for firmware patching (byte-at-a-time), not bulk data transfer.

For maximum throughput, you need:
1. **Bulk endpoints** (not control transfers)
2. **Large DMA transfers** (KB, not bytes)
3. **Minimal 8051 involvement** in the data path
4. **Direct PCIe TLP generation** for GPU access

## Current Transfer Methods - Speed Analysis

### E4/E5 Vendor Commands (SLOW)
```
Host                    8051 CPU                 Hardware
  |                        |                        |
  |--USB Control Xfer----->|                        |
  |   (8 byte setup)       |                        |
  |                        |--Process command------>|
  |                        |   (ISR overhead)       |
  |                        |--Config DMA----------->|
  |                        |<--DMA complete---------|
  |<---Response------------|                        |

Per-byte overhead: ~1000+ CPU cycles
E5 write: 1 byte per transfer
E4 read: Typically small (1-256 bytes)
```

### USB Bulk Transfer (FAST)
```
Host                    8051 CPU                 Hardware
  |                        |                        |
  |--Bulk OUT data---------|----------------------->|
  |   (1024 bytes/pkt)     |  (minimal involvement) |
  |                        |                        |
  |<---Bulk IN data--------|<-----------------------|

Per-packet: ~64-1024 bytes with minimal CPU overhead
USB3 SuperSpeed: Up to 5 Gbps
```

## Hardware Resources for USBGPU

### USED - Critical for Fast Transfers

| Resource | Address | Size | Purpose |
|----------|---------|------|---------|
| USB3 Controller | 0x9000-0x93FF | - | SuperSpeed bulk endpoints |
| USB Data Buffer | 0x8000-0x8FFF | 4KB | Main data staging |
| USB EP Buffer | 0xD800-0xDFFF | 2KB | Endpoint hardware buffer |
| DMA Engine | 0xCE00-0xCE9F | - | Hardware data movement |
| XDATA | 0x0000-0x5FFF | ~24KB | Working memory for "GPU" |

### ALSO USED - PCIe to GPU

| Resource | Address | Purpose |
|----------|---------|---------|
| PCIe TLP Engine | 0xB210-0xB25F | Memory read/write to GPU BAR |
| PCIe Link Control | 0xB400-0xB4FF | Link status, speed negotiation |
| PCIe DMA Trigger | 0xB296 | Trigger PCIe transactions |
| PCIe Address Regs | 0xB218-0xB21B | 32-bit GPU address |
| PCIe Data Reg | 0xB220 | Read/write data (4 bytes) |

### NOT USED - Can Simplify/Ignore

| Resource | Address | Purpose |
|----------|---------|---------|
| NVMe Controller | 0xC4xx-0xC5xx | NVMe-specific command processing |
| NVMe Queues | 0xA000-0xB1FF | NVMe submission/completion queues |
| SCSI Translation | Most of scsi.c | Mass storage protocol overhead |

## Recommended Architecture for Fast Transfers

### Option 1: Modified Bulk Endpoints (Recommended)
Repurpose the existing Mass Storage bulk endpoints:

```
EP1 OUT (Bulk): Host → Device data
EP2 IN  (Bulk): Device → Host data

Max packet size:
  - USB 2.0 High Speed: 512 bytes
  - USB 3.0 SuperSpeed: 1024 bytes
```

**Firmware changes needed:**
1. Modify USB descriptors (remove Mass Storage class)
2. Strip SCSI command parsing
3. Direct DMA from bulk endpoint to XDATA buffer

### Option 2: Vendor-Specific Bulk Protocol
Create custom bulk command protocol:

```c
// Command packet (first bulk OUT packet)
struct gpu_cmd {
    uint8_t  opcode;      // 0x01=write, 0x02=read, 0x03=execute
    uint8_t  flags;
    uint16_t addr;        // XDATA address
    uint32_t length;      // Bytes to transfer
};

// Data follows in subsequent bulk packets
```

## Key Registers for Bulk Transfers

### USB Bulk Endpoint Control
```c
// Endpoint configuration
REG_USB_EP_CFG_905A     // EP config
REG_USB_EP_BUF_HI       // DMA source high (0x905B)
REG_USB_EP_BUF_LO       // DMA source low (0x905C)
REG_USB_EP_CTRL_905D    // EP control 1
REG_USB_EP_READY        // EP ready flags (0x9096)

// DMA control
REG_SCSI_DMA_CTRL       // 0xCE00 - write 0x03 to start
REG_USB_DMA_STATE       // 0xCE89 - poll for completion
```

### PCIe TLP Engine (GPU Access)
```c
// TLP configuration
0xB210  PCIE_FMT_TYPE    // TLP type: 0x00=MemRd, 0x40=MemWr
0xB213  PCIE_TLP_CTRL    // Write 0x01 to enable
0xB217  PCIE_BYTE_EN     // Byte enables (0x0F = all 4 bytes)

// GPU address (32-bit BAR address)
0xB218  PCIE_ADDR_0      // Address bits 7:0
0xB219  PCIE_ADDR_1      // Address bits 15:8
0xB21A  PCIE_ADDR_2      // Address bits 23:16
0xB21B  PCIE_ADDR_3      // Address bits 31:24

// Data (4 bytes per TLP)
0xB220  PCIE_DATA        // Write data for MemWr
0xB22C  PCIE_CPL_DATA    // Read data from MemRd completion

// Trigger and status
0xB254  PCIE_TRIGGER     // Write 0x0F to send TLP
0xB296  PCIE_STATUS      // Bit 1=complete, Bit 0=error
```

### PCIe Transaction Sequence
```c
// Memory Write to GPU:
1. Set 0xB210 = 0x40        // MemWr TLP type
2. Set 0xB213 = 0x01        // Enable
3. Set 0xB217 = 0x0F        // All bytes
4. Set 0xB218-0xB21B = addr // GPU BAR address
5. Set 0xB220 = data        // 4 bytes to write
6. Write 0xB296 = 0x01,0x02,0x04 // Clear status
7. Write 0xB254 = 0x0F      // Trigger!
8. Poll 0xB296 bit 1        // Wait for complete

// Memory Read from GPU:
1. Set 0xB210 = 0x00        // MemRd TLP type
2. Set 0xB218-0xB21B = addr // GPU BAR address
3. Trigger and wait...
4. Read 0xB22C              // Completion data
```

### USB Speed Register
```c
REG_USB_LINK_STATUS     // 0x9100
// Bits 0-1: Speed mode
//   0 = Full Speed (12 Mbps) - unusable for GPU
//   1 = High Speed (480 Mbps)
//   2 = SuperSpeed (5 Gbps) - target this
//   3 = SuperSpeed+ (10 Gbps)
```

## DMA Path for Maximum Throughput

### Bulk OUT (Host → GPU)
```
1. USB hardware receives bulk packet (1024 bytes)
2. Hardware DMA writes to USB_SCSI_BUF (0x8000)
3. Firmware gets interrupt when USB transfer complete
4. Firmware generates PCIe MemWr TLPs to GPU BAR
   - Loop: read 4 bytes from XDATA, write to PCIe
   - Or use burst mode if available
```

### Bulk IN (GPU → Host)
```
1. Firmware generates PCIe MemRd TLPs to GPU BAR
2. Read completion data into USB_SCSI_BUF (0x8000)
3. Configure EP DMA source address
4. Write to DMA trigger register
5. Hardware DMA sends to host
```

### Bottleneck Analysis
```
USB3 SuperSpeed: 5 Gbps = 625 MB/s theoretical
PCIe Gen3 x4:    32 Gbps = 4 GB/s theoretical

Actual bottleneck: 8051 CPU generating TLPs!
- Each 4-byte PCIe transaction needs ~100 CPU cycles
- CPU at ~100 MHz = ~1M TLPs/sec = ~4 MB/s
```

## PCIe Burst/DMA Mode Investigation

**Conclusion: NO hardware DMA between XDATA and PCIe for bridge-initiated transfers.**

### Why NVMe is Fast (Bus Mastering)

NVMe SSDs achieve high throughput because the SSD is the **bus master**:
```
Host Memory                      NVMe SSD
    │                               │
    │<──── DMA Read (commands) ─────│  SSD reads submission queue
    │                               │
    │<──── DMA Write (data) ────────│  SSD writes data to host
    │──── DMA Read (data) ──────────>│  SSD reads data from host
    │                               │
    │<──── DMA Write (completion) ──│  SSD writes completion
```

The ASM2464PD provides PCI addresses for NVMe to DMA:
- NVME_IOSQ_DMA_ADDR (0x00820000) - Submission queue
- NVME_DATA_BUF_DMA_ADDR (0x00200000) - Data buffer

The 8051 CPU just sets up queues and rings doorbells - NVMe hardware does the heavy lifting.

### Why GPU Access is Slow (Programmed I/O)

For GPU access, the bridge must be the **initiator**:
```
ASM2464PD (8051)                 GPU BAR
    │                               │
    │──── MemWr TLP (4 bytes) ──────>│  Bridge writes to GPU
    │<─── Completion ───────────────│
    │                               │
    │──── MemRd TLP ────────────────>│  Bridge reads from GPU
    │<─── CplD (4 bytes) ───────────│
```

Each 4-byte transfer requires:
1. Set PCIE_FMT_TYPE (0xB210) = 0x40 for MemWr
2. Set PCIE_TLP_CTRL (0xB213) = 0x01
3. Set PCIE_BYTE_EN (0xB217) = 0x0F
4. Set PCIE_ADDR_0..3 (0xB218-0xB21B) = address
5. Set PCIE_DATA (0xB220) = data
6. Clear PCIE_STATUS (0xB296)
7. Trigger via PCIE_TRIGGER (0xB254) = 0x0F
8. Poll PCIE_STATUS for completion

**~10 XDATA accesses per 4 bytes = ~100 CPU cycles minimum**

### Hardware Findings

| Register | Value | Notes |
|----------|-------|-------|
| PCIE_TLP_LENGTH (0xB216) | Always 0x20 | Mode flag, not burst size |
| PCIE_DATA (0xB220) | 4 bytes | Only data register |
| PCIE_BYTE_EN (0xB217) | 0x0F | Always 4 bytes enabled |

Searched for but NOT found:
- ❌ Multi-DWORD TLP configuration
- ❌ Hardware scatter-gather DMA to PCIe
- ❌ Auto-incrementing address mode
- ❌ FIFO-to-PCIe DMA engine

### Maximum Achievable Throughput

**PCIe path (XDATA → GPU):**
- ~100 CPU cycles per 4-byte TLP
- 100 MHz CPU = 1M TLPs/sec
- **~4 MB/s maximum** for bridge-initiated PCIe

**USB path (Host → XDATA):**
- Hardware DMA from USB bulk to XDATA
- Minimal CPU involvement
- **~300 MB/s possible** with bulk endpoints

### Implications for USBGPU

The bottleneck is inescapable with current hardware:
1. USB → XDATA: Fast (hardware DMA)
2. XDATA → GPU: **SLOW** (programmed I/O)
3. GPU → XDATA: **SLOW** (programmed I/O)
4. XDATA → USB: Fast (hardware DMA)

**Possible Workarounds:**
1. Use XDATA as "GPU memory" directly (compute on 8051)
2. Minimize GPU writes (batch commands, use GPU for compute)
3. Use GPU's own DMA if it supports bus mastering ← **THIS IS THE SOLUTION**
4. Accept ~4 MB/s for GPU memory access

## GPU Bus Mastering - The Fast Path

### Key Insight: Use NVMe's DMA Addresses

The ASM2464PD exposes XDATA regions as PCIe BAR addresses that downstream devices can DMA to:

| XDATA Region | PCI Address | Size | Purpose |
|--------------|-------------|------|---------|
| 0xA000-0xAFFF | 0x00820000 | 4KB | Command Queue (was NVMe IOSQ) |
| 0xB100-0xB1FF | 0x00800000 | 256B | Completion Queue (was NVMe ACQ) |
| 0xF000-0xFFFF | 0x00200000 | 4KB | Data Buffer (was NVMe data) |

**NVMe SSDs achieve high throughput because THEY are the bus master** - they DMA directly to these addresses without 8051 involvement.

### GPU Bus Master Architecture

```
USB Host                  ASM2464PD Bridge              GPU
   │                           │                          │
   │──USB Bulk OUT──────>│──>XDATA[0x8000]               │
   │                      │      │                        │
   │                      │      v                        │
   │                      │  Firmware copies              │
   │                      │  to Command Queue             │
   │                      │      │                        │
   │                      │      v                        │
   │              PCI BAR @ 0x00820000 ←── GPU Reads ────│
   │              (maps to XDATA 0xA000)  Command Queue  │
   │                      │                              │
   │                      │                              │
   │              PCI BAR @ 0x00200000 ←── GPU DMAs ────>│
   │              (maps to XDATA 0xF000)  Data           │
   │                      │                              │
   │                      v                              │
   │<──USB Bulk IN──│<──XDATA[0xF000]                   │
```

### Protocol for GPU Bus Mastering

1. **Host → GPU Command:**
   - Host sends command via USB bulk to XDATA[0x8000]
   - Firmware copies command to queue at XDATA[0xA000]
   - Firmware rings "doorbell" (writes to GPU's doorbell register via TLP)
   - GPU reads command from PCI address 0x00820000

2. **GPU → Bridge Data Transfer:**
   - GPU DMAs data to PCI address 0x00200000
   - Data appears in XDATA[0xF000] automatically
   - No 8051 involvement in data transfer!

3. **Bridge → Host Data Transfer:**
   - Firmware triggers USB DMA from XDATA[0xF000] to USB bulk endpoint
   - Hardware DMA sends to host at USB3 speeds

### Throughput with GPU Bus Mastering

**With GPU as bus master:**
- USB → XDATA: ~300 MB/s (hardware DMA)
- GPU ↔ XDATA: **~4 GB/s** (GPU DMA, no CPU) ← Now fast!
- XDATA → USB: ~300 MB/s (hardware DMA)

**Bottleneck shifts to USB3** which is the ideal scenario.

### Requirements for GPU

For this to work, the GPU must:
1. **Support bus mastering** - it must be able to initiate PCIe transactions
2. **Know the PCI addresses** - firmware tells GPU where to DMA:
   - Command queue: 0x00820000
   - Data buffer: 0x00200000
3. **Use appropriate TLP types** - MemWr for writes, MemRd for reads

Most GPUs (even integrated ones) support bus mastering for their own memory access, so this should be achievable.

### Implementation Steps

1. **Initialize GPU as bus master:**
   - Enable bus master bit in GPU's PCIe config space
   - Configure GPU's BAR to not conflict with bridge addresses

2. **Set up shared memory regions:**
   - Command queue at XDATA 0xA000 (GPU reads from PCI 0x00820000)
   - Data buffer at XDATA 0xF000 (GPU reads/writes PCI 0x00200000)

3. **Define command format:**
   ```c
   struct gpu_cmd {
       uint8_t opcode;     // GPU operation
       uint8_t flags;
       uint16_t length;    // Data length
       uint32_t gpu_addr;  // Address in GPU memory
       uint32_t reserved;
   };
   ```

4. **Doorbell mechanism:**
   - Firmware writes to GPU doorbell via single TLP (slow, but infrequent)
   - GPU processes command, DMAs data (fast, bulk of transfer)
   - GPU writes completion to PCI 0x00800000 (XDATA 0xB100)

### Comparison

| Method | USB→GPU | GPU→USB | CPU Involvement |
|--------|---------|---------|-----------------|
| E4/E5 Vendor | ~10 KB/s | ~50 KB/s | Every byte |
| Bridge TLP (MemWr) | ~4 MB/s | ~4 MB/s | Every 4 bytes |
| GPU Bus Master | ~300 MB/s | ~300 MB/s | Command only |

### Verifying PCI Address Configuration

The PCI addresses (0x00200000, 0x00820000, etc.) are configured during PCIe enumeration.
To verify/discover them:

1. **From host side:** Use `lspci -vvv` to see the bridge's BAR configuration
2. **From firmware:** The addresses are stored in XDATA globals and NVMe init code
3. **At runtime:** GPU driver can read bridge config space to discover available addresses

The mapping is established by the bridge hardware - when a downstream device (GPU)
issues a MemWr TLP to PCI address 0x00200000, the bridge hardware automatically
routes it to XDATA 0xF000. This is the same mechanism NVMe SSDs use.

### Actual Buffer Size: 6-8MB Internal SRAM

The XDATA addresses (0x8000, 0xF000) are just **4KB windows** into a much larger internal buffer.

**PCI Address Space Layout (from firmware constants):**
```
PCI Address       Offset    Purpose
0x00200000        2 MB      Data buffer start (NVME_DATA_BUF_DMA_ADDR)
0x00820000        8.125 MB  I/O Submission Queue (NVME_IOSQ_DMA_ADDR)

Gap: 0x00820000 - 0x00200000 = 0x00620000 = 6.125 MB
```

**Firmware size table at 0x5BDB (32-bit little-endian):**
```
Index  Bytes (hex)    Size
0      00 00 02 00    128 KB
1      00 00 04 00    256 KB
2      00 00 08 00    512 KB
3      00 00 10 00    1 MB
4      00 00 20 00    2 MB
5      00 00 40 00    4 MB
6      00 00 80 00    8 MB   ← Maximum transfer size
7      00 00 01 00    64 KB  (fallback)
```

**Buffer Size Analysis:**
- **Conservative (NVMe mode):** 6MB - staying below IOSQ at 0x00820000
- **Maximum (USBGPU mode):** 8MB - if IOSQ/ACQ regions repurposed
- **Physical SRAM:** At least 8MB based on supported transfer sizes

**Architecture:**
```
                 Internal SRAM (~8MB)
    ┌────────────────────────────────────────────────┐
    │                                                │
    │  0x00200000 ┌──────────────────────────────┐   │
    │             │     Data Buffer Region       │   │
    │             │        (6-8 MB)              │   │
    │             │                              │   │
    │             │   USB bulk ←→ DMA engine     │   │
    │             │   NVMe SSD ←→ DMA engine     │   │
    │             │   GPU ←→ DMA engine          │   │
    │             └──────────────────────────────┘   │
    │  0x00820000 ┌──────────────────────────────┐   │
    │             │  NVMe Command Queues (128KB) │   │
    │             └──────────────────────────────┘   │
    └────────────────────────────────────────────────┘
                         │
              ┌──────────┴──────────┐
              │   8051 CPU Views    │
              │  (4KB windows only) │
              ├─────────────────────┤
              │ XDATA 0x8000: USB   │
              │ XDATA 0xF000: NVMe  │
              └─────────────────────┘
```

**The 8051 CPU sees only fixed 4KB windows:**
- XDATA 0x8000-0x8FFF: 4KB window into USB buffer region
- XDATA 0xF000-0xFFFF: 4KB window into NVMe buffer region
- These are FIXED mappings - no dynamic window/bank select

**But hardware DMA accesses the FULL buffer via 32-bit addressing.**

### USB DMA Indexing Mechanism

USB does NOT use simple 16-bit offsets for large transfers. Instead, it uses
**32-bit PCI-style addressing** via the SCSI DMA registers.

**DMA Address Registers (CE76-CE79):**
```
Register   Purpose
CE76       Address byte 0 (LSB)
CE77       Address byte 1
CE78       Address byte 2
CE79       Address byte 3 (MSB)
```

**Data Flow for USB Bulk Transfers:**
```
1. SCSI command arrives with LBA (logical block address)
2. Firmware calculates 32-bit buffer address from LBA
3. Address stored in XDATA 0x0544-0x0547
4. DMA registers (CE76-CE79) programmed with 32-bit address
5. Hardware DMA transfers data - 8051 never touches bulk data
```

**Firmware Address Setup (from 0x385d):**
```asm
mov dptr, #0x0544    ; Source: calculated 32-bit address
lcall 0x0d5c         ; Read 4 bytes into R4-R7
mov dptr, #0xce76    ; Dest: DMA address registers
lcall 0x0d9d         ; Write 4 bytes (R4-R7) to CE76-79
```

**Slot Table Structure (0x0580+):**
Firmware maintains a slot table with 9-byte records for tracking transfers:
```c
// Calculate slot address:
slot_addr = 0x0580 + (slot_index * 9);
// Each slot contains buffer address and transfer parameters
```

**Key Insight:** The 16-bit endpoint buffer pointers (D803/D804) are only used
for small control transfers. Bulk data uses the 32-bit CE76-CE79 path, enabling
access to the full 6-8MB internal SRAM regardless of 8051's 64KB limitation.

**Throughput with Hardware DMA:**
- USB3 SuperSpeed: ~400 MB/s (16KB bursts)
- NVMe DMA: ~4 GB/s to internal SRAM
- GPU DMA (bus master): ~4 GB/s to internal SRAM
- 8051 CPU only handles control path, not data movement

### Tinygrad USB Buffer Sizes (Actual Usage)

From `~/tinygrad/tinygrad/runtime/support/usb.py`:

```python
# Line 47-48: Per-stream buffers
self.buf_data_in = [(ctypes.c_uint8 * 0x1000)() ...]   # 4 KB per stream
self.buf_data_out = [(ctypes.c_uint8 * 0x80000)() ...] # 512 KB per stream

# Line 158-159: SCSI write chunking
for i in range(0, len(buf), 0x10000):  # 64 KB chunks
    self.exec_ops([ScsiWriteOp(buf[i:i+0x10000], lba), ...])
```

**Actual Buffer Sizes Used:**
| Buffer | Size | Notes |
|--------|------|-------|
| USB Data Out | 512 KB | Host→Device per stream |
| USB Data In | 4 KB | Device→Host per stream |
| SCSI Write Chunk | 64 KB | Per SCSI WRITE(16) command |
| Max Streams | 31 | USB3 bulk streams |

**Why 64KB SCSI chunks?**
- 64 KB = 128 sectors × 512 bytes
- Matches standard NVMe maximum transfer size per command
- Allows firmware to process completion between chunks
- Reliable across all USB host controllers

**Total Concurrent Bandwidth:**
- 31 streams × 512 KB = **~16 MB** in flight
- But individual SCSI writes are 64 KB each

### Caveats

1. **GPU driver complexity:** Need custom GPU driver or modify existing one to DMA to bridge addresses
2. **Chunk coordination:** 4KB buffer means careful firmware coordination for larger transfers
3. **Latency:** Each command still requires firmware to ring GPU doorbell (~microseconds)
4. **GPU support:** Not all GPUs may support arbitrary DMA to non-GPU addresses

Despite these caveats, GPU bus mastering is the ONLY way to achieve high throughput
through this bridge for GPU workloads.

## Memory Map for USBGPU

```
XDATA Address Space (64KB):
0x0000-0x05FF  GPU control/status area
0x0600-0x0FFF  Command queues
0x1000-0x6FFF  GPU working memory (~24KB)
0x7000-0x7FFF  Flash buffer (can repurpose if not updating FW)
0x8000-0x8FFF  USB I/O buffer (4KB) - DMA target
0x9000-0x9FFF  MMIO registers (read-only logical)
0xA000-0xAFFF  (Was NVMe IOSQ - can repurpose)
0xB000-0xBFFF  (Was NVMe Admin - can repurpose)
0xC000-0xEFFF  MMIO registers
0xF000-0xFFFF  (Was NVMe data - can repurpose)
```

## Theoretical Maximum Throughput

### USB 3.0 SuperSpeed
- Raw: 5 Gbps = 625 MB/s
- Actual: ~400 MB/s (protocol overhead)
- Bulk transfer: 1024 bytes per microframe

### Current E4/E5 (Measured)
- E5 write: ~1-10 KB/s (control transfer overhead)
- E4 read: ~10-50 KB/s

### Optimized Bulk (Target)
- Bulk OUT: 100-300 MB/s
- Bulk IN: 100-300 MB/s

## Implementation Priority

1. **Modify USB descriptors** to vendor class
2. **Implement bulk handlers** - bypass SCSI layer
3. **Direct DMA setup** - minimize CPU involvement
4. **Command protocol** - define GPU operations
5. **Optimize ISR** - reduce interrupt latency

## Files to Modify

### Keep/Modify
- `src/usb/usb_init.c` - USB initialization
- `src/usb/usb_descriptor.c` - Change to vendor class
- `src/isr/usb_isr.c` - Bulk endpoint handlers
- `src/drivers/dma.c` - DMA engine control

### Remove/Stub
- `src/app/scsi.c` - SCSI command parsing
- `src/app/nvme.c` - NVMe translation
- `src/drivers/nvme.c` - NVMe controller

## Current USB Descriptors (from fw.bin)

### Device Descriptor (0x0627)
```
VID: 0x174C (ASMedia)
PID: 0x2462
USB Version: 2.1
Max Packet EP0: 64 bytes
```

### Configuration Descriptor (0x58CF) - USB3 SuperSpeed
```
Interface Class: 0x08 (Mass Storage)
Interface SubClass: 0x06 (SCSI)
Interface Protocol: 0x50 (Bulk-Only Transport)

EP1 IN  (0x81): Bulk, 1024 bytes max packet ← Already USB3 size!
EP2 OUT (0x02): Bulk, 1024 bytes max packet ← Already USB3 size!
```

### Key Insight
**The bulk endpoints already support 1024-byte packets for USB3 SuperSpeed!**

The bottleneck is NOT the endpoint size - it's the SCSI command processing layer.
Each SCSI command has:
- 31-byte CBW (Command Block Wrapper)
- Data phase
- 13-byte CSW (Command Status Wrapper)
- Firmware processing overhead

For USBGPU, you want to **bypass SCSI entirely** and use raw bulk transfers.

## Quick Win: Larger E4 Reads

If you can't modify the firmware yet, you can improve E4 by:
1. Requesting larger sizes (up to 4KB)
2. Batching multiple addresses in one read
3. The DMA already supports large transfers

But this is still limited by control transfer overhead (~64KB/s max).

## Fastest Path: Direct Bulk

To achieve maximum throughput:

1. **Keep the existing endpoint configuration** (EP1/EP2, 1024-byte packets)
2. **Replace SCSI handler** with direct DMA handler
3. **Define simple command format** in first bulk packet
4. **Stream data** in subsequent packets

Example flow for 64KB write:
```
Host sends:  [CMD: WRITE 0x1000 len=65536] (1 packet)
Host sends:  [DATA: 64 × 1024-byte packets]
Device DMA:  Bulk EP → XDATA[0x1000-0x10FFF]
Host receives: [STATUS: OK]
```

This eliminates:
- SCSI CBW/CSW overhead
- SCSI command parsing
- Multiple DMA setup cycles
