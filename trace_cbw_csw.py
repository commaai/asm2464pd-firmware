#!/usr/bin/env python3
"""
Trace the stock firmware's complete CBW/CSW cycle.

This script:
1. Loads the stock firmware into the emulator
2. Boots the firmware and connects USB
3. Injects a TUR (Test Unit Ready) CBW command
4. Traces ALL reads/writes to MSC-related registers
5. Watches for the CSW response
6. Injects a SECOND TUR command to see re-arm sequence
7. Prints a complete trace log

Key registers monitored:
  0x9101 (USB_PERIPH_STATUS) - CBW received notification
  0x9118 (USB_EP_STATUS) - EP status
  0x9119-0x912E (CBW registers) - CBW data 
  0xCE88/0xCE89 (DMA handshake) - bulk DMA state machine
  0x90A1 (BULK_DMA_TRIGGER) - bulk IN trigger
  0xC42C/0xC42D (MSC CTRL/STATUS) - MSC engine
  0x900B (MSC_CFG) - MSC config
  0xC42A (NVME_DOORBELL) - NVMe doorbell
  0xD800-0xD80C (EP buffer) - CSW data
  0x901A (MSC_LENGTH) - transfer length
  0x9006 (EP0_CONFIG) - bulk ready
  0x9093/0x9094 (EP_CFG1/2) - endpoint arming
"""

import sys
import os

# Add project root and emulate dir to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'emulate'))

from emulate.emu import Emulator

# ============================================================================
# Trace log
# ============================================================================
trace_log = []
phase_markers = []

def log(msg):
    """Print and store a trace message."""
    print(msg)
    trace_log.append(msg)

def mark_phase(name):
    """Mark a phase boundary in the trace."""
    line = f"\n{'='*70}\n  PHASE: {name}\n{'='*70}"
    log(line)
    phase_markers.append((len(trace_log), name))


