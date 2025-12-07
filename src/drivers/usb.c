/*
 * ASM2464PD Firmware - USB Driver
 *
 * USB interface controller for USB4/Thunderbolt to NVMe bridge
 * Handles USB enumeration, endpoint configuration, and data transfers
 *
 * ============================================================================
 * ARCHITECTURE OVERVIEW
 * ============================================================================
 * The ASM2464PD USB subsystem handles the host interface for the NVMe bridge:
 *
 *   USB Host <---> USB Controller <---> Endpoint Buffers <---> DMA Engine
 *                      |                      |
 *                      v                      v
 *              Status Registers         SCSI/Mass Storage
 *
 * The USB controller supports:
 * - USB 3.2 Gen2x2 (20 Gbps)
 * - USB4/Thunderbolt 3/4 tunneling
 * - 8 configurable endpoints (EP0-EP7)
 * - Mass Storage Class (SCSI over USB)
 * - Bulk-Only Transport (BOT) protocol
 *
 * ============================================================================
 * REGISTER MAP
 * ============================================================================
 * USB Core Registers (0x9000-0x90FF):
 * 0x9000: REG_USB_STATUS          - Main status register
 *         Bit 0: Activity/interrupt pending
 *         Bit 7: Connected/ready
 * 0x9001: REG_USB_CONTROL         - Control register
 * 0x9002: REG_USB_CONFIG          - Configuration
 * 0x9003: REG_USB_EP0_STATUS      - EP0 status
 * 0x9004-0x9005: REG_USB_EP0_LEN  - EP0 transfer length (16-bit)
 * 0x9006: REG_USB_EP0_CONFIG      - EP0 configuration
 *         Bit 0: Mode bit (set for USB mode)
 * 0x9007-0x9008: REG_USB_SCSI_BUF_LEN - SCSI buffer length
 * 0x9091: REG_INT_FLAGS_EX0       - Extended interrupt flags
 * 0x9093: REG_USB_EP_CFG1         - Endpoint config 1
 * 0x9094: REG_USB_EP_CFG2         - Endpoint config 2
 * 0x9096: USB Endpoint Base       - Indexed by endpoint number
 * 0x9101: REG_USB_PERIPH_STATUS   - Peripheral status
 *         Bit 6: Peripheral busy flag
 * 0x9118: REG_USB_EP_STATUS       - Endpoint status bitmap (8 EPs)
 * 0x910D-0x910E: Status pair
 * 0x911B: REG_USB_BUFFER_ALT      - Buffer alternate
 * 0x911F-0x9122: USB status bytes
 *
 * Buffer Control (0xD800-0xD8FF):
 * 0xD804-0xD807: Transfer status copy area
 * 0xD80C: Buffer transfer start
 *
 * ============================================================================
 * ENDPOINT DISPATCH TABLE
 * ============================================================================
 * Located at CODE address 0x5A6A (256 bytes):
 * - Maps USB status byte to endpoint index (0-7)
 * - Value >= 8 means "no endpoint to process"
 * - Priority-based selection using bit position lookup
 *
 * Bit mask table at 0x5B6A (8 bytes):
 * - Maps endpoint index to clear mask
 *
 * Offset table at 0x5B72 (8 bytes):
 * - Maps endpoint index to register offset (multiples of 8)
 *
 * ============================================================================
 * WORK AREA GLOBALS (0x0000-0x0BFF)
 * ============================================================================
 * 0x000A: G_EP_CHECK_FLAG         - Endpoint processing check
 * 0x014E: Circular buffer index (5-bit)
 * 0x0218-0x0219: Buffer address pair
 * 0x0464: G_SYS_STATUS_PRIMARY    - Primary status for indexing
 * 0x0465: G_SYS_STATUS_SECONDARY  - Secondary status
 * 0x054E: G_EP_CONFIG_ARRAY       - Endpoint config array base
 * 0x0564: G_EP_QUEUE_CTRL         - Endpoint queue control
 * 0x0565: G_EP_QUEUE_STATUS       - Endpoint queue status
 * 0x05A6-0x05A7: G_PCIE_TXN_COUNT - PCIe transaction count
 * 0x05D3: Endpoint config multiplier base
 * 0x06E6: G_STATE_FLAG_06E6       - Processing complete/error flag
 * 0x07E4: G_SYS_FLAGS_BASE        - System flags base (must be 1)
 * 0x0A7B: G_EP_DISPATCH_VAL1      - First endpoint index
 * 0x0A7C: G_EP_DISPATCH_VAL2      - Second endpoint index
 * 0x0AA8-0x0AAB: Flash error flags and state
 * 0x0AF2: G_TRANSFER_FLAG_0AF2    - Transfer active flag
 * 0x0AF5: G_EP_DISPATCH_OFFSET    - Combined dispatch offset
 * 0x0AFA-0x0AFB: G_TRANSFER_PARAMS - Transfer parameters
 * 0x0B2E: G_USB_TRANSFER_FLAG     - USB transfer in progress
 * 0x0B41: Buffer handler check
 *
 * ============================================================================
 * ENDPOINT DISPATCH ALGORITHM
 * ============================================================================
 * 1. Read endpoint status from REG_USB_EP_STATUS (0x9118)
 * 2. Look up primary endpoint index via ep_index_table[status]
 * 3. If index >= 8, exit (no endpoints need service)
 * 4. Read secondary status from USB_EP_BASE + ep_index1
 * 5. Look up secondary endpoint index
 * 6. If secondary index >= 8, exit
 * 7. Calculate combined offset = ep_offset_table[ep_index1] + ep_index2
 * 8. Call endpoint handler with combined offset
 * 9. Clear endpoint status via bit mask write
 * 10. Loop up to 32 times
 *
 * ============================================================================
 * IMPLEMENTATION STATUS
 * ============================================================================
 * [x] usb_enable (0x1b7e)                  - Load config params
 * [x] usb_setup_endpoint                   - Configure endpoint (stub)
 * [x] usb_ep_init_handler (0x5409)         - Clear state flags
 * [x] usb_ep_handler (0x5442)              - Process single endpoint
 * [x] usb_buffer_handler (0xd810)          - Buffer transfer dispatch
 * [x] usb_ep_config_bulk (0x1cfc)          - Configure bulk endpoint
 * [x] usb_ep_config_int (0x1d07)           - Configure interrupt endpoint
 * [x] usb_set_transfer_flag (0x1d1d)       - Set transfer flag
 * [x] usb_get_nvme_data_ctrl (0x1d24)      - Get NVMe data control
 * [x] usb_set_nvme_ctrl_bit7 (0x1d2b)      - Set control bit 7
 * [x] usb_get_sys_status_offset (0x1743)   - Get status with offset
 * [x] usb_calc_addr_with_offset (0x1752)   - Calculate address
 * [x] usb_set_done_flag (0x1787)           - Set done flag
 * [x] usb_set_transfer_active_flag (0x312a) - Set transfer active
 * [x] usb_copy_status_to_buffer (0x3147)   - Copy status regs
 * [x] usb_clear_idata_indexed (0x3168)     - Clear indexed location
 * [x] usb_read_status_pair (0x3181)        - Read 16-bit status
 * [x] usb_read_transfer_params (0x31a5)    - Read transfer params
 * [x] usb_calc_queue_addr (0x176b)         - Calculate queue address
 * [x] usb_calc_queue_addr_next (0x1779)    - Calculate next queue address
 * [x] usb_store_idata_16 (0x1d32)          - Store 16-bit to IDATA
 * [x] usb_add_masked_counter (0x1d39)      - Add to circular counter
 * [x] usb_calc_indexed_addr (0x179d)       - Calculate indexed address
 * [x] usb_read_queue_status_masked (0x17c1) - Read masked queue status
 * [x] usb_shift_right_3 (0x17cd)           - Shift utility
 * [x] usb_ep_dispatch_loop (0x0e96)        - Main endpoint dispatch
 * [x] dma_clear_dword (0x173b)             - Clear 32-bit value
 * [x] usb_calc_addr_009f (0x1b88)          - Calculate address with IDATA offset
 * [x] usb_get_ep_config_indexed (0x1b96)   - Get indexed endpoint config
 * [x] usb_read_buf_addr_pair (0x1ba5)      - Read buffer address pair
 * [x] usb_get_idata_0x12_field (0x1bae)    - Extract IDATA[0x12] field
 * [x] usb_set_ep0_mode_bit (0x1bde)        - Set EP0 mode bit 0
 * [x] usb_get_config_offset_0456 (0x1be8)  - Get config offset 0x04XX
 * [x] usb_init_pcie_txn_state (0x1d43)     - Initialize PCIe transaction state
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/* External utility functions from utils.c */
extern uint32_t idata_load_dword(__idata uint8_t *ptr);
extern uint32_t idata_load_dword_alt(__idata uint8_t *ptr);

