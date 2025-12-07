# ASM2464PD Firmware Reimplementation - TODO

## Overview

| Metric | Value |
|--------|-------|
| Original firmware size | 98,012 bytes |
| Current implementation | 10,651 bytes |
| Completion | ~10.9% |
| Functions in original | ~660 |
| Functions implemented | ~200 |

The original firmware is approximately **9x larger** than our current implementation.

---

## Major Missing Subsystems

### 1. USB Protocol Stack (Partial)
The USB subsystem handles host communication, endpoint management, and data transfers.

**Implemented in `usb.c` (~40 functions):**
- `usb_enable` ✓, `usb_setup_endpoint` ✓
- `usb_ep_process` ✓, `usb_buffer_handler` ✓
- `usb_ep_config_bulk` ✓, `usb_ep_config_int` ✓
- `usb_set_transfer_flag` ✓, `usb_get_nvme_data_ctrl` ✓
- `usb_calc_queue_addr` ✓, `usb_calc_queue_addr_next` ✓
- `usb_set_done_flag` ✓, `usb_copy_status_to_buffer` ✓
- `usb_read_status_pair` ✓, `usb_read_transfer_params` ✓

**Implemented in `state_helpers.c` (stubs):**
- `usb_func_1b14` ✓, `usb_func_1b20` ✓, `usb_func_1b23` ✓
- `usb_reset_interface` ✓

**Remaining (~60 functions):**
- `usb_parse_descriptor` - Parse USB descriptors
- `usb_validate_descriptor` - Validate descriptor format
- `usb_endpoint_handler` - Main endpoint interrupt handler
- `usb_data_handler` - USB data stage handling
- `usb_configure` - USB configuration
- `usb_func_1aad` through `usb_func_1b60` - Various USB helpers (partial)

**Location in firmware:** 0x1A00-0x1E00, 0x5200-0x5500

### 2. NVMe Command Processing (Partial)
NVMe controller for SSD communication.

**Implemented in `nvme.c` (~25 functions):**
- `nvme_load_transfer_data` ✓, `nvme_calc_buffer_offset` ✓
- `nvme_subtract_idata_16` ✓, `nvme_inc_circular_counter` ✓
- `nvme_set_ep_queue_ctrl_84` ✓, `nvme_clear_status_bit1` ✓
- `nvme_add_to_global_053a` ✓, `nvme_set_data_ctrl_bit7` ✓
- `nvme_store_idata_16` ✓, `nvme_check_completion` ✓
- `nvme_set_usb_mode_bit` ✓, `nvme_get_config_offset` ✓
- `nvme_calc_idata_offset` ✓, `nvme_check_scsi_ctrl` ✓

**Remaining (~45 functions):**
- `nvme_submit_cmd` - Submit NVMe command (partial)
- `nvme_init_step` - Initialization step
- `nvme_process_cmd` - Process NVMe command
- `nvme_io_request` - I/O request handling
- `nvme_read_status` - Read status
- `nvme_initialize` - Full initialization
- `nvme_io_handler` - I/O handler

**Location in firmware:** 0x1B00-0x1D00, 0x3200-0x3600

### 3. DMA Engine (Mostly Complete)
Direct Memory Access for high-speed data transfers.

**Implemented in `dma.c` (~20 functions):**
- `dma_config_channel` ✓ - Configure DMA channel (0x4A80)
- `dma_start_transfer` ✓ - Start DMA transfer (0x4AB0)
- `dma_setup_transfer` ✓ - Setup transfer parameters (0x5270)
- `dma_check_scsi_status` ✓ - Check SCSI status (0x5290)
- `dma_clear_status` ✓, `dma_load_transfer_params` ✓
- `dma_clear_state_counters` ✓, `dma_init_ep_queue` ✓
- `scsi_get_tag_count_status` ✓, `scsi_get_queue_status` ✓

**Implemented in `state_helpers.c` (stubs):**
- `transfer_func_16a2` ✓, `transfer_func_16b7` ✓, `transfer_func_17ed` ✓

**Remaining (~15 functions):**
- `dma_transfer_handler` - Transfer completion handler (0x4D00)
- `dma_store_to_0a7d` - Store DMA parameters (0x1804)
- `transfer_func_1602` through `transfer_func_16cc` - Transfer helpers

**Location in firmware:** 0x1600-0x1800, 0x4A00-0x4E00

### 4. PCIe/Thunderbolt Interface (Partial)
PCIe passthrough and Thunderbolt tunneling.

