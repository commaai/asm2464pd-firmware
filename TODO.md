# ASM2464PD Firmware - Remaining Work

## Progress Summary

| Metric | Value |
|--------|-------|
| **Firmware Size** | 48,916 / 98,012 bytes (49.9%) |
| **Completed Ranges** | 0x1000-0x1FFF, 0x2000-0x3FFF, 0x4000-0x5FFF, 0x9000-0x98FF, 0xA000-0xBFFF, 0xE000-0xFFFF |
| **Major Gaps** | 0x8000-0x8FFF, 0x9900-0x9FFF, 0xC000-0xDFFF |

Note: SDCC generates different code than the original Keil C51 compiler, so byte-exact matching is not possible. Function-level correctness is the goal.

---

## 1. High Priority Functions (10+ calls)

These are the most frequently called functions and should be prioritized:

| Address | Calls | Current Status | Notes |
|---------|-------|----------------|-------|
| 0xaa37 | 18 | ✓ DONE | Part of nvme_cmd_state_handler_aa36 |
| 0xd1e6 | 14 | TODO | Event handler |
| 0xd1f2 | 14 | TODO | Event handler |
| 0xdd12 | 11 | STUB | helper_dd12 - needs full implementation |
| 0xd5da | 10 | TODO | Power/event handler |
| 0xc2e7 | 8 | TODO | Error handler |
| 0xc2f8 | 6 | TODO | Error handler |
| 0xd185 | 6 | TODO | Event handler |
| 0xce23 | 5 | STUB | transfer_handler_ce23 |
| 0xceab | 5 | TODO | Transfer handler |

---

## 2. PCIe State Machine Complex (0x9700-0x98FF) ✓ COMPLETE

Implemented as `cfg_pcie_ep_state_machine()` in cmd.c covering 0x9741-0x9901 with all entry points.

### Entry Points (all implemented)
- [x] 0x9777 (6 calls) - Mask A to 0x0F, call helpers, compare
- [x] 0x976e (5 calls) - Inc r2; anl a,r4; read @dptr; check bit 4 (overlapping code)
- [x] 0x97bd (5 calls) - State transition logic (overlapping code)
- [x] 0x97c9 (5 calls) - State transition logic (overlapping code)
- [x] 0x97fc (5 calls) - State transition logic
- [x] 0x984d (7 calls) - Mid-loop: subb a,r1; xrl a,r2; jnc
- [x] 0x9854 (7 calls) - Read DPTR, add 3, call 0x99b5
- [x] 0x9874 (5 calls) - State machine continuation
- [x] 0x9887 (5 calls) - State machine continuation

### Helper Functions (0x9700-0x9740)
- [x] cmd_clear_trigger_bits (0x96f7-0x9702)
- [x] cmd_write_trigger_wait (0x9703-0x9712)
- [x] cmd_config_e400_e420 (0x9713-0x971d)
- [x] cmd_setup_e424_e425 (0x971e-0x9728)
- [x] cmd_set_trigger_bit6 (0x9729-0x972f)
- [x] cmd_call_dd12_config (0x9730-0x9739)
- [x] cmd_extract_bits67_write (0x973a-0x9740)

---

## 3. Queue & Admin Command Handlers (0xA000-0xBFFF) ✓ COMPLETE

**Coverage: 97% (309/317 call targets)**

This range handles NVMe admin commands, queue management, and buffer operations.
Implementations are spread across queue_handlers.c, nvme.c, pcie.c, utils.c, and stubs.c.

### High Priority (all implemented)
- [x] 0xaa37 (18 calls) - Part of nvme_cmd_state_handler_aa36 in queue_handlers.c
- [x] 0xa2f8, 0xa2f9 (4 calls each) - Mid-function entries in pcie_link_config_a2c2
- [x] 0xa3db, 0xa3f5 (3 calls each) - Mid-function entries in PCIe register helper cluster

### Sub-ranges (all implemented)
| Range | Purpose | Coverage |
|-------|---------|----------|
| 0xA000-0xA3FF | PCIe address calculation helpers | 98% (45/46) |
| 0xA400-0xA6FF | USB/DMA buffer helpers | 100% (27/27) |
| 0xA700-0xAAFF | Admin command handlers | 98% (61/62) |
| 0xAB00-0xAFFF | Queue management | 89% (33/37) |
| 0xB000-0xB7FF | PCIe TLP construction | 98% (45/46) |
| 0xB800-0xBFFF | Completion handlers | 99% (98/99) |

### Key Implementations
- queue_handlers.c: 57 functions (power state helpers, NVMe cmd handlers)
- nvme.c: NVMe buffer and queue operations
- pcie.c: PCIe TLP construction and link helpers
- utils.c: 40+ register helper functions (0xBB00-0xBF00)

---

## 4. Event & Error Handlers (0xC000-0xDFFF)

**Estimated: ~200 functions, 6 stubs need implementation**

### Stubs Needing Implementation
- [ ] 0xdd12 - helper_dd12 (11 calls) - Command trigger and mode setup
- [ ] 0xce23 - transfer_handler_ce23 (5 calls) - DMA transfer handler
- [ ] 0xcb05 - helper_cb05 (3 calls) - Unknown
- [ ] 0xd17a - helper_d17a (1 call) - Protocol finalization
- [ ] 0xd8d5 - pcie_handler_d8d5 (1 call) - PCIe handler
- [ ] 0xdbbb - helper_dbbb (1 call) - State flag handler