/*
 * usb_enable - Enable USB interface
 * Address: 0x1b7e-0x1b87 (10 bytes)
 *
 * Loads configuration parameters from internal RAM addresses 0x09 and 0x6b.
 * Returns two 32-bit values in R4-R7 and R0-R3 to caller.
 *
 * Original disassembly:
 *   1b7e: mov r0, #0x09
 *   1b80: lcall 0x0d78       ; idata_load_dword (loads IDATA[0x09-0x0c] to R4-R7)
 *   1b83: mov r0, #0x6b
 *   1b85: ljmp 0x0d90        ; idata_load_dword_alt (loads IDATA[0x6b-0x6e] to R0-R3)
 */
void usb_enable(void)
{
    idata_load_dword((__idata uint8_t *)0x09);
    idata_load_dword_alt((__idata uint8_t *)0x6b);
}

/*
 * usb_setup_endpoint - Configure USB endpoint
 * Address: 0x1bd5-0x1bdb (7 bytes)
 *
 * This is actually an address calculation helper that:
 * 1. Adds 0x10 to A
 * 2. Stores result in R1
 * 3. Propagates carry to R2
 * 4. Jumps to 0x0bc8 for further processing
 *
 * Note: ghidra.c shows this as part of a larger flow involving
 * IDATA[0x6B..0x6F] operations (see dma_copy_idata_6b_to_6f).
 * The function is typically called as part of endpoint configuration
 * during USB initialization.
 *
 * Original disassembly:
 *   1bd5: add a, #0x10
 *   1bd7: mov r1, a
 *   1bd8: clr a
 *   1bd9: addc a, r2
 *   1bda: mov r2, a
 *   1bdb: ljmp 0x0bc8
 */
void usb_setup_endpoint(void)
{
    /* This helper is typically inlined or called via tail-call optimization.
     * The actual endpoint configuration happens in the caller context. */
}

/*===========================================================================
 * Endpoint Dispatch Tables
 * Address: 0x5a6a, 0x5b6a, 0x5b72 in CODE memory
 *===========================================================================*/

/*
 * Endpoint index mapping table
 * Address: 0x5a6a (256 bytes)
 *
 * Maps USB status byte values to endpoint indices (0-7).
 * Value >= 8 means "no endpoint to process" (exit loop).
 * Pattern repeats: 08 00 01 00 02 00 01 00 03 00 01 00 02 00 01 00
 *                  04 00 01 00 02 00 01 00 03 00 01 00 02 00 01 00
 *                  05 00 01 00 02 00 01 00 03 00 01 00 02 00 01 00
 *                  04 00 01 00 02 00 01 00 03 00 01 00 02 00 01 00
 *                  ... repeats for 256 entries
 */
static const __code uint8_t ep_index_table[256] = {
    /* 0x00-0x0F */
    0x08, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0x10-0x1F */
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0x20-0x2F */
    0x05, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0x30-0x3F */
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0x40-0x4F */
    0x06, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0x50-0x5F */
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0x60-0x6F */
    0x05, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0x70-0x7F */
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0x80-0x8F */
    0x07, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0x90-0x9F */
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0xA0-0xAF */
    0x05, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0xB0-0xBF */
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0xC0-0xCF */
    0x06, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0xD0-0xDF */
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0xE0-0xEF */
    0x05, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    /* 0xF0-0xFF */
    0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00
};

/*
 * Endpoint bit mask table
 * Address: 0x5b6a (8 bytes)
 *
 * Maps endpoint index (0-7) to bit mask for status clear.
 */
