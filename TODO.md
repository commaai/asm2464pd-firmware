# ASM2464PD Firmware Reimplementation - TODO

## Overview

| Metric | Value |
|--------|-------|
| Original firmware size | 98,012 bytes |
| Current implementation | 9,367 bytes |
| Completion | ~9.5% |
| Functions in original | ~785 |
| Functions implemented | ~170 |

The original firmware is approximately **11x larger** than our current implementation.

---

## Major Missing Subsystems

### 1. USB Protocol Stack (High Priority)
The USB subsystem handles host communication, endpoint management, and data transfers.

**Missing Functions (~80 functions):**
- `usb_parse_descriptor` - Parse USB descriptors
- `usb_validate_descriptor` - Validate descriptor format
- `usb_endpoint_handler` - Main endpoint interrupt handler
- `usb_data_handler` - USB data stage handling
- `usb_setup_data_xfer` - Setup data transfer
- `usb_get_xfer_status` - Get transfer status
- `usb_configure` - USB configuration
- `usb_check_status` - Status checking
- `usb_func_1aad` through `usb_func_1b60` - Various USB helpers
- `usb_ep_init_handler` - Endpoint initialization
- `usb_ep_process` - Endpoint processing (0x52A7)
- `usb_set_endpoint_config` - Configure endpoints

**Location in firmware:** 0x1A00-0x1E00, 0x5200-0x5500

### 2. NVMe Command Processing (High Priority)
NVMe controller for SSD communication.

**Missing Functions (~60 functions):**
- `nvme_submit_cmd` - Submit NVMe command (partial)
- `nvme_load_transfer_data` - Load transfer data
- `nvme_calc_buffer_offset` - Calculate buffer offsets
- `nvme_subtract_idata_16` - 16-bit subtraction
- `nvme_inc_circular_counter` - Queue pointer management
- `nvme_set_ep_queue_ctrl_84` - Queue control
- `nvme_clear_status_bit1` - Status clearing
- `nvme_add_to_global_053a` - Add to counter
- `nvme_set_data_ctrl_bit7` - Data control
- `nvme_store_idata_16` - Store 16-bit value
- `nvme_init_step` - Initialization step
- `nvme_check_completion` - Check command completion
- `nvme_process_cmd` - Process NVMe command
- `nvme_io_request` - I/O request handling
- `nvme_read_status` - Read status
- `nvme_initialize` - Full initialization
- `nvme_io_handler` - I/O handler
- `nvme_util_check_command_ready` - Command ready check
- `nvme_util_get_queue_depth` - Get queue depth
- `nvme_util_advance_queue` - Advance queue pointer

**Location in firmware:** 0x1B00-0x1D00, 0x3200-0x3600

### 3. DMA Engine (High Priority)
Direct Memory Access for high-speed data transfers.

**Missing Functions (~40 functions):**
- `dma_config_channel` - Configure DMA channel (0x4A80)
- `dma_start_transfer` - Start DMA transfer (0x4AB0)
- `dma_transfer_handler` - Transfer completion handler (0x4D00)
- `dma_setup_transfer` - Setup transfer parameters (0x5270)
- `dma_check_scsi_status` - Check SCSI status (0x5290)
- `dma_store_to_0a7d` - Store DMA parameters (0x1804)
- `transfer_func_1602` through `transfer_func_16cc` - Transfer helpers
- `transfer_func_16ae`, `transfer_func_16b0` - More helpers
- `transfer_func_16f6` - Status clearing
- `transfer_func_17d8`, `transfer_func_17e3`, `transfer_func_17ed` - Late helpers

**Location in firmware:** 0x1600-0x1800, 0x4A00-0x4E00

### 4. PCIe/Thunderbolt Interface (Medium Priority)
PCIe passthrough and Thunderbolt tunneling.

**Missing Functions (~30 functions):**
- `pcie_error_handler` - Error handling
- `handler_c105` - PCIe event handler (0xC105)
- `phy_register_config` - PHY register configuration
- `phy_config_link_params` - Link parameter configuration (partial)
- `handler_ed02` - Bank 1 PCIe handler
- `handler_eef9` - Bank 1 error handler
- Various dispatch stubs (0x0570-0x0650)

**Location in firmware:** 0xC100-0xC400, 0xED00-0xEF00

### 5. Flash/SPI Interface (Medium Priority)
SPI flash for firmware storage and configuration.

**Missing Functions (~25 functions):**
- `flash_func_0adc` - Flash operation A
- `flash_func_0b15` - Flash operation B
- `flash_add_to_xdata16` - Add to flash address
- `flash_write_byte` - Write byte to flash
- `flash_write_idata` - Write from IDATA
- `flash_write_r1_xdata` - Write via R1
- `flash_func_0bc8` - Flash helper
- `flash_cmd_handler` - Command handler (0x0525 target)

**Location in firmware:** 0x0A00-0x0C00, 0xBA00-0xBB00

### 6. Protocol State Machine (Medium Priority)
Main protocol handling and state transitions.