### High Priority Functions
- [ ] 0xd1e6 (14 calls), 0xd1f2 (14 calls) - Event dispatch pair
- [ ] 0xd5da (10 calls) - Power/event handler
- [ ] 0xc2e7, 0xc2f8, 0xc2f1 - Error logging cluster
- [ ] 0xc20f, 0xc2bf - Error state handlers

### Sub-ranges
| Range | Purpose | Priority |
|-------|---------|----------|
| 0xC000-0xC0FF | PCIe tunnel/link init | Medium |
| 0xC100-0xC2FF | Error logging functions | High |
| 0xC300-0xC4FF | Config register helpers | Low |
| 0xC500-0xC8FF | DMA/buffer handlers | Medium |
| 0xC900-0xCBFF | Queue completion | Medium |
| 0xCC00-0xCFFF | Transfer handlers | High |
| 0xD000-0xD2FF | Event dispatch | High |
| 0xD300-0xD5FF | Power management | Medium |
| 0xD600-0xD8FF | PHY configuration | Medium |
| 0xD900-0xDFFF | State machine helpers | Medium |

---

## 5. NVMe Command Engine (0x9000-0x93FF)

**Status: COMPLETE** ✓

### Key Functions
- [x] 0x9388 (48 calls) - flash_config_init_9388 in state_helpers.c
- [x] 0x92bb - nvme_state_error_set_92bb in nvme.c
- [x] 0x92b3 - helper_92b3 in nvme.c
- [x] 0x9264-0x92af - nvme_queue_count_matches_9264 in nvme.c
- [x] 0xc00d - pcie_tunnel_init_c00d in stubs.c
- [x] 0xc44f - queue_calc_dptr_c44f in stubs.c
- [x] 0xc4a9 - queue_check_status_c4a9 in stubs.c

### Notes
Many functions in 0x9400-0x96FF are implemented in nvme.c and cmd.c. The addresses 0x900a, 0x9070, 0x925a listed in the original TODO are mid-function entry points within larger functions, not standalone functions.

---

## 6. Remaining Empty Stubs

These are placeholder functions that need actual implementation:

### USB Stubs
- [ ] usb_parse_descriptor
- [ ] usb_state_setup_4c98
- [ ] usb_helper_51ef
- [ ] usb_helper_5112
- [ ] handler_0327_usb_power_init

### NVMe Stubs
- [ ] nvme_util_advance_queue
- [ ] nvme_util_check_command_ready
- [ ] nvme_util_clear_completion

### SCSI Stubs
- [ ] scsi_send_csw
- [ ] scsi_dma_mode_setup

### System Stubs
- [ ] startup_init
- [ ] sys_event_dispatch_05e8
- [ ] sys_init_helper_bbc7
- [ ] sys_timer_handler_e957
- [ ] handler_039a_buffer_dispatch

---

## 7. Utility Functions (0x0000-0x0FFF) ✓ COMPLETE

Analysis complete - none of these addresses are standalone application functions:

- [x] 0x0110 - Mid-function entry point in startup init code (not a callable function)
- [x] 0x034d - Dispatch stub table entry (covered by dispatch.c infrastructure)
- [x] 0x0555 - Dispatch stub table entry (covered by dispatch.c infrastructure)
- [x] 0x0810 - Unused interrupt vector (`reti` + 0xFF padding)
- [x] 0x09e7 - NOP padding / unused code space
- [x] 0x0d59 - Keil C51 runtime library: 32-bit memory store helper (IDATA/XDATA/paged XDATA dispatch based on R3). SDCC provides equivalent runtime.

---

## 8. Code Quality & Verification

### Register Naming
- Audit XDATA_REG8/XDATA8 usage - ensure all registers ≥0x6000 use proper REG_* names
- Audit global variables - ensure all XDATA <0x6000 use proper G_* names

### Build Verification
- Regular builds to check for size regression
- Compare disassembly of key functions against fw.bin

### Testing Priorities
1. PCIe link training (pcie_lane_config_helper)
2. USB enumeration
3. NVMe command handling
4. SCSI translation

---

## Recommended Work Order

1. **Stubs with 5+ calls** - dd12, ce23, high-priority event handlers
2. **Error/Event handlers (0xC000-0xD2FF)** - Critical for proper operation
3. ~~**Queue handlers (0xA700-0xAFFF)**~~ ✓ DONE (97% coverage)
4. ~~**PCIe state machine (0x9700-0x98FF)**~~ ✓ DONE
5. ~~**Remaining 0xA000-0xBFFF**~~ ✓ DONE (97% coverage)
6. **Remaining 0xD300-0xDFFF** - Power and PHY
7. **Low-priority stubs** - USB helpers, system stubs
8. ~~**Utility functions (0x0000-0x0FFF)**~~ ✓ DONE

---

## Completed Sections ✓

- [x] State Machine Helpers (0x1000-0x1FFF)
- [x] Protocol State Machines (0x2000-0x3FFF)
- [x] SCSI/USB Mass Storage (0x4000-0x5FFF)
- [x] NVMe Command Engine (0x9000-0x93FF) - nvme.c, stubs.c
- [x] PCIe State Machine Complex (0x9700-0x98FF) - cfg_pcie_ep_state_machine in cmd.c
- [x] Queue & Admin Command Handlers (0xA000-0xBFFF) - 97% coverage across queue_handlers.c, nvme.c, pcie.c, utils.c
- [x] Bank1 High (0xE000-0xFFFF)
- [x] Core drivers: pcie.c, nvme.c, usb.c, dma.c, flash.c, phy.c, power.c, timer.c, uart.c
- [x] Utility Functions (0x0000-0x0FFF) - All analyzed, none are standalone app functions