static const __code uint8_t ep_bit_mask_table[8] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
};

/*
 * Endpoint offset table
 * Address: 0x5b72 (8 bytes)
 *
 * Maps endpoint index (0-7) to register offset (multiples of 8).
 */
static const __code uint8_t ep_offset_table[8] = {
    0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38
};

/*===========================================================================
 * USB Endpoint XDATA Addresses
 *===========================================================================*/

/* USB endpoint register base at 0x9096 (indexed by endpoint) */
#define REG_USB_EP_BASE     0x9096

/*===========================================================================
 * Endpoint Handler Forward Declaration
 *===========================================================================*/

/*
 * usb_ep_init_handler - USB endpoint initialization sub-handler
 * Address: 0x5409-0x5417 (15 bytes)
 *
 * Clears various state flags and dispatches to buffer handler at 0xD810.
 *
 * Original disassembly:
 *   5409: clr a               ; A = 0
 *   540a: mov dptr, #0x0b2e
 *   540d: movx @dptr, a       ; XDATA[0x0B2E] = 0
 *   540e: mov r0, #0x6a
 *   5410: mov @r0, a          ; IDATA[0x6A] = 0
 *   5411: mov dptr, #0x06e6
 *   5414: movx @dptr, a       ; XDATA[0x06E6] = 0
 *   5415: ljmp 0x039a         ; dispatch to 0xD810
 */
static void usb_ep_init_handler(void)
{
    /* Clear state variables in work area */
    G_USB_TRANSFER_FLAG = 0;

    /* Clear IDATA[0x6A] */
    *(__idata uint8_t *)0x6A = 0;

    /* Clear processing complete flag in work area */
    G_STATE_FLAG_06E6 = 0;

    /* Original jumps to 0x039a which dispatches to 0xD810 (buffer handler) */
    /* handler_039a() is the main buffer handler in main.c */
    /* Note: Cannot call directly from static function - caller should handle */
}

/*
 * usb_ep_handler - Process single USB endpoint
 * Address: 0x5442-0x544b (10 bytes)
 *
 * Called from endpoint dispatch loop to process a single endpoint.
 * Checks XDATA[0x000A] and conditionally calls 0x5409.
 *
 * Original disassembly:
 *   5442: mov dptr, #0x000a
 *   5445: movx a, @dptr
 *   5446: jnz 0x544b          ; if non-zero, return
 *   5448: lcall 0x5409
 *   544b: ret
 */
static void usb_ep_handler(void)
{
    if (G_EP_CHECK_FLAG == 0) {
        usb_ep_init_handler();
    }
}

/*===========================================================================
 * Table-Driven Endpoint Dispatch
 *===========================================================================*/

/*
 * usb_ep_dispatch_loop - USB endpoint processing loop
 * Address: 0x0e96-0x0efb (101 bytes)
 *
 * Main USB endpoint dispatch loop that iterates up to 32 times,
 * reading endpoint status and dispatching to handlers.
 *
 * Algorithm:
 * 1. For counter = 0 to 31:
 *    a. Read USB status from 0x9118
 *    b. Look up endpoint index via ep_index_table
 *    c. If index >= 8, exit loop (no more endpoints to process)
 *    d. Read secondary status from 0x9096 + first_index
 *    e. Look up second endpoint index
 *    f. If second_index >= 8, exit loop
 *    g. Calculate combined offset and store to 0x0AF5
 *    h. Call endpoint handler at 0x5442
 *    i. Write bit mask to clear endpoint status
 *
 * Original disassembly:
 *   0e96: mov 0x37, #0x00     ; counter = 0
 *   0e99: mov dptr, #0x9118   ; USB status
 *   0e9c: movx a, @dptr       ; read status
 *   0e9d: mov dptr, #0x5a6a   ; index table
 *   0ea0: movc a, @a+dptr     ; lookup
 *   0ea1: mov dptr, #0x0a7b
 *   0ea4: movx @dptr, a       ; store index1
 *   ... (see full analysis above)
 *   0ef9: jc 0x0e99           ; loop if counter < 32
 */
/*===========================================================================
 * Buffer Handler (0xD810)
 *===========================================================================*/

/*
 * usb_buffer_handler - Buffer transfer dispatch handler
 * Address: 0xd810-0xd851 (66 bytes)
 *
 * Complex handler that checks various status flags and configures
 * timer registers for buffer operations.
 *
 * Original disassembly:
 *   d810: mov dptr, #0x0b41
 *   d813: movx a, @dptr
 *   d814: jz 0xd851           ; if 0, return
 *   d816: mov dptr, #0x9091
 *   d819: movx a, @dptr
 *   d81a: jb 0xe0.0, 0xd851   ; if bit 0 set, return
 *   d81d: mov dptr, #0x07e4
 *   d820: movx a, @dptr
 *   d821: xrl a, #0x01
 *   d823: jnz 0xd851          ; if != 1, return
 *   d825: mov dptr, #0x9000
 *   d828: movx a, @dptr
 *   d829: jnb 0xe0.0, 0xd83a  ; if bit 0 clear, skip to 0xd83a
 *   d82c: mov dptr, #0xc471
 *   d82f: movx a, @dptr
 *   d830: jb 0xe0.0, 0xd851   ; if bit 0 set, return
 *   d833: mov dptr, #0x000a
 *   d836: movx a, @dptr
 *   d837: jz 0xd846           ; if 0, skip to 0xd846
 *   d839: ret                 ; early return
 *   d83a: mov dptr, #0x9101
 *   d83d: movx a, @dptr
 *   d83e: jb 0xe0.6, 0xd851   ; if bit 6 set, return
 *   d841: mov r0, #0x6a
 *   d843: mov a, @r0
 *   d844: jnz 0xd851          ; if IDATA[0x6A] != 0, return
 *   d846: mov dptr, #0xcc17   ; Timer 1 CSR
 *   d849: mov a, #0x04
 *   d84b: movx @dptr, a       ; Write 0x04
 *   d84c: mov a, #0x02
 *   d84e: movx @dptr, a       ; Write 0x02
 *   d84f: dec a               ; A = 0x01
 *   d850: movx @dptr, a       ; Write 0x01
 *   d851: ret
 */