**Implemented in `pcie.c` (~20 functions):**
- `pcie_init` ✓, `pcie_init_alt` ✓
- `pcie_poll_and_read_completion` ✓, `pcie_get_completion_status` ✓
- `pcie_get_link_speed` ✓, `pcie_set_byte_enables` ✓
- `pcie_read_completion_data` ✓, `pcie_write_status_complete` ✓
- `pcie_set_idata_params` ✓, `pcie_clear_address_regs` ✓
- `pcie_inc_txn_counters` ✓, `pcie_wait_for_completion` ✓

**Remaining (~20 functions):**
- `pcie_error_handler` - Error handling
- `handler_c105` - PCIe event handler (0xC105)
- `handler_ed02` - Bank 1 PCIe handler
- `handler_eef9` - Bank 1 error handler
- Various dispatch stubs (0x0570-0x0650)

**Location in firmware:** 0xC100-0xC400, 0xED00-0xEF00

### 5. Flash/SPI Interface (Mostly Complete)
SPI flash for firmware storage and configuration.

**Implemented in `flash.c` (~20 functions):**
- `flash_add_to_xdata16` ✓, `flash_write_word` ✓
- `flash_write_idata_word` ✓, `flash_write_r1_xdata_word` ✓
- `flash_poll_busy` ✓, `flash_set_cmd` ✓
- `flash_set_addr_md` ✓, `flash_set_addr_hi` ✓
- `flash_set_data_len` ✓, `flash_set_mode_enable` ✓
- `flash_start_transaction` ✓, `flash_run_transaction` ✓
- `flash_wait_and_poll` ✓, `flash_read_status` ✓

**Implemented in `state_helpers.c` (stubs):**
- `flash_func_0bc8` ✓, `flash_func_1679` ✓

**Remaining (~10 functions):**
- `flash_func_0adc` - Flash operation A
- `flash_func_0b15` - Flash operation B
- `flash_cmd_handler` - Command handler (0x0525 target)

**Location in firmware:** 0x0A00-0x0C00, 0xBA00-0xBB00

### 6. Protocol State Machine ✓ COMPLETED
Main protocol handling and state transitions.

**Implemented in `protocol.c`:**
- `protocol_dispatch` - Protocol dispatcher ✓
- `protocol_state_machine` - Main state machine (0x3900) ✓
- `handler_3adb` - State/event handler (0x3ADB) ✓
- `core_handler_4ff2` - Core processing (0x4FF2) ✓
- `protocol_init` - Initialization ✓

**Remaining (~30 functions):**
- `event_state_handler` - Event state handling
- `handler_2608` - State handler
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

### Phase 1: Core Functionality (Partial)
1. Complete USB endpoint handler (`usb_ep_handler` at 0x5442) - partial
2. Complete USB master handler (`usb_master_handler` at 0x10E0) - partial
3. ~~Implement `usb_ep_process` (0x52A7)~~ ✓ in usb.c
4. Implement NVMe command submission path - partial

### Phase 2: Data Transfer (Mostly Complete)
1. ~~DMA channel configuration and start~~ ✓ `dma_config_channel()`, `dma_start_transfer()` in dma.c
2. DMA transfer completion handling (partial - `dma_transfer_handler` still needed)
3. ~~SCSI/Mass Storage command processing~~ ✓ `scsi_send_csw()`, command handlers in scsi.c
4. NVMe I/O request handling (partial - high-level handlers still needed)

### Phase 3: Protocol ✓ COMPLETED
1. ~~Protocol state machine (0x3900)~~ ✓ `protocol_state_machine()` in protocol.c
2. ~~Event handling (0x3ADB)~~ ✓ `handler_3adb()` in protocol.c
3. ~~Core handler (0x4FF2)~~ ✓ `core_handler_4ff2()` in protocol.c

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
| USB | 100 | 25 | 75 |
| NVMe | 70 | 25 | 45 |
| DMA | 50 | 35 | 15 |
| PCIe/PHY | 40 | 12 | 28 |
| Flash | 30 | 16 | 14 |
| Protocol/State | 60 | 25 | 35 |
| Power | 20 | 4 | 16 |
| Utility | 80 | 25 | 55 |
| Timer/ISR | 30 | 11 | 19 |
| Bank 1 | 100 | 6 | 94 |
| **Total** | **660** | **199** | **461** |

### Memory Usage (Current)

```
CODE:  10,651 bytes / 98,012 bytes (10.9%)
XDATA: Registers defined, globals partially defined
IDATA: Work areas defined
```

### Key Files to Reference

- `ghidra.c` - Ghidra decompilation of all functions
- `fw.bin` - Original firmware binary
- `usb-to-pcie-re/ASM2x6x/doc/Notes.md` - Reverse engineering notes
- `usb.py` - Python library that talks to this chip