# ============================================================================
# Register name table for the monitored ranges
# ============================================================================
REG_NAMES = {
    0x9000: "USB_STATUS",
    0x9001: "USB_CONTROL",
    0x9002: "USB_CONFIG",
    0x9003: "EP0_STATUS",
    0x9004: "EP0_LEN_L",
    0x9005: "EP0_LEN_H",
    0x9006: "EP0_CONFIG",
    0x9007: "SCSI_BUF_LEN_L",
    0x9008: "SCSI_BUF_LEN_H",
    0x900B: "MSC_CFG",
    0x9010: "USB_DATA_L",
    0x9011: "USB_DATA_H",
    0x9012: "USB_FIFO_STATUS",
    0x9018: "USB_XCVR_MODE",
    0x9019: "USB_MODE_9019",
    0x901A: "MSC_LENGTH",
    0x905A: "EP_CFG_905A",
    0x905B: "EP_BUF_HI",
    0x905C: "EP_BUF_LO",
    0x905D: "EP_CTRL_905D",
    0x905E: "EP_MGMT",
    0x905F: "EP_CTRL_905F",
    0x9090: "INT_MASK_9090",
    0x9091: "CTRL_PHASE",
    0x9092: "DMA_TRIGGER",
    0x9093: "EP_CFG1",
    0x9094: "EP_CFG2",
    0x9096: "EP_READY",
    0x90A0: "CTRL_90A0",
    0x90A1: "BULK_DMA_TRIGGER",
    0x90E0: "USB_SPEED",
    0x90E1: "SW_DMA_TRIGGER",
    0x90E2: "USB_MODE",
    0x90E3: "EP_STATUS_90E3",
    0x9100: "LINK_STATUS",
    0x9101: "PERIPH_STATUS",
    0x9118: "EP_STATUS",
    0x9119: "CBW_LEN_HI",
    0x911A: "CBW_LEN_LO",
    0x911B: "CBW_SIG0(U)",
    0x911C: "CBW_SIG1(S)",
    0x911D: "CBW_SIG2(B)",
    0x911E: "CBW_SIG3(C)",
    0x911F: "CBW_TAG_0",
    0x9120: "CBW_TAG_1",
    0x9121: "CBW_TAG_2",
    0x9122: "CBW_TAG_3",
    0x9123: "CBW_XFER_LEN_0",
    0x9124: "CBW_XFER_LEN_1",
    0x9125: "CBW_XFER_LEN_2",
    0x9126: "CBW_XFER_LEN_3",
    0x9127: "CBW_FLAGS",
    0x9128: "CBW_LUN",
    0x9129: "CBW_CB_LEN",
    0x912A: "CBWCB_0(opcode)",
    0x912B: "CBWCB_1",
    0x912C: "CBWCB_2",
    0x912D: "CBWCB_3",
    0x912E: "CBWCB_4",
    0x912F: "CBWCB_5",
    0xC400: "NVME_C400",
    0xC412: "NVME_READY",
    0xC42A: "NVME_DOORBELL",
    0xC42C: "MSC_CTRL",
    0xC42D: "MSC_STATUS",
    0xC471: "NVME_QUEUE_BUSY",
    0xC47A: "NVME_CMD_STATUS",
    0xC47B: "NVME_C47B",
    0xC4C0: "SCSI_CMD_BUF_0",
    0xC4C1: "SCSI_CMD_BUF_1",
    0xC4C2: "SCSI_CMD_BUF_2",
    0xC4C3: "SCSI_CMD_BUF_3",
    0xC4EC: "EP_STATUS_C4EC",
    0xC4ED: "EP_INDEX_C4ED",
    0xCE00: "SCSI_DMA_CTRL",
    0xCE55: "SCSI_DMA_XFER_CNT",
    0xCE5D: "DEBUG_MASK",
    0xCE6C: "XFER_STATUS_CE6C",
    0xCE86: "USB_DMA_ERROR",
    0xCE88: "BULK_DMA_HANDSHAKE",
    0xCE89: "USB_DMA_STATE",
    0xCEB0: "CEB0",
    0xCEB2: "CEB2",
    0xCEB3: "CEB3",
    0xD800: "EP_BUF_CTRL/CSW_U",
    0xD801: "EP_BUF_SEL/CSW_S",
    0xD802: "EP_BUF_DATA/CSW_B",
    0xD803: "EP_BUF_PTR_LO/CSW_S",
    0xD804: "CSW_TAG_0",
    0xD805: "CSW_TAG_1",
    0xD806: "EP_BUF_STATUS/CSW_TAG_2",
    0xD807: "EP_BUF_LEN_HI/CSW_TAG_3",
    0xD808: "CSW_RESIDUE_0",
    0xD809: "CSW_RESIDUE_1",
    0xD80A: "CSW_RESIDUE_2",
    0xD80B: "CSW_RESIDUE_3",
    0xD80C: "CSW_STATUS",
    0xC8D4: "DMA_CONFIG_C8D4",
    0xC509: "C509",
}

def reg_name(addr):
    if addr in REG_NAMES:
        return REG_NAMES[addr]
    return f"0x{addr:04X}"


# ============================================================================
# Setup emulator
# ============================================================================
log("=" * 70)
log("  CBW/CSW Cycle Trace — Stock Firmware (fw_tinygrad.bin)")
log("=" * 70)

emu = Emulator(trace=False, log_hw=False, log_uart=True, usb_delay=999999999)
emu.load_firmware('fw_tinygrad.bin')
emu.reset()

# ============================================================================
# Install comprehensive MMIO tracing hooks
# ============================================================================
# We'll install custom read/write hooks for ALL registers of interest,
# wrapping the existing hooks to add our trace logging.

# Store original hooks before wrapping
orig_read_hooks = {}
orig_write_hooks = {}