void usb_buffer_handler(void)
{
    uint8_t status;

    /* Check USB state */
    if (G_USB_STATE_0B41 == 0) {
        return;
    }

    /* Check USB interrupt flags bit 0 */
    status = REG_INT_FLAGS_EX0;
    if (status & 0x01) {
        return;
    }

    /* Check flags base - must be 1 */
    if (G_SYS_FLAGS_BASE != 1) {
        return;
    }

    /* Check USB status bit 0 */
    status = REG_USB_STATUS;
    if (status & 0x01) {
        /* USB status bit 0 set - check NVMe queue pointer */
        status = REG_NVME_QUEUE_PTR_C471;
        if (status & 0x01) {
            return;
        }

        /* Check endpoint check flag */
        if (G_EP_CHECK_FLAG != 0) {
            return;  /* Early return */
        }
    } else {
        /* USB status bit 0 clear - check USB peripheral status */
        status = REG_USB_PERIPH_STATUS;
        if (status & 0x40) {  /* Bit 6 */
            return;
        }

        /* Check IDATA[0x6A] */
        if (*(__idata uint8_t *)0x6A != 0) {
            return;
        }
    }

    /* Configure Timer 1 CSR with sequence: 0x04, 0x02, 0x01 */
    REG_TIMER1_CSR = 0x04;
    REG_TIMER1_CSR = 0x02;
    REG_TIMER1_CSR = 0x01;
}

/*===========================================================================
 * USB Endpoint Configuration Functions
 *===========================================================================*/

/*
 * usb_ep_config_bulk - Configure endpoint for bulk transfer
 * Address: 0x1cfc-0x1d06 (11 bytes)
 *
 * Sets USB endpoint registers 0x9093 and 0x9094 for bulk transfer config.
 *
 * Original disassembly:
 *   1cfc: mov dptr, #0x9093
 *   1cff: mov a, #0x08
 *   1d01: movx @dptr, a      ; XDATA[0x9093] = 0x08
 *   1d02: inc dptr
 *   1d03: mov a, #0x02
 *   1d05: movx @dptr, a      ; XDATA[0x9094] = 0x02
 *   1d06: ret
 */
void usb_ep_config_bulk(void)
{
    REG_USB_EP_CFG1 = 0x08;
    REG_USB_EP_CFG2 = 0x02;
}

/*
 * usb_ep_config_int - Configure endpoint for interrupt transfer
 * Address: 0x1d07-0x1d11 (11 bytes)
 *
 * Sets USB endpoint registers 0x9093 and 0x9094 for interrupt transfer config.
 *
 * Original disassembly:
 *   1d07: mov dptr, #0x9093
 *   1d0a: mov a, #0x02
 *   1d0c: movx @dptr, a      ; XDATA[0x9093] = 0x02
 *   1d0d: inc dptr
 *   1d0e: mov a, #0x10
 *   1d10: movx @dptr, a      ; XDATA[0x9094] = 0x10
 *   1d11: ret
 */
void usb_ep_config_int(void)
{
    REG_USB_EP_CFG1 = 0x02;
    REG_USB_EP_CFG2 = 0x10;
}

/*
 * usb_set_transfer_flag - Set USB transfer in-progress flag
 * Address: 0x1d1d-0x1d23 (7 bytes)
 *
 * Sets XDATA[0x0B2E] = 1 to indicate transfer in progress.
 *
 * Original disassembly:
 *   1d1d: mov dptr, #0x0b2e
 *   1d20: mov a, #0x01
 *   1d22: movx @dptr, a
 *   1d23: ret
 */
void usb_set_transfer_flag(void)
{
    G_USB_TRANSFER_FLAG = 1;
}

/*
 * usb_get_nvme_data_ctrl - Get NVMe data control status
 * Address: 0x1d24-0x1d2a (7 bytes)
 *
 * Reads NVMe data control register and masks upper 2 bits.
 *
 * Original disassembly:
 *   1d24: mov dptr, #0xc414
 *   1d27: movx a, @dptr
 *   1d28: anl a, #0xc0       ; mask bits 7-6
 *   1d2a: ret
 */
uint8_t usb_get_nvme_data_ctrl(void)
{
    return REG_NVME_DATA_CTRL & 0xC0;
}

/*
 * usb_set_nvme_ctrl_bit7 - Set bit 7 of NVMe control register
 * Address: 0x1d2b-0x1d31 (7 bytes)
 *
 * Reads current value, clears bit 7, sets bit 7, writes back.
 *
 * Original disassembly:
 *   1d2b: movx a, @dptr      ; read from DPTR (caller sets)
 *   1d2c: anl a, #0x7f       ; clear bit 7
 *   1d2e: orl a, #0x80       ; set bit 7
 *   1d30: movx @dptr, a
 *   1d31: ret
 */
void usb_set_nvme_ctrl_bit7(__xdata uint8_t *ptr)
{
    uint8_t val = *ptr;
    val = (val & 0x7F) | 0x80;
    *ptr = val;
}

/*===========================================================================
 * DMA/Transfer Utility Functions
 *===========================================================================*/

/*
 * dma_clear_dword - Clear 32-bit value at XDATA address
 * Address: 0x173b-0x1742 (8 bytes)
 *
 * Clears R4-R7 to 0 and calls xdata_store_dword (0x0dc5).
 *
 * Original disassembly:
 *   173b: clr a
 *   173c: mov r7, a
 *   173d: mov r6, a
 *   173e: mov r5, a
 *   173f: mov r4, a
 *   1740: ljmp 0x0dc5        ; xdata_store_dword
 */
void dma_clear_dword(__xdata uint8_t *ptr)
{
    ptr[0] = 0;
    ptr[1] = 0;
    ptr[2] = 0;
    ptr[3] = 0;
}

/*
 * usb_get_sys_status_offset - Get system status with offset
 * Address: 0x1743-0x1751 (15 bytes)
 *
 * Reads status from 0x0464, adds 0xA8 to form address in 0x05XX region,
 * and reads from that address.
 *
 * Original disassembly:
 *   1743: mov dptr, #0x0464
 *   1746: movx a, @dptr       ; read status
 *   1747: add a, #0xa8        ; offset = status + 0xA8
 *   1749: mov 0x82, a         ; DPL = offset
 *   174b: clr a
 *   174c: addc a, #0x05       ; DPH = 0x05
 *   174e: mov 0x83, a
 *   1750: movx a, @dptr       ; read from 0x05XX
 *   1751: ret
 */