**Missing Functions (~40 functions):**
- `protocol_dispatch` - Protocol dispatcher (0x0458)
- `protocol_state_machine` - Main state machine (0x3900)
- `event_state_handler` - Event state handling
- `handler_2608` - State handler
- `handler_3adb` - State/event handler
- `core_handler_4ff2` - Core processing
- Various `FUN_CODE_xxxx` state helpers

**Location in firmware:** 0x3900-0x3E00, 0x4F00-0x5100

### 7. Power Management (Low Priority)
Power state management and sleep modes.

**Missing Functions (~15 functions):**
- `power_set_state` - Set power state
- `power_check_status` - Check power status
- `usb_power_init` - USB power initialization
- Various power-related helpers

**Location in firmware:** 0x0080-0x0100, 0xB100-0xB200

### 8. Error Handling and Logging (Low Priority)
Error detection, handling, and debug output.

**Missing Functions (~20 functions):**
- `error_handler_e911` - Bank 1 error handler
- `error_handler_a066` - Bank 1 error handler
- `error_handler_ef4e` - Bank 1 error handler
- `debug_output_handler` - Debug output (0xAF5E)
- `delay_function` - Timing delays

**Location in firmware:** 0xE900-0xEF00 (Bank 1)

---

## Dispatch Stubs (0x0300-0x0650)

There are approximately **80 dispatch stubs** in the 0x0300-0x0650 region that route to handlers in various banks. Most are 4-5 bytes each:

```
FUN_CODE_0322 -> dispatches to ???
FUN_CODE_032c -> dispatches to 0x92C5
FUN_CODE_0331 -> dispatches to ???
...
FUN_CODE_0633 -> dispatches to ???
FUN_CODE_0638 -> dispatches to ???
handler_063d -> dispatches to 0xEEF9
```

**Status:** Only ~15 dispatch stubs are implemented.

---

## Bank 1 Functions (0x10000-0x17F0C)

Bank 1 contains ~32KB of code accessed via DPX=1. Major functions:

| Address | Function | Status |
|---------|----------|--------|
| 0xE911 | error_handler_e911 | Stub only |
| 0xA066 | error_handler_a066 | Stub only |
| 0xEF4E | error_handler_ef4e | Stub only |
| 0xED02 | handler_ed02 | Not implemented |
| 0xEEF9 | handler_eef9 | Not implemented |
| 0xE762 | FUN_CODE_e762 | Not implemented |
| 0xE677 | FUN_CODE_e677 | Not implemented |
| 0xE2A6 | FUN_CODE_e2a6 | Not implemented |
| 0xE91D | FUN_CODE_e91d | Not implemented |
| 0xE902 | FUN_CODE_e902 | Not implemented |

---

## Data Tables

The firmware contains several data tables that need to be implemented:

| Address | Size | Description | Status |
|---------|------|-------------|--------|
| 0x0648 | ~200 | Initialization table | Partial |
| 0x5A6A | 256 | EP index lookup table | Implemented |
| 0x5B6A | 8 | EP bit mask table | Implemented |
| 0x5B72 | 8 | EP offset table | Implemented |
| 0x5B7A | ~100 | Additional EP tables | Not implemented |
| 0x7000 | 4096 | Flash buffer | Defined |
| 0xF000 | 4096 | USB buffer | Defined |

---

## Priority Implementation Order

### Phase 1: Core Functionality
1. Complete USB endpoint handler (`usb_ep_handler` at 0x5442)
2. Complete USB master handler (`usb_master_handler` at 0x10E0)
3. Implement `usb_ep_process` (0x52A7)
4. Implement NVMe command submission path

### Phase 2: Data Transfer
1. DMA channel configuration and start
2. DMA transfer completion handling
3. SCSI/Mass Storage command processing
4. NVMe I/O request handling

### Phase 3: Protocol
1. Protocol state machine (0x3900)
2. Event handling (0x3ADB)
3. Core handler (0x4FF2)

### Phase 4: Error Handling & Misc
1. Bank 1 error handlers
2. Power management
3. Debug output

---

## Notes

### Functions by Category (approximate counts)

| Category | Total | Implemented | Remaining |
|----------|-------|-------------|-----------|
| Dispatch stubs | 80 | 15 | 65 |
| USB | 100 | 20 | 80 |
| NVMe | 70 | 10 | 60 |
| DMA | 50 | 15 | 35 |
| PCIe/PHY | 40 | 12 | 28 |
| Flash | 30 | 16 | 14 |
| Protocol/State | 60 | 10 | 50 |
| Power | 20 | 4 | 16 |
| Utility | 80 | 20 | 60 |
| Timer/ISR | 30 | 11 | 19 |
| Bank 1 | 100 | 6 | 94 |
| **Total** | **660** | **139** | **521** |

### Memory Usage (Current)

```
CODE:  8,949 bytes / 98,012 bytes (9.1%)
XDATA: Registers defined, globals partially defined
IDATA: Work areas defined
```

### Key Files to Reference

- `ghidra.c` - Ghidra decompilation of all functions
- `fw.bin` - Original firmware binary
- `usb-to-pcie-re/ASM2x6x/doc/Notes.md` - Reverse engineering notes
- `usb.py` - Python library that talks to this chip