# Ranges to monitor
MONITOR_RANGES = [
    (0x9000, 0x9200),  # USB core + CBW regs
    (0xC400, 0xC500),  # NVMe/MSC
    (0xCE80, 0xCE90),  # DMA handshake
    (0x90A0, 0x90F0),  # Bulk DMA, EP config
    (0xD800, 0xD810),  # EP buffer / CSW
    # Additional individual registers
]

# Extra individual addresses
EXTRA_ADDRS = [
    0xCE00, 0xCE55, 0xCE5D, 0xCE6C, 0xCEB0, 0xCEB2, 0xCEB3,
    0xC8D4, 0xC509,
    0x9220, 0x9300, 0x9301, 0x9302,
]

# Collect all monitored addresses
monitored_addrs = set()
for start, end in MONITOR_RANGES:
    for a in range(start, end):
        monitored_addrs.add(a)
for a in EXTRA_ADDRS:
    monitored_addrs.add(a)

# Read/write counters per address
read_counts = {}
write_counts = {}

def make_traced_read(emu_ref, addr, original_hook):
    """Create a traced read hook wrapping the original."""
    def traced_read(a):
        if original_hook:
            val = original_hook(a)
        else:
            val = emu_ref.hw.regs.get(a, 0x00)
        
        read_counts[a] = read_counts.get(a, 0) + 1
        name = reg_name(a)
        pc = emu_ref.cpu.pc
        bank = emu_ref.memory.read_sfr(0x96) & 1
        cyc = emu_ref.hw.cycles
        log(f"[{cyc:8d}] READ  {name:25s} (0x{a:04X}) = 0x{val:02X}  PC=0x{pc:04X} bank={bank}  #{read_counts[a]}")
        return val
    return traced_read

def make_traced_write(emu_ref, addr, original_hook):
    """Create a traced write hook wrapping the original."""
    def traced_write(a, v):
        write_counts[a] = write_counts.get(a, 0) + 1
        name = reg_name(a)
        pc = emu_ref.cpu.pc
        bank = emu_ref.memory.read_sfr(0x96) & 1
        cyc = emu_ref.hw.cycles
        old_val = emu_ref.hw.regs.get(a, 0x00)
        log(f"[{cyc:8d}] WRITE {name:25s} (0x{a:04X}) = 0x{v:02X} (was 0x{old_val:02X})  PC=0x{pc:04X} bank={bank}  #{write_counts[a]}")
        
        if original_hook:
            original_hook(a, v)
        else:
            emu_ref.hw.regs[a] = v
    return traced_write

# Install hooks
for addr in monitored_addrs:
    # Save originals
    orig_read = emu.memory.xdata_read_hooks.get(addr)
    orig_write = emu.memory.xdata_write_hooks.get(addr)
    
    # Install traced wrappers
    emu.memory.xdata_read_hooks[addr] = make_traced_read(emu, addr, orig_read)
    emu.memory.xdata_write_hooks[addr] = make_traced_write(emu, addr, orig_write)

log(f"\nInstalled trace hooks on {len(monitored_addrs)} addresses")

# ============================================================================
# Phase 1: Boot firmware, let it initialize
# ============================================================================
mark_phase("FIRMWARE BOOT (no USB)")

# Run enough cycles for basic initialization (no USB connected yet)
emu.run(max_instructions=500000)
log(f"\nAfter boot: PC=0x{emu.cpu.pc:04X}, {emu.inst_count} instructions, {emu.hw.cycles} cycles")

# ============================================================================
# Phase 2: Connect USB
# ============================================================================
mark_phase("USB CONNECT")

# Manually connect USB (we disabled auto-connect by setting huge delay)
emu.hw.usb_connected = True
emu.hw.usb_controller.connect(speed=2)  # SuperSpeed

# Set up PCIe state needed for MSC
emu.hw.regs[0xB480] = 0x03
emu.memory.xdata[0x0AF7] = 0x01
emu.memory.xdata[0x053F] = 0x01
emu.memory.xdata[0x05A3] = 0x00
emu.memory.xdata[0x05B1] = 0x03