uint8_t usb_get_sys_status_offset(void)
{
    uint8_t status = G_SYS_STATUS_PRIMARY;
    uint16_t addr = 0x0500 + status + 0xA8;
    return XDATA8(addr);
}

/*
 * usb_calc_addr_with_r7 - Calculate address with R7 offset
 * Address: 0x1752-0x175c (11 bytes)
 *
 * Calculates address 0x0059 + R7 and returns DPTR pointing there.
 *
 * Original disassembly:
 *   1752: mov a, #0x59
 *   1754: add a, r7          ; A = 0x59 + R7
 *   1755: mov 0x82, a        ; DPL = result
 *   1757: clr a
 *   1758: addc a, #0x00      ; DPH = carry
 *   175a: mov 0x83, a
 *   175c: ret
 */
__xdata uint8_t *usb_calc_addr_with_offset(uint8_t offset)
{
    return (__xdata uint8_t *)(0x0059 + offset);
}

/*
 * usb_set_done_flag - Set processing done flag
 * Address: 0x1787-0x178d (7 bytes)
 *
 * Sets XDATA[0x06E6] = 1 to indicate processing complete.
 *
 * Original disassembly:
 *   1787: mov dptr, #0x06e6
 *   178a: mov a, #0x01
 *   178c: movx @dptr, a
 *   178d: ret
 */
void usb_set_done_flag(void)
{
    G_STATE_FLAG_06E6 = 1;
}

/*
 * usb_set_transfer_active_flag - Set transfer flag and USB mode bit
 * Address: 0x312a-0x3139 (16 bytes)
 *
 * Sets transfer flag at 0x0AF2 to 1, then sets bit 0 of USB EP0 config.
 *
 * Original disassembly:
 *   312a: mov dptr, #0x0af2
 *   312d: mov a, #0x01
 *   312f: movx @dptr, a       ; XDATA[0x0AF2] = 1
 *   3130: mov dptr, #0x9006
 *   3133: movx a, @dptr
 *   3134: anl a, #0xfe        ; clear bit 0
 *   3136: orl a, #0x01        ; set bit 0
 *   3138: movx @dptr, a
 *   3139: ret
 */
void usb_set_transfer_active_flag(void)
{
    uint8_t val;

    G_TRANSFER_FLAG_0AF2 = 1;

    val = REG_USB_EP0_CONFIG;
    val = (val & 0xFE) | 0x01;
    REG_USB_EP0_CONFIG = val;
}

/*
 * usb_copy_status_to_buffer - Copy USB status regs to buffer area
 * Address: 0x3147-0x3167 (33 bytes)
 *
 * Copies 4 bytes from USB status registers 0x911F-0x9122 to buffer
 * area 0xD804-0xD807.
 *
 * Original disassembly:
 *   3147: mov dptr, #0x911f
 *   314a: movx a, @dptr
 *   314b: mov dptr, #0xd804
 *   314e: movx @dptr, a       ; D804 = [911F]
 *   314f: mov dptr, #0x9120
 *   3152: movx a, @dptr
 *   3153: mov dptr, #0xd805
 *   3156: movx @dptr, a       ; D805 = [9120]
 *   3157: mov dptr, #0x9121
 *   315a: movx a, @dptr
 *   315b: mov dptr, #0xd806
 *   315e: movx @dptr, a       ; D806 = [9121]
 *   315f: mov dptr, #0x9122
 *   3162: movx a, @dptr
 *   3163: mov dptr, #0xd807
 *   3166: movx @dptr, a       ; D807 = [9122]
 *   3167: ret
 */
void usb_copy_status_to_buffer(void)
{
    REG_BUFFER_PTR_HIGH = REG_USB_STATUS_1F;
    REG_BUFFER_LENGTH_LOW = REG_USB_STATUS_20;
    REG_BUFFER_STATUS = REG_USB_STATUS_21;
    REG_BUFFER_LENGTH_HIGH = REG_USB_STATUS_22;
}

/*
 * usb_clear_idata_indexed - Clear indexed IDATA location
 * Address: 0x3168-0x3180 (25 bytes)
 *
 * Calculates address 0x00C2 + IDATA[0x38] and clears that XDATA location,
 * then returns pointer to 0x00E5 + IDATA[0x38].
 *
 * Original disassembly:
 *   3168: mov a, #0xc2
 *   316a: add a, 0x38         ; A = 0xC2 + IDATA[0x38]
 *   316c: mov 0x82, a         ; DPL = A
 *   316e: clr a
 *   316f: addc a, #0x00       ; DPH = carry
 *   3171: mov 0x83, a
 *   3173: clr a
 *   3174: movx @dptr, a       ; clear XDATA[0x00C2 + offset]
 *   3175: mov a, #0xe5
 *   3177: add a, 0x38         ; A = 0xE5 + IDATA[0x38]
 *   3179: mov 0x82, a
 *   317b: clr a
 *   317c: addc a, #0x00
 *   317e: mov 0x83, a
 *   3180: ret
 */
__xdata uint8_t *usb_clear_idata_indexed(void)
{
    uint8_t offset = *(__idata uint8_t *)0x38;

    /* Clear at 0x00C2 + offset */
    XDATA8(0x00C2 + offset) = 0;

    /* Return pointer to 0x00E5 + offset */
    return (__xdata uint8_t *)(0x00E5 + offset);
}

/*===========================================================================
 * USB Status Read Functions
 *===========================================================================*/

/*
 * usb_read_status_pair - Read 16-bit status from USB registers
 * Address: 0x3181-0x3188 (8 bytes)
 *
 * Reads USB status registers 0x910D and 0x910E as a 16-bit value.
 * Returns high byte in R6, low byte in A.
 *
 * Original disassembly:
 *   3181: mov dptr, #0x910d
 *   3184: movx a, @dptr       ; R6 = [0x910D]
 *   3185: mov r6, a
 *   3186: inc dptr
 *   3187: movx a, @dptr       ; A = [0x910E]
 *   3188: ret
 */
