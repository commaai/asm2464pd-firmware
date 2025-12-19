# ASM2464PD as USBGPU - How Tinygrad Uses It

## Overview

Tinygrad uses the ASM2464PD USB4-to-NVMe bridge as a "USB GPU" by exploiting its PCIe
initiator capability and large internal SRAM buffer. This document explains the actual
implementation and identifies the slow path that needs improvement.

## Hardware Architecture

```
USB Host (tinygrad)           ASM2464PD Bridge                   GPU
       │                            │                              │
       │  USB3 Bulk Streams         │  PCIe TLPs                   │
       │  (31 concurrent)           │                              │
       │                            │                              │
       ├──EP 0x02 OUT (data)───────>│                              │
       ├──EP 0x04 OUT (commands)───>│  8051 CPU                    │
       │                            │     │                        │
       │<──EP 0x81 IN (data)────────│     ├──Config Space──────────>│
       │<──EP 0x83 IN (status)──────│     ├──Memory Write──────────>│
       │                            │     └──Memory Read───────────>│
       │                            │            │                  │
       │                            │            v                  │
       │                      Internal SRAM (6-8 MB)               │
       │                      ┌────────────────────┐               │
       │                      │ Data @ 0x00200000  │<──GPU DMA────>│
       │                      │ Queues @ 0x00820000│               │
       │                      └────────────────────┘               │
```

## Tinygrad's USB Communication

### Endpoints Used

| Endpoint | Direction | Purpose |
|----------|-----------|---------|
| 0x02     | OUT       | Bulk data to device (SCSI writes) |
| 0x04     | OUT       | SCSI command blocks |
| 0x81     | IN        | Bulk data from device |
| 0x83     | IN        | SCSI status responses |

### Buffer Allocations (from usb.py)

```python
# Per-stream buffers (31 streams for USB3 bulk streaming)
buf_data_out = 512 KB per stream    # Host → Device data
buf_data_in  = 4 KB per stream      # Device → Host data (NOTE: small!)
buf_cmd      = 32 bytes             # SCSI CDB wrapper
buf_stat     = 64 bytes             # Status response
```

### Transfer Methods

**1. SCSI WRITE (Fast - 64KB chunks)**
```python
# ScsiWriteOp in usb.py line 147-149
struct.pack('>BBQIBB', 0x8A, 0, op.lba, sectors, 0, 0)
# 0x8A = SCSI WRITE(16) opcode
# lba = Logical Block Address (maps to PCI address 0x00200000 + lba*512)
# sectors = size / 512

# Chunking (usb.py lines 156-160)
for i in range(0, len(buf), 0x10000):  # 64 KB chunks
    self.exec_ops([ScsiWriteOp(buf[i:i+0x10000], lba), ...])
```

**2. E4 Read (SLOW - 255 byte chunks)**
```python
# ReadOp in usb.py line 143-146
struct.pack('>BBBHB', 0xE4, op.size, addr >> 16, addr & 0xFFFF, 0)
# op.size limited to 0xFF (255 bytes)

# Read method (usb.py line 165-167)
def read(self, base_addr, length, stride=0xff):  # stride = 255!
    parts = self.exec_ops([ReadOp(base_addr + off, min(stride, length - off))
                           for off in range(0, length, stride)])
```

**3. E5 Write (Register access - 1 byte at a time)**
```python
# WriteOp in usb.py line 136-141
struct.pack('>BBBHB', 0xE5, value, addr >> 16, addr & 0xFFFF, 0)
# Single byte per command - used for MMIO register writes, not bulk data
```

### Speed Comparison

| Operation | Chunk Size | Throughput | Use Case |
|-----------|------------|------------|----------|
| SCSI WRITE (0x8A) | 64 KB | ~100+ MB/s | Fast bulk OUT |
| E4 Read (0xE4) | 255 bytes | ~1-5 MB/s | **SLOW** bulk IN |
| E5 Write (0xE5) | 1 byte | ~10-50 KB/s | Register config |
| PCIe MemWr | 4 bytes | ~4 MB/s | GPU BAR writes |
| PCIe MemRd | 4 bytes | ~2 MB/s | GPU BAR reads |