# Trigger interrupt
ie = emu.memory.read_sfr(0xA8)
ie |= 0x81  # EA + EX0
emu.memory.write_sfr(0xA8, ie)
emu.cpu._ext0_pending = True

# Run through USB enumeration
emu.run(max_instructions=500000)
log(f"\nAfter USB connect: PC=0x{emu.cpu.pc:04X}, {emu.inst_count} instructions")

# Clear trace logs from boot/enumeration - we only care about CBW/CSW
boot_trace_count = len(trace_log)
boot_reads = dict(read_counts)
boot_writes = dict(write_counts)
read_counts.clear()
write_counts.clear()

# ============================================================================
# Phase 3: Inject TUR CBW #1
# ============================================================================
mark_phase("INJECT CBW #1 — TEST UNIT READY (opcode 0x00)")

def inject_tur_cbw(emu, tag=0x00000001):
    """
    Inject a Test Unit Ready CBW into the MMIO registers.
    
    CBW format (31 bytes):
      Bytes 0-3:   dCBWSignature = "USBC" (0x55534243)
      Bytes 4-7:   dCBWTag (echoed in CSW)
      Bytes 8-11:  dCBWDataTransferLength = 0 (TUR has no data)
      Byte 12:     bmCBWFlags = 0x00 (no data direction)
      Byte 13:     bCBWLUN = 0x00
      Byte 14:     bCBWCBLength = 0x06
      Bytes 15-30: CBWCB (0x00 = TEST UNIT READY, rest zeros)
    
    Hardware populates registers 0x9119-0x912E when a CBW arrives.
    """
    log(f"  Setting up CBW registers for TUR (tag=0x{tag:08X})...")
    
    # CBW signature: "USBC"
    emu.hw.regs[0x911B] = 0x55  # 'U'
    emu.hw.regs[0x911C] = 0x53  # 'S'  
    emu.hw.regs[0x911D] = 0x42  # 'B'
    emu.hw.regs[0x911E] = 0x43  # 'C'
    
    # CBW Tag (4 bytes)
    emu.hw.regs[0x911F] = (tag >> 0) & 0xFF
    emu.hw.regs[0x9120] = (tag >> 8) & 0xFF
    emu.hw.regs[0x9121] = (tag >> 16) & 0xFF
    emu.hw.regs[0x9122] = (tag >> 24) & 0xFF
    
    # dCBWDataTransferLength = 0 (TUR has no data phase)
    emu.hw.regs[0x9123] = 0x00
    emu.hw.regs[0x9124] = 0x00
    emu.hw.regs[0x9125] = 0x00
    emu.hw.regs[0x9126] = 0x00
    
    # bmCBWFlags = 0x00 (no data)
    emu.hw.regs[0x9127] = 0x00
    
    # bCBWLUN = 0
    emu.hw.regs[0x9128] = 0x00
    
    # bCBWCBLength = 6
    emu.hw.regs[0x9129] = 0x06
    
    # CBWCB[0] = 0x00 (TEST UNIT READY)
    emu.hw.regs[0x912A] = 0x00
    emu.hw.regs[0x912B] = 0x00
    emu.hw.regs[0x912C] = 0x00
    emu.hw.regs[0x912D] = 0x00
    emu.hw.regs[0x912E] = 0x00
    emu.hw.regs[0x912F] = 0x00
    
    # CBW transfer length high/low (read by firmware at 0x9119/0x911A)
    emu.hw.regs[0x9119] = 0x00
    emu.hw.regs[0x911A] = 0x00
    
    # EP status (non-zero means endpoints active)
    emu.hw.regs[0x9118] = 0x01
    
    # Set 0x9101 bit 6 = CBW received
    # This is the primary trigger - the ISR checks bit 6 to dispatch CBW handler
    emu.hw.regs[0x9101] = 0x40
    
    # USB connected, bit 0 = data path status
    emu.hw.regs[0x9000] = 0x81
    
    # USB interrupt pending
    emu.hw.regs[0xC802] = 0x05
    
    # USB speed = SuperSpeed
    emu.hw.regs[0x90E0] = 0x02
    emu.hw.regs[0x9100] = 0x02
    
    # USB state = configured
    emu.memory.idata[0x6A] = 5
    
    # CDB in alternate location (firmware may read from here too)
    emu.hw.regs[0x910D] = 0x00  # opcode
    emu.hw.regs[0x910E] = 0x00
    
    # DMA state ready for new transfer
    emu.hw.usb_ce89_read_count = 0
    emu.hw.usb_cmd_pending = True
    emu.hw.usb_cmd_type = 0  # Not E4/E5, it's a SCSI TUR
    
    # MSC engine state - make sure it's ready
    # C42D should be in a state where MSC is ready to accept
    emu.hw.regs[0xC42D] = 0x00
    
    # C8D4 = 0x00 for NVMe DMA mode (CSW via C42C)  
    emu.hw.regs[0xC8D4] = 0x00
    
    # NVMe queue busy (needed for 90A1 path)
    emu.hw.regs[0xC471] = 0x01
    
    # Ensure PCIe link is up
    emu.hw.regs[0xB480] = 0x03
    emu.memory.xdata[0x0AF7] = 0x01
    
    # Trigger External Interrupt 0
    ie = emu.memory.read_sfr(0xA8)
    ie |= 0x81  # EA + EX0
    emu.memory.write_sfr(0xA8, ie)
    emu.cpu._ext0_pending = True
    
    log(f"  CBW injected. Triggering EX0 interrupt...")