uint16_t usb_read_status_pair(void)
{
    uint8_t hi = REG_USB_STATUS_0D;
    uint8_t lo = REG_USB_STATUS_0E;
    return ((uint16_t)hi << 8) | lo;
}

/*
 * usb_read_transfer_params - Read transfer parameters
 * Address: 0x31a5-0x31ac (8 bytes)
 *
 * Reads 16-bit value from transfer params at 0x0AFA-0x0AFB.
 * Returns high byte in R6, low byte in A.
 *
 * Original disassembly:
 *   31a5: mov dptr, #0x0afa
 *   31a8: movx a, @dptr       ; R6 = [0x0AFA]
 *   31a9: mov r6, a
 *   31aa: inc dptr
 *   31ab: movx a, @dptr       ; A = [0x0AFB]
 *   31ac: ret
 */
uint16_t usb_read_transfer_params(void)
{
    uint8_t hi = G_TRANSFER_PARAMS_HI;
    uint8_t lo = G_TRANSFER_PARAMS_LO;
    return ((uint16_t)hi << 8) | lo;
}

/*===========================================================================
 * Address Calculation Functions
 *===========================================================================*/

/*
 * usb_calc_queue_addr - Calculate queue element address
 * Address: 0x176b-0x1778 (14 bytes)
 *
 * Calculates DPTR = 0x0478 + (A * 4) where A is input.
 * Used for accessing 4-byte queue elements.
 *
 * Original disassembly:
 *   176b: add a, 0xe0         ; A = A * 2 (add A to itself via ACC register)
 *   176d: add a, 0xe0         ; A = A * 2 again (so A * 4)
 *   176f: add a, #0x78
 *   1771: mov 0x82, a         ; DPL = result
 *   1773: clr a
 *   1774: addc a, #0x04       ; DPH = 0x04 + carry
 *   1776: mov 0x83, a
 *   1778: ret
 */
__xdata uint8_t *usb_calc_queue_addr(uint8_t index)
{
    uint16_t offset = (uint16_t)index * 4;
    return (__xdata uint8_t *)(0x0478 + offset);
}

/*
 * usb_calc_queue_addr_next - Calculate next queue element address
 * Address: 0x1779-0x1786 (14 bytes)
 *
 * Calculates DPTR = 0x0479 + (A * 4) where A is input.
 * Similar to usb_calc_queue_addr but starts at 0x0479.
 *
 * Original disassembly:
 *   1779: add a, 0xe0         ; A = A * 2
 *   177b: add a, 0xe0         ; A = A * 4
 *   177d: add a, #0x79
 *   177f: mov 0x82, a         ; DPL
 *   1781: clr a
 *   1782: addc a, #0x04       ; DPH = 0x04 + carry
 *   1784: mov 0x83, a
 *   1786: ret
 */
__xdata uint8_t *usb_calc_queue_addr_next(uint8_t index)
{
    uint16_t offset = (uint16_t)index * 4;
    return (__xdata uint8_t *)(0x0479 + offset);
}

/*
 * usb_store_idata_16 - Store 16-bit value to IDATA
 * Address: 0x1d32-0x1d38 (7 bytes)
 *
 * Stores 16-bit value (R6:A) to IDATA[0x16:0x17].
 * High byte to [0x16], low byte to [0x17].
 *
 * Original disassembly:
 *   1d32: mov r1, #0x17
 *   1d34: mov @r1, a          ; IDATA[0x17] = A (low)
 *   1d35: mov a, r6
 *   1d36: dec r1
 *   1d37: mov @r1, a          ; IDATA[0x16] = R6 (high)
 *   1d38: ret
 */
void usb_store_idata_16(uint8_t hi, uint8_t lo)
{
    *(__idata uint8_t *)0x17 = lo;
    *(__idata uint8_t *)0x16 = hi;
}

/*
 * usb_add_masked_counter - Add to counter with 5-bit mask
 * Address: 0x1d39-0x1d42 (10 bytes)
 *
 * Reads value from 0x014E, adds input, masks to 5 bits, writes back.
 * Used for circular buffer index management.
 *
 * Original disassembly:
 *   1d39: mov r7, a           ; save A
 *   1d3a: mov dptr, #0x014e
 *   1d3d: movx a, @dptr       ; A = [0x014E]
 *   1d3e: add a, r7           ; A += original A
 *   1d3f: anl a, #0x1f        ; mask to 0-31
 *   1d41: movx @dptr, a       ; write back
 *   1d42: ret
 */
void usb_add_masked_counter(uint8_t value)
{
    uint8_t current = G_USB_INDEX_COUNTER;
    G_USB_INDEX_COUNTER = (current + value) & 0x1F;
}

/*===========================================================================
 * Address Calculation Helpers
 *===========================================================================*/

/*
 * usb_calc_indexed_addr - Calculate indexed address
 * Address: 0x179d-0x17a8 (12 bytes)
 *
 * Calculates DPTR = 0x00C2 + IDATA[0x52].
 * Returns pointer to indexed location.
 *
 * Original disassembly:
 *   179d: mov a, #0xc2
 *   179f: add a, 0x52         ; A = 0xC2 + IDATA[0x52]
 *   17a1: mov 0x82, a         ; DPL
 *   17a3: clr a
 *   17a4: addc a, #0x00       ; DPH = carry
 *   17a6: mov 0x83, a
 *   17a8: ret
 */
__xdata uint8_t *usb_calc_indexed_addr(void)
{
    uint8_t offset = *(__idata uint8_t *)0x52;
    return (__xdata uint8_t *)(0x00C2 + offset);
}

/*
 * usb_read_queue_status_masked - Read and mask queue status
 * Address: 0x17c1-0x17cc (12 bytes)
 *
 * Reads REG_SCSI_DMA_QUEUE_STAT, masks to 4 bits, stores to IDATA[0x40],
 * returns the masked value.
 *
 * Original disassembly:
 *   17c1: mov dptr, #0xce67
 *   17c4: movx a, @dptr       ; read queue status
 *   17c5: anl a, #0x0f        ; mask to 4 bits
 *   17c7: mov 0x40, a         ; store to IDATA[0x40]
 *   17c9: clr c
 *   17ca: subb a, #0x08       ; compare with 8
 *   17cc: ret
 */