**The asymmetry is severe**: Writes are ~20-100x faster than reads.

## The Firmware Patch

Tinygrad's `patch.py` downloads ASMedia firmware and makes one modification:

```python
patches = [(0x2a0d + 1 + 4, b'\x0a', b'\x05')]
# File offset: 0x2a12
# Changes: mov r7, #0x0a → mov r7, #0x05
```

### What The Patch Does

At code address 0x2a0d, there's a SCSI write completion status check:

```c
// From src/app/protocol.c lines 2250-2267
if (*ptr != 0) {
    goto return_0x0a;  // Return error code 10
}
// ...
return_0x0a:
    // NOTE: patch.py changes this return value from 0x0a to 0x05
    return 0x0a;  // Original returns 10
```

The patch changes this return value from 0x0a (10) to 0x05 (5).

**Why?** The return value is a status code that affects the SCSI state machine.
Code 10 (0x0a) indicates one type of completion status, while code 5 (0x05)
indicates another. The patch likely changes how partial/retry scenarios are
handled to make the write path more reliable for tinygrad's usage pattern.

### VID/PID Change

The patch also changes the device identity via config blocks:

```
Original:  VID=0x174C PID=0x2464 (ASMedia USB4 NVMe)
Patched:   VID=0xADD1 PID=0x0001 ("tiny")
```

This allows tinygrad to identify its patched devices.

## Internal SRAM Buffer

### Size: 6-8 MB

```
PCI Address       Purpose
0x00200000        Data buffer start (6+ MB available)
0x00820000        NVMe queues (can be repurposed)

Gap = 0x00620000 = 6.125 MB for data buffer
```

### LBA Addressing

SCSI commands use LBA (Logical Block Address) which firmware converts:

```
buffer_addr = 0x00200000 + (LBA × 512)
```

So `ScsiWriteOp(data, lba=256)` writes to PCI address:
```
0x00200000 + (256 × 512) = 0x00220000
```

### 32-bit DMA Addressing

The 8051 has 64KB address space but accesses the full buffer via DMA registers:

```
Register   Purpose
CE76-CE79  32-bit DMA address (little-endian)
```

USB bulk transfers program these registers and let hardware DMA handle the data.
The 8051 CPU never touches the bulk data bytes.

## The Slow Path Problem

### Why E4 Reads Are Slow

E4 is a vendor-specific SCSI command for register/memory reads:

1. **Limited to 255 bytes per request** (line 143: `assert op.size <= 0xff`)
2. **Control transfer overhead** - each E4 goes through SCSI CDB processing
3. **No bulk streaming** - cannot pipeline like SCSI WRITE does

### Why SCSI READ Isn't Used

Standard SCSI READ(16) command (opcode 0x88) isn't implemented in tinygrad.
The firmware supports it, but tinygrad only implemented SCSI WRITE.

### Comparison: SCSI WRITE vs E4 READ

```
SCSI WRITE (current - fast):
  1 command → 64KB data → 1 status
  ~100 MB/s

E4 READ (current - slow):
  1 command → 255 bytes data → 1 status
  × 4000 times for 1 MB
  ~1-5 MB/s
```

## Plan to Fix the Slow Path

### Option 1: Implement ScsiReadOp (Recommended)

Add SCSI READ(16) support to tinygrad, mirroring the write path:

```python
@dataclasses.dataclass(frozen=True)
class ScsiReadOp: size:int; lba:int=0

# In exec_ops:
elif isinstance(op, ScsiReadOp):
    sectors = round_up(op.size, 512) // 512
    _add_req(struct.pack('>BBQIBB', 0x88, 0, op.lba, sectors, 0, 0),
             sectors * 512, None)  # Request data IN

def scsi_read(self, size:int, lba:int=0) -> bytes:
    # Chunk reads like writes
    result = b''
    for i in range(0, size, 0x10000):
        chunk_size = min(0x10000, size - i)
        resp = self.exec_ops([ScsiReadOp(chunk_size, lba + i // 512)])
        result += resp[0][:chunk_size]
    return result
```

**Expected improvement:** 255 bytes → 64 KB per request = **~250x faster**

### Option 2: Batch E4 Requests

If firmware changes are easier than tinygrad changes:

```python
# Current: Sequential 255-byte reads
for off in range(0, length, 255):
    result += exec_ops([ReadOp(addr + off, 255)])

# Better: Parallel 255-byte reads (use all 31 streams)
reads = [ReadOp(addr + off, 255) for off in range(0, length, 255)]
results = exec_ops(reads[:31])  # 31 concurrent = 7.9KB per batch
```

**Expected improvement:** ~31x faster (still limited by E4 overhead)

### Option 3: Larger E4 Transfers (Firmware Change)

The 255-byte limit is arbitrary. Firmware could support larger E4 reads:

```c
// Current: uint8_t limits to 255
// Change: Use 16-bit length field in E4 command

// E4 format: 0xE4 [size_hi] [addr>>16] [addr_lo] [addr_hi] [size_lo]
// Allows up to 64KB per E4 request
```

**Expected improvement:** ~250x faster with firmware mod

### Option 4: Use GPU Bus Mastering

For large reads from GPU memory, use the GPU as bus master:

1. GPU DMAs data to PCI address 0x00200000 (bridge's SRAM)
2. Tinygrad reads from SRAM via SCSI READ or E4
3. Eliminates slow PCIe MemRd TLPs from 8051

**Expected improvement:** GPU-limited rather than bridge-limited

## Summary

| Path | Current | After Fix | Improvement |
|------|---------|-----------|-------------|
| USB → Device (SCSI WRITE) | 64 KB/cmd | 64 KB/cmd | Already fast |
| Device → USB (E4 READ) | 255 B/cmd | 64 KB/cmd | **~250x** |
| 8051 → GPU (PCIe MemWr) | 4 B/TLP | 4 B/TLP | Can't improve* |
| GPU → 8051 (PCIe MemRd) | 4 B/TLP | 4 B/TLP | Can't improve* |
| GPU ↔ SRAM (bus master) | N/A | ~4 GB/s | Use this! |

*The 8051-initiated PCIe path is fundamentally slow (~4 MB/s max). Use GPU
bus mastering for high-throughput data movement.

## Quick Reference

### Tinygrad Device Opening

```python
# From usb.py line 117
self.usb = USB3(0xADD1, 0x0001, 0x81, 0x83, 0x02, 0x04)
#              VID     PID     data_in stat_in data_out cmd_out
```

### Controller Initialization

```python
# usb.py lines 123-124
self.exec_ops([
    WriteOp(0x54b, b' '),        # Unknown config
    WriteOp(0x54e, b'\x04'),     # Unknown config
    WriteOp(0x5a8, b'\x02'),     # Unknown config
    WriteOp(0x5f8, b'\x04'),     # Unknown config
    WriteOp(0x7ec, b'\x01\x00\x00\x00'),  # 4-byte config
    WriteOp(0xc422, b'\x02'),    # Enable something
    WriteOp(0x0, b'\x33'),       # Start signal?
])
```

### PCIe Memory Access

```python
# usb.py lines 185-214
def pcie_request(self, fmt_type, address, value=None, size=4):
    # fmt_type: 0x60=MemWr, 0x20=MemRd
    # Writes to B210-B296 registers to generate TLPs

def pcie_mem_write(self, address, values, size):
    # Batch PCIe memory writes (4 on OSX, 16 on Linux per batch)
```