inject_tur_cbw(emu, tag=0x00000001)

# ============================================================================
# Phase 4: Run until CSW is sent (or timeout)
# ============================================================================
mark_phase("PROCESSING CBW #1 — Watching for CSW")

# We need to detect when the CSW is written to D800-D80C
# The CSW signature is "USBS" at D800-D803
csw_detected = [False]
csw_cycle = [0]

# Track the D800-D803 writes to detect CSW signature
csw_sig_bytes = {}

# Override D800-D803 write hooks to detect CSW
orig_d800_hooks = {}
for addr in range(0xD800, 0xD80D):
    orig_d800_hooks[addr] = emu.memory.xdata_write_hooks.get(addr)

def make_csw_detect_write(emu_ref, addr, original_hook):
    def hook(a, v):
        write_counts[a] = write_counts.get(a, 0) + 1
        name = reg_name(a)
        pc = emu_ref.cpu.pc
        bank = emu_ref.memory.read_sfr(0x96) & 1
        cyc = emu_ref.hw.cycles
        old_val = emu_ref.hw.regs.get(a, 0x00) if a >= 0x6000 else emu_ref.hw.usb_ep_data_buf[a - 0xD800]
        log(f"[{cyc:8d}] WRITE {name:25s} (0x{a:04X}) = 0x{v:02X} (was 0x{old_val:02X})  PC=0x{pc:04X} bank={bank}  #{write_counts[a]}")
        
        # Track CSW signature bytes
        csw_sig_bytes[a] = v
        
        # Check if USBS signature has been written
        if (csw_sig_bytes.get(0xD800) == 0x55 and  # 'U'
            csw_sig_bytes.get(0xD801) == 0x53 and  # 'S'
            csw_sig_bytes.get(0xD802) == 0x42 and  # 'B'
            csw_sig_bytes.get(0xD803) == 0x53):    # 'S'
            if not csw_detected[0]:
                csw_detected[0] = True
                csw_cycle[0] = cyc
                log(f"\n  *** CSW SIGNATURE DETECTED (USBS) at cycle {cyc}! ***\n")
        
        if original_hook:
            original_hook(a, v)
        else:
            emu_ref.hw.usb_ep_data_buf[a - 0xD800] = v
    return hook