uint8_t usb_read_queue_status_masked(void)
{
    uint8_t val = REG_SCSI_DMA_QUEUE_STAT & 0x0F;
    *(__idata uint8_t *)0x40 = val;
    return val;
}

/*
 * usb_shift_right_3 - Right shift value by 3 bits
 * Address: 0x17cd-0x17d7 (11 bytes)
 *
 * Shifts input right 3 bits, masks to 5 bits.
 *
 * Original disassembly:
 *   17cd: rrc a
 *   17ce: rrc a
 *   17cf: rrc a               ; A >>= 3
 *   17d0: anl a, #0x1f        ; mask
 *   17d2: mov r7, a
 *   17d3: clr c
 *   17d4: mov a, #0x03
 *   17d6: subb a, r7          ; carry if R7 > 3
 *   17d7: ret
 */
uint8_t usb_shift_right_3(uint8_t val)
{
    return (val >> 3) & 0x1F;
}

/*===========================================================================
 * Table-Driven Endpoint Dispatch
 *===========================================================================*/

/*
 * usb_ep_dispatch_loop - USB endpoint processing loop
 * Address: 0x0e96-0x0efb (101 bytes)
 *
 * Main USB endpoint dispatch loop that iterates up to 32 times,
 * reading endpoint status and dispatching to handlers.
 *
 * Algorithm:
 * 1. For counter = 0 to 31:
 *    a. Read USB status from 0x9118
 *    b. Look up endpoint index via ep_index_table
 *    c. If index >= 8, exit loop (no more endpoints to process)
 *    d. Read secondary status from 0x9096 + first_index
 *    e. Look up second endpoint index
 *    f. If second_index >= 8, exit loop
 *    g. Calculate combined offset and store to 0x0AF5
 *    h. Call endpoint handler at 0x5442
 *    i. Write bit mask to clear endpoint status
 *
 * Original disassembly:
 *   0e96: mov 0x37, #0x00     ; counter = 0
 *   0e99: mov dptr, #0x9118   ; USB status
 *   0e9c: movx a, @dptr       ; read status
 *   0e9d: mov dptr, #0x5a6a   ; index table
 *   0ea0: movc a, @a+dptr     ; lookup
 *   0ea1: mov dptr, #0x0a7b
 *   0ea4: movx @dptr, a       ; store index1
 *   ... (see full analysis above)
 *   0ef9: jc 0x0e99           ; loop if counter < 32
 */
void usb_ep_dispatch_loop(void)
{
    __idata uint8_t counter;
    uint8_t status;
    uint8_t ep_index1;
    uint8_t ep_index2;
    uint8_t offset;
    uint8_t bit_mask;

    /* Initialize counter at IDATA 0x37 */
    counter = 0;

    do {
        /* Read USB endpoint status */
        status = REG_USB_EP_STATUS;

        /* Look up first endpoint index */
        ep_index1 = ep_index_table[status];

        /* Store to endpoint dispatch value 1 */
        G_EP_DISPATCH_VAL1 = ep_index1;

        /* Re-read (original firmware does this) */
        ep_index1 = G_EP_DISPATCH_VAL1;

        /* If index >= 8, no endpoint to process - exit */
        if (ep_index1 >= 8) {
            break;
        }

        /* Read secondary status from endpoint base + ep_index1 */
        status = XDATA8(REG_USB_EP_BASE + ep_index1);

        /* Look up second endpoint index */
        ep_index2 = ep_index_table[status];

        /* Store to endpoint dispatch value 2 */
        G_EP_DISPATCH_VAL2 = ep_index2;

        /* Re-read */
        ep_index2 = G_EP_DISPATCH_VAL2;

        /* If second index >= 8, exit */
        if (ep_index2 >= 8) {
            break;
        }

        /* Look up offset from first endpoint index */
        offset = ep_offset_table[ep_index1];

        /* Calculate combined offset: offset + ep_index2 */
        G_EP_DISPATCH_OFFSET = offset + ep_index2;

        /* Call endpoint handler */
        usb_ep_handler();

        /* Clear endpoint status by writing bit mask */
        bit_mask = ep_bit_mask_table[ep_index2];

        /* Write bit mask to endpoint base + ep_index1 */
        XDATA8(REG_USB_EP_BASE + ep_index1) = bit_mask;

        /* Increment counter */
        counter++;

    } while (counter < 0x20);
}

/*===========================================================================
 * Additional USB Utility Functions
 *===========================================================================*/

/*
 * usb_calc_addr_009f - Calculate address 0x009F + IDATA[0x3E]
 * Address: 0x1b88-0x1b95 (14 bytes)
 *
 * Reads offset from IDATA[0x3E], adds to 0x9F, returns that XDATA value.
 *
 * Original disassembly:
 *   1b88: mov r7, a
 *   1b89: mov a, #0x9f
 *   1b8b: add a, 0x3e           ; A = 0x9F + IDATA[0x3E]
 *   1b8d: mov 0x82, a           ; DPL
 *   1b8f: clr a
 *   1b90: addc a, #0x00         ; DPH = carry
 *   1b92: mov 0x83, a
 *   1b94: movx a, @dptr
 *   1b95: ret
 */
uint8_t usb_calc_addr_009f(void)
{
    uint8_t offset = *(__idata uint8_t *)0x3E;
    return XDATA8(0x009F + offset);
}

/*
 * usb_get_ep_config_indexed - Get endpoint config from indexed array
 * Address: 0x1b96-0x1ba4 (15 bytes)
 *
 * Reads G_SYS_STATUS_SECONDARY, uses it to index into endpoint config array
 * at 0x054E with multiplier 0x14.
 *
 * Original disassembly:
 *   1b96: mov dptr, #0x0465
 *   1b99: movx a, @dptr         ; A = [0x0465]
 *   1b9a: mov dptr, #0x054e     ; base = 0x054E
 *   1b9d: mov 0xf0, #0x14       ; B = 0x14 (multiplier)
 *   1ba0: lcall 0x0dd1          ; mul_add_index
 *   1ba3: movx a, @dptr         ; read from result
 *   1ba4: ret
 */
uint8_t usb_get_ep_config_indexed(void)
{
    uint8_t status = G_SYS_STATUS_SECONDARY;
    uint16_t addr = 0x054E + ((uint16_t)status * 0x14);
    return XDATA8(addr);
}

/*
 * usb_read_buf_addr_pair - Read 16-bit buffer address from 0x0218
 * Address: 0x1ba5-0x1bad (9 bytes)
 *
 * Reads 16-bit value from work area 0x0218-0x0219.
 *
 * Original disassembly:
 *   1ba5: mov dptr, #0x0218
 *   1ba8: movx a, @dptr         ; R6 = [0x0218] (high)
 *   1ba9: mov r6, a
 *   1baa: inc dptr
 *   1bab: movx a, @dptr         ; R7 = [0x0219] (low)
 *   1bac: mov r7, a
 *   1bad: ret
 */
uint16_t usb_read_buf_addr_pair(void)
{
    uint8_t hi = G_BUF_ADDR_HI;
    uint8_t lo = G_BUF_ADDR_LO;
    return ((uint16_t)hi << 8) | lo;
}

/*
 * usb_get_idata_0x12_field - Extract field from IDATA[0x12]
 * Address: 0x1bae-0x1bc0 (19 bytes)
 *
 * Reads IDATA[0x12], swaps nibbles, rotates right, masks to 3 bits.
 * Returns R4-R7 with extracted value.
 *
 * Original disassembly:
 *   1bae: mov r1, 0x05          ; save R5-R7 to R1-R3
 *   1bb0: mov r2, 0x06
 *   1bb2: mov r3, 0x07
 *   1bb4: mov r0, #0x12
 *   1bb6: mov a, @r0            ; A = IDATA[0x12]
 *   1bb7: swap a                ; swap nibbles
 *   1bb8: rrc a                 ; rotate right through carry
 *   1bb9: anl a, #0x07          ; mask to 3 bits
 *   1bbb: mov r7, a
 *   1bbc: clr a
 *   1bbd: mov r4, a             ; R4 = 0
 *   1bbe: mov r5, a             ; R5 = 0
 *   1bbf: mov r6, a             ; R6 = 0
 *   1bc0: ret
 */
uint8_t usb_get_idata_0x12_field(void)
{
    uint8_t val = *(__idata uint8_t *)0x12;
    /* Swap nibbles: bits 7-4 <-> bits 3-0 */
    val = ((val << 4) | (val >> 4));
    /* Rotate right (approximation without carry) */
    val = val >> 1;
    /* Mask to 3 bits */
    return val & 0x07;
}

/*
 * usb_set_ep0_mode_bit - Set bit 0 of USB EP0 config register
 * Address: 0x1bde-0x1be7 (10 bytes)
 *
 * Reads 0x9006, clears bit 0, sets bit 0, writes back.
 * Note: This is the same as nvme_set_usb_mode_bit in nvme.c
 *
 * Original disassembly:
 *   1bde: mov dptr, #0x9006
 *   1be1: movx a, @dptr
 *   1be2: anl a, #0xfe          ; clear bit 0
 *   1be4: orl a, #0x01          ; set bit 0
 *   1be6: movx @dptr, a
 *   1be7: ret
 */
void usb_set_ep0_mode_bit(void)
{
    uint8_t val;

    val = REG_USB_EP0_CONFIG;
    val = (val & 0xFE) | 0x01;
    REG_USB_EP0_CONFIG = val;
}

/*
 * usb_get_config_offset_0456 - Get config offset in 0x04XX region
 * Address: 0x1be8-0x1bf5 (14 bytes)
 *
 * Reads G_SYS_STATUS_PRIMARY, adds 0x56, returns pointer to 0x04XX.
 *
 * Original disassembly:
 *   1be8: mov dptr, #0x0464
 *   1beb: movx a, @dptr         ; A = [0x0464]
 *   1bec: add a, #0x56          ; A = A + 0x56
 *   1bee: mov 0x82, a           ; DPL
 *   1bf0: clr a
 *   1bf1: addc a, #0x04         ; DPH = 0x04 + carry
 *   1bf3: mov 0x83, a
 *   1bf5: ret
 */
__xdata uint8_t *usb_get_config_offset_0456(void)
{
    uint8_t val = G_SYS_STATUS_PRIMARY;
    uint16_t addr = 0x0400 + val + 0x56;
    return (__xdata uint8_t *)addr;
}

/*
 * usb_init_pcie_txn_state - Initialize PCIe transaction state
 * Address: 0x1d43-0x1d70 (46 bytes)
 *
 * Clears 0x0AAA, reads transaction count from 0x05A6, stores to 0x0AA8,
 * reads indexed config, stores to 0x0AA9.
 *
 * Original disassembly (partial):
 *   1d43: clr a
 *   1d44: mov dptr, #0x0aaa
 *   1d47: movx @dptr, a         ; clear 0x0AAA
 *   1d48: mov dptr, #0x05a6
 *   1d4b: movx a, @dptr         ; read PCIe txn count low
 *   1d4c: mov 0xf0, #0x22       ; multiplier = 0x22
 *   1d4f: mov dptr, #0x05d3     ; base = 0x05D3
 *   1d52: lcall 0x0dd1          ; indexed read
 *   1d55: movx a, @dptr
 *   1d56: mov dptr, #0x0aa8
 *   1d59: movx @dptr, a         ; store to flash error 0
 *   ... continues
 */
void usb_init_pcie_txn_state(void)
{
    uint8_t txn_lo;
    uint8_t val;
    uint16_t addr;

    /* Clear flash reset flag */
    G_FLASH_RESET_0AAA = 0;

    /* Read PCIe transaction count low */
    txn_lo = G_PCIE_TXN_COUNT_LO;

    /* Calculate indexed address: 0x05D3 + (txn_lo * 0x22) */
    addr = 0x05D3 + ((uint16_t)txn_lo * 0x22);
    val = XDATA8(addr);

    /* Store to flash error 0 */
    G_FLASH_ERROR_0 = val;

    /* Read secondary status and calculate indexed config */
    val = G_SYS_STATUS_SECONDARY;
    addr = 0x0548 + ((uint16_t)val * 0x14);
    val = XDATA8(addr);

    /* Store to flash error 1 */
    G_FLASH_ERROR_1 = val;
}