for addr in range(0xD800, 0xD80D):
    emu.memory.xdata_write_hooks[addr] = make_csw_detect_write(emu, addr, orig_d800_hooks[addr])

# Run firmware to process CBW #1
# Run in chunks to monitor progress
max_chunks = 20
chunk_size = 100000
total_inst = 0

for chunk in range(max_chunks):
    before_inst = emu.inst_count
    emu.run(max_instructions=emu.inst_count + chunk_size)
    ran = emu.inst_count - before_inst
    total_inst += ran
    
    if csw_detected[0]:
        log(f"\n  CSW #1 detected after {total_inst} instructions")
        # Run a bit more to see re-arm sequence
        emu.run(max_instructions=emu.inst_count + 50000)
        break
    
    if ran < chunk_size:
        log(f"  Firmware halted after {total_inst} instructions, PC=0x{emu.cpu.pc:04X}")
        break

if not csw_detected[0]:
    log(f"\n  WARNING: CSW NOT DETECTED after {total_inst} instructions")
    log(f"  PC=0x{emu.cpu.pc:04X}")
    log(f"  D800-D80C: {' '.join(f'{emu.hw.usb_ep_data_buf[i]:02X}' for i in range(13))}")

# ============================================================================
# Phase 5: Inject TUR CBW #2
# ============================================================================
mark_phase("INJECT CBW #2 — SECOND TEST UNIT READY")

# Reset tracking
csw_detected[0] = False
csw_sig_bytes.clear()
read_counts_before_cbw2 = dict(read_counts)
write_counts_before_cbw2 = dict(write_counts)

inject_tur_cbw(emu, tag=0x00000002)

# ============================================================================
# Phase 6: Run until second CSW
# ============================================================================
mark_phase("PROCESSING CBW #2 — Watching for second CSW")

total_inst_2 = 0
for chunk in range(max_chunks):
    before_inst = emu.inst_count
    emu.run(max_instructions=emu.inst_count + chunk_size)
    ran = emu.inst_count - before_inst
    total_inst_2 += ran
    
    if csw_detected[0]:
        log(f"\n  CSW #2 detected after {total_inst_2} instructions")
        emu.run(max_instructions=emu.inst_count + 50000)
        break
    
    if ran < chunk_size:
        log(f"  Firmware halted after {total_inst_2} instructions, PC=0x{emu.cpu.pc:04X}")
        break

if not csw_detected[0]:
    log(f"\n  WARNING: CSW #2 NOT DETECTED after {total_inst_2} instructions")

# ============================================================================
# Summary
# ============================================================================
mark_phase("SUMMARY")

log(f"\nTotal instructions: {emu.inst_count}")
log(f"Total cycles: {emu.hw.cycles}")

# Print read/write counts for key registers
log(f"\n{'='*70}")
log(f"  Register Access Summary")
log(f"{'='*70}")
log(f"{'Register':30s} {'Reads':>8s} {'Writes':>8s}")
log(f"{'-'*30} {'-'*8} {'-'*8}")

# Sort by register address
all_addrs = sorted(set(list(read_counts.keys()) + list(write_counts.keys())))
for addr in all_addrs:
    r = read_counts.get(addr, 0)
    w = write_counts.get(addr, 0)
    if r > 0 or w > 0:
        name = f"{reg_name(addr)} (0x{addr:04X})"
        log(f"{name:30s} {r:>8d} {w:>8d}")

# Dump final CSW buffer state
log(f"\nFinal EP buffer (0xD800-0xD80C):")
for i in range(13):
    val = emu.hw.usb_ep_data_buf[i]
    log(f"  0x{0xD800+i:04X}: 0x{val:02X}  ({chr(val) if 0x20 <= val < 0x7f else '.'})")

# ============================================================================
# Write full trace to file
# ============================================================================
with open('trace_cbw_csw.log', 'w') as f:
    for line in trace_log:
        f.write(line + '\n')

log(f"\nFull trace written to trace_cbw_csw.log ({len(trace_log)} lines)")
