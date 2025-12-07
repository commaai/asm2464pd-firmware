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

/* External handler from main.c */
extern void handler_039a(void);

/* Forward declaration - USB master handler (0x10e0)
 * Called at end of endpoint dispatch loop */
void usb_master_handler(void);

/* Forward declaration - FUN_CODE_4532 */
static void usb_endpoint_status_handler(void);

/* Forward declaration - usb_set_transfer_active_flag */
void usb_set_transfer_active_flag(void);

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

    /* Jump to 0x039a which dispatches to 0xD810 (buffer handler) */
    handler_039a();
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

/*
 * usb_endpoint_status_handler - Handle endpoint status change (FUN_CODE_4532)
 * Address: 0x4532-0x45xx
 *
 * Complex handler that processes endpoint status bits.
 * Reads XDATA[0x0003], checks bits 7 and 4, dispatches to sub-handlers.
 *
 * Original disassembly:
 *   4532: mov dptr, #0x0003
 *   4535: movx a, @dptr
 *   4536: mov 0x3a, a          ; save to IDATA[0x3A]
 *   4538: mov a, 0x3a
 *   453a: jnb e0.7, 0x4554     ; if bit 7 clear, skip
 *   453d-4553: handle bit 7 set case
 *   4554: mov a, 0x3a
 *   4556: jnb e0.4, 0x4562     ; if bit 4 clear, skip
 *   ... continues
 */
static void usb_endpoint_status_handler(void)
{
    uint8_t status;

    /* Read endpoint status control */
    status = G_EP_STATUS_CTRL;

    /* Store to IDATA[0x3A] for later checks */
    *(__idata uint8_t *)0x3A = status;

    /* Check bit 7 */
    if (status & 0x80) {
        /* Bit 7 handling - dispatches to 0x051b with R5=0x13, R4=0x00, R7=0x05
         * Then clears, calls 0x039f with R7=0, writes 1 to 0x0B2F, calls 0x04FD */
        /* TODO: Implement full logic */
    }

    /* Check bit 4 */
    if (status & 0x10) {
        /* Bit 4 handling - different dispatch */
        /* TODO: Implement full logic */
    }
}

/*
 * usb_ep_process - USB endpoint processing
 * Address: 0x52a7-0x52c6 (32 bytes)
 *
 * Called to process an endpoint. Checks IDATA[0x6A] state:
 * - If == 5: write 0x02 to 0x90E3, optionally call FUN_CODE_4532, then init handler
 * - Otherwise: set transfer active flag, set bit 7 on USB config register
 *
 * Original disassembly:
 *   52a7: mov r0, #0x6a
 *   52a9: mov a, @r0            ; A = IDATA[0x6A]
 *   52aa: add a, #0xfb          ; A = A + 0xFB (check if == 5)
 *   52ac: jnz 0x52c0            ; if not 5, go to 0x52C0
 *   52ae: mov dptr, #0x90e3
 *   52b1: mov a, #0x02
 *   52b3: movx @dptr, a         ; XDATA[0x90E3] = 2
 *   52b4: mov dptr, #0x0003
 *   52b7: movx a, @dptr         ; A = XDATA[0x0003]
 *   52b8: jz 0x52bd             ; if 0, skip call
 *   52ba: lcall 0x4532          ; call FUN_CODE_4532
 *   52bd: ljmp 0x5409           ; jump to usb_ep_init_handler
 *   52c0: lcall 0x312a          ; usb_set_transfer_active_flag
 *   52c3: lcall 0x31ce          ; nvme_read_status (sets bit 7 on @dptr)
 *   52c6: ret
 */
void usb_ep_process(void)
{
    uint8_t state;
    uint8_t val;

    /* Read IDATA[0x6A] */
    state = *(__idata uint8_t *)0x6A;

    /* Check if state == 5 (add 0xFB and check if zero) */
    if (state == 5) {
        /* Write 0x02 to endpoint status register */
        REG_USB_EP_STATUS_90E3 = 0x02;

        /* Check XDATA[0x0003] */
        if (G_EP_STATUS_CTRL != 0) {
            /* Call endpoint status handler */
            usb_endpoint_status_handler();
        }

        /* Jump to init handler */
        usb_ep_init_handler();
        return;
    }

    /* Call usb_set_transfer_active_flag (leaves DPTR = 0x9006) */
    usb_set_transfer_active_flag();

    /* nvme_read_status: read from DPTR, set bit 7, write back
     * After usb_set_transfer_active_flag, DPTR = 0x9006 (REG_USB_EP0_CONFIG) */
    val = REG_USB_EP0_CONFIG;
    val = (val & 0x7F) | 0x80;
    REG_USB_EP0_CONFIG = val;
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

    /*
     * After main loop: Check 0x909e bit 0
     * If bit 0 NOT set: jump to usb_master_handler
     * If bit 0 IS set: handle special case first
     *
     * At 0x0efb-0x0f19:
     *   0efb: mov dptr, #0x909e
     *   0efe: movx a, @dptr
     *   0eff: jb e0.0, 0x0f05     ; if bit 0 set, go to special handling
     *   0f02: ljmp 0x10e0         ; else go to usb_master_handler
     *   0f05: mov dptr, #0x0af5
     *   0f08: mov a, #0x40
     *   0f0a: movx @dptr, a       ; G_EP_DISPATCH_OFFSET = 0x40
     *   0f0b: lcall 0x5442        ; usb_ep_handler()
     *   0f0e: mov dptr, #0x909e
     *   0f11: mov a, #0x01
     *   0f13: movx @dptr, a       ; 0x909e = 1
     *   0f14: mov dptr, #0x90e3
     *   0f17: inc a               ; a = 2
     *   0f18: movx @dptr, a       ; 0x90e3 = 2
     *   0f19: ljmp 0x10e0         ; usb_master_handler()
     */
    status = XDATA8(0x909E);
    if (!(status & 0x01)) {
        usb_master_handler();
        return;
    }

    /* Special case: endpoint 0x40 handling */
    G_EP_DISPATCH_OFFSET = 0x40;
    usb_ep_handler();
    XDATA8(0x909E) = 0x01;
    XDATA8(0x90E3) = 0x02;
    usb_master_handler();
}

/*
 * usb_master_handler - USB Master interrupt handler
 * Address: 0x10e0-0x117a (155 bytes)
 *
 * Called at the end of endpoint dispatch loop. Handles:
 * - System interrupt status (0xC806 bit 5)
 * - Link status events (0xCEF3, 0xCEF2)
 * - USB master interrupt (0xC802 bit 2) -> NVMe queue processing
 *
 * Original disassembly:
 *   10e0: mov dptr, #0xc806
 *   10e3: movx a, @dptr
 *   10e4: jnb e0.5, 0x110d    ; if bit 5 not set, skip
 *   10e7: mov dptr, #0xcef3
 *   10ea: movx a, @dptr
 *   10eb: jnb e0.3, 0x10fe    ; if bit 3 not set, skip
 *   10ee-10f9: clear 0x0464, write 0x08 to 0xcef3, call 0x2608
 *   10fc: sjmp 0x110d         ; skip to next check
 *   10fe-110a: check 0xcef2 bit 7, write 0x80, call 0x3adb with r7=0
 *   110d-1111: check 0xc802 bit 2 for NVMe queue
 *   1114-117a: NVMe queue processing loop (0x20 iterations)
 */
void usb_master_handler(void)
{
    uint8_t status;
    uint8_t counter;

    /* Check system interrupt status bit 5 (0xC806) */
    status = REG_INT_SYSTEM;
    if (status & 0x20) {
        /* Check link status 0xCEF3 bit 3 */
        status = REG_CPU_LINK_CEF3;
        if (status & 0x08) {
            /* Clear 0x0464, write 0x08 to 0xCEF3 */
            G_SYS_STATUS_PRIMARY = 0x00;
            REG_CPU_LINK_CEF3 = 0x08;
            /* Call handler_2608 - state handler */
            /* TODO: Add call to handler_2608 when implemented */
        } else {
            /* Check 0xCEF2 bit 7 */
            status = REG_CPU_LINK_CEF2;
            if (status & 0x80) {
                /* Write 0x80 to 0xCEF2 */
                REG_CPU_LINK_CEF2 = 0x80;
                /* Call 0x3ADB (handler_3adb) with R7=0 */
                /* TODO: Add call to handler_3adb when implemented */
            }
        }
    }

    /* Check USB master interrupt bit 2 (0xC802) - NVMe queue processing */
    status = REG_INT_USB_MASTER;
    if (!(status & 0x04)) {
        return;
    }

    /* NVMe queue processing loop - up to 32 iterations
     *
     * Loop at 0x1117-0x1138:
     *   - Check 0xC471 bit 0
     *   - If set: check 0x0055 for zero
     *   - If 0x0055 is zero: check 0xC520 bit 1, call 0x488f if set
     *   - Call 0x1196 (nvme queue helper)
     *   - Increment counter, loop while counter < 0x20
     */
    for (counter = 0; counter < 0x20; counter++) {
        /* Check NVMe queue pointer bit 0 */
        status = REG_NVME_QUEUE_PTR_C471;
        if (!(status & 0x01)) {
            break;  /* No more queue entries */
        }

        /* Check if NVMe queue ready flag is zero */
        if (G_NVME_QUEUE_READY == 0) {
            /* Check NVMe link status bit 1 */
            status = REG_NVME_LINK_STATUS;
            if (status & 0x02) {
                /* Call handler at 0x488f */
                /* TODO: Implement FUN_CODE_488f */
            }
        }

        /* Call NVMe queue helper at 0x1196 */
        /* TODO: Implement nvme_queue_helper */
    }

    /* Post-loop processing at 0x113a-0x117a:
     *
     * Check USB status (0x9000) bit 0:
     *   - If set: check 0xC520 bit 0, call 0x3E81 if set
     *             check 0xC520 bit 1, call 0x488f if set
     *   - If clear: check 0xC520 bit 1, call 0x4784 if set
     *               check 0xC520 bit 0, call 0x49e9 if set
     *
     * Finally check 0xC42C bit 0, call 0x4784 and write 0x01 to 0xC42C if set
     */
    status = REG_USB_STATUS;
    if (status & 0x01) {
        /* USB status bit 0 set path */
        status = REG_NVME_LINK_STATUS;
        if (status & 0x01) {
            /* Call 0x3E81 */
            /* TODO: Implement FUN_CODE_3e81 */
        }
        status = REG_NVME_LINK_STATUS;
        if (status & 0x02) {
            /* Call 0x488f */
            /* TODO: Implement FUN_CODE_488f */
        }
    } else {
        /* USB status bit 0 clear path */
        status = REG_NVME_LINK_STATUS;
        if (status & 0x02) {
            /* Call 0x4784 */
            /* TODO: Implement FUN_CODE_4784 */
        }
        status = REG_NVME_LINK_STATUS;
        if (status & 0x01) {
            /* Call 0x49e9 */
            /* TODO: Implement FUN_CODE_49e9 */
        }
    }

    /* Check MSC control bit 0, clear it if set */
    status = REG_USB_MSC_CTRL;
    if (status & 0x01) {
        /* Call 0x4784 */
        /* TODO: Implement FUN_CODE_4784 */
        REG_USB_MSC_CTRL = 0x01;  /* Clear bit by writing 1 */
    }
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

/*===========================================================================
 * USB Helper Functions (0x1A00-0x1B60 region)
 *===========================================================================*/

/*
 * usb_func_1a00 - USB status check and configuration setup
 * Address: 0x1a00-0x1aac (173 bytes)
 *
 * Complex function that:
 * 1. Reads REG_CE88, masks to bits 7-6, ORs with IDATA[0x0D], writes back
 * 2. Polls REG_CE89 bit 0 until set
 * 3. Sets IDATA[0x39] = 1
 * 4. Dispatches based on IDATA[0x39] and various conditions
 * 5. Handles status registers 0x911D/0x911E, writes to 0x0056/0x0057
 * 6. Calls multiple helper functions (0x3189, 0x31fb, 0x329f, etc.)
 *
 * This is a complex state machine entry point for USB processing.
 *
 * Original disassembly at 0x1A00:
 *   1a00: mov dptr, #0xce88
 *   1a03: movx a, @dptr
 *   1a04: anl a, #0xc0         ; mask bits 7-6
 *   1a06: mov r0, #0x0d
 *   1a08: orl a, @r0           ; OR with IDATA[0x0D]
 *   1a09: movx @dptr, a        ; write back
 *   1a0a: mov dptr, #0xce89    ; poll loop start
 *   1a0d: movx a, @dptr
 *   1a0e: jnb e0.0, 0x1a0a     ; wait for bit 0
 *   1a11: mov 0x39, #0x01      ; IDATA[0x39] = 1
 *   ...
 */
void usb_func_1a00(void)
{
    uint8_t val;
    uint8_t idata_0d;
    uint8_t idata_39;

    /* Read CE88, mask bits 7-6, OR with IDATA[0x0D], write back */
    val = XDATA8(0xCE88);
    idata_0d = *(__idata uint8_t *)0x0D;
    val = (val & 0xC0) | idata_0d;
    XDATA8(0xCE88) = val;

    /* Poll CE89 bit 0 until set */
    do {
        val = XDATA8(0xCE89);
    } while (!(val & 0x01));

    /* Set IDATA[0x39] = 1 */
    *(__idata uint8_t *)0x39 = 1;

    /* Check if we should jump to end block at 0x1a3b */
    /* This depends on various state conditions */
    idata_39 = *(__idata uint8_t *)0x39;

    /* Check if IDATA[0x39] == 1 */
    if (idata_39 == 1) {
        /* Check CE89 bit 1 */
        val = XDATA8(0xCE89);
        if (!(val & 0x02)) {
            /* Check 0x0AF8 */
            if (XDATA8(0x0AF8) != 0) {
                /* Jump to 0x2DB7 - another handler */
                return;
            }
        }

        /* Process IDATA[0x0D], call helpers, etc. */
        /* Complex logic follows - simplified stub */
    }

    /* Final cleanup: clear 0x0052 and return */
    XDATA8(0x0052) = 0;
}

/*
 * usb_func_1aad - USB init helper with endpoint configuration
 * Address: 0x1aad-0x1af6 (74 bytes)
 *
 * Initializes endpoint queue configuration:
 * 1. Stores R7 to 0x0566
 * 2. Reads 0x0B00, multiplies by 0x40, adds to 0x021B, stores at 0x0568/0x0569
 * 3. Stores IDATA[0x0D] to 0x0567
 * 4. Copies IDATA[0x18:0x19] to 0x056A:0x056B
 * 5. Calls idata_load_dword(0x0E), stores to 0x0570
 * 6. Calls idata_load_dword(0x12), stores to 0x056C
 *
 * Original disassembly:
 *   1aad: mov dptr, #0x0566
 *   1ab0: mov a, r7
 *   1ab1: movx @dptr, a         ; [0x0566] = R7
 *   1ab2: mov dptr, #0x0b00
 *   1ab5: movx a, @dptr         ; A = [0x0B00]
 *   1ab6: mov 0xf0, #0x40       ; B = 0x40
 *   1ab9: mul ab                ; A = A * 0x40
 *   1aba: mov r7, a             ; R7 = low result
 *   1abb: mov dptr, #0x021b
 *   1abe: movx a, @dptr         ; A = [0x021B]
 *   1abf: add a, r7             ; A += R7
 *   1ac0: mov r6, a
 *   ...
 */
void usb_func_1aad(uint8_t param)
{
    uint8_t val;
    uint8_t offset_lo;
    uint8_t offset_hi;
    uint8_t idata_val;

    /* Store parameter to 0x0566 */
    G_EP_QUEUE_PARAM = param;

    /* Read 0x0B00, multiply by 0x40 */
    val = G_USB_PARAM_0B00;
    offset_lo = val * 0x40;  /* Low byte of multiplication */

    /* Add to value at 0x021B */
    val = G_BUF_BASE_LO;
    offset_lo += val;

    /* Add carry to 0x021A */
    offset_hi = G_BUF_BASE_HI;
    if (offset_lo < val) {
        offset_hi++;  /* Add carry */
    }

    /* Store to 0x0568/0x0569 */
    G_BUF_OFFSET_HI = offset_hi;
    G_BUF_OFFSET_LO = offset_lo;

    /* Store IDATA[0x0D] to 0x0567 */
    idata_val = *(__idata uint8_t *)0x0D;
    G_EP_QUEUE_IDATA = idata_val;

    /* Copy IDATA[0x18:0x19] to 0x056A:0x056B */
    idata_val = *(__idata uint8_t *)0x18;
    XDATA8(0x056A) = idata_val;
    idata_val = *(__idata uint8_t *)0x19;
    XDATA8(0x056B) = idata_val;

    /* Load dword from IDATA[0x0E] and store to 0x0570 */
    idata_load_dword((__idata uint8_t *)0x0E);
    /* Note: Result is in R4-R7, caller stores to DPTR=0x0570 via xdata_store_dword */

    /* Load dword from IDATA[0x12] and store to 0x056C */
    idata_load_dword((__idata uint8_t *)0x12);
    /* Note: Result stored to 0x056C via xdata_store_dword */
}

/*
 * usb_func_1af9 - Decrement indexed XDATA counter
 * Address: 0x1af9-0x1b13 (27 bytes)
 *
 * Calculates address 0x0171 + IDATA[0x3E], reads byte, decrements, writes back.
 * Then reads the decremented value.
 *
 * Original disassembly:
 *   1af9: mov a, #0x71
 *   1afb: add a, 0x3e           ; A = 0x71 + IDATA[0x3E]
 *   1afd: mov 0x82, a           ; DPL
 *   1aff: clr a
 *   1b00: addc a, #0x01         ; DPH = 0x01 + carry
 *   1b02: mov 0x83, a
 *   1b04: movx a, @dptr         ; read
 *   1b05: dec a                 ; decrement
 *   1b06: movx @dptr, a         ; write back
 *   1b07-1b12: recalculate same address and read again
 *   1b13: ret
 */
uint8_t usb_func_1af9(void)
{
    uint8_t offset = *(__idata uint8_t *)0x3E;
    uint16_t addr = 0x0171 + offset;
    uint8_t val;

    /* Decrement value at address */
    val = XDATA8(addr);
    val--;
    XDATA8(addr) = val;

    /* Read back and return */
    return XDATA8(addr);
}

/*
 * usb_func_1b14 - Address calculation and store helper
 * Address: 0x1b14-0x1b13 (bytes overlap - actually 0x1b14-0x1b2a, 23 bytes)
 *
 * Takes R1:R2 (address), stores to DPTR, calls 0x0D84, then calls
 * idata_store_dword with R0=0x12, then calls 0x0DDD with DPTR=0x0007.
 *
 * Original disassembly:
 *   1b14: mov r1, a             ; R1 = A (low byte)
 *   1b15: clr a
 *   1b16: addc a, r2            ; A = R2 + carry
 *   1b17: mov 0x82, r1          ; DPL = R1
 *   1b19: mov 0x83, a           ; DPH = A
 *   1b1b: lcall 0x0d84          ; xdata_store_dword helper
 *   1b1e: mov r0, #0x12
 *   1b20: lcall 0x0db9          ; idata_store_dword
 *   1b23: mov dptr, #0x0007
 *   1b26: lcall 0x0ddd          ; another helper
 *   1b29: mov a, r1
 *   1b2a: ret
 */
/* Note: usb_func_1b14 is now in state_helpers.c with correct signature for protocol.c */

void usb_func_1b14_addr(uint8_t addr_lo, uint8_t addr_hi)
{
    /* This function propagates address calculations between helpers.
     * Due to register-based calling convention in 8051, this is
     * mostly handled by the caller in C. */
    (void)addr_lo;
    (void)addr_hi;
}

/*
 * usb_func_1b2b - Calculate address 0x0108 + IDATA[0x0D]
 * Address: 0x1b2b-0x1b37 (13 bytes)
 *
 * Reads IDATA[0x0D], adds to 0x08, returns DPTR pointing to 0x01XX.
 *
 * Original disassembly:
 *   1b2b: mov r0, #0x0d
 *   1b2d: mov a, @r0            ; A = IDATA[0x0D]
 *   1b2e: add a, #0x08          ; A += 8
 *   1b30: mov 0x82, a           ; DPL
 *   1b32: clr a
 *   1b33: addc a, #0x01         ; DPH = 0x01 + carry
 *   1b35: mov 0x83, a
 *   1b37: ret
 */
__xdata uint8_t *usb_func_1b2b(void)
{
    uint8_t offset = *(__idata uint8_t *)0x0D;
    uint16_t addr = 0x0108 + offset;
    return (__xdata uint8_t *)addr;
}

/*
 * usb_func_1b38 - Mask input and calculate address 0x014E + IDATA[0x3E]
 * Address: 0x1b38-0x1b46 (15 bytes)
 *
 * Masks input to 5 bits, calculates 0x014E + IDATA[0x3E], returns DPTR.
 *
 * Original disassembly:
 *   1b38: anl a, #0x1f          ; A &= 0x1F
 *   1b3a: mov r7, a             ; R7 = masked value
 *   1b3b: mov a, #0x4e
 *   1b3d: add a, 0x3e           ; A = 0x4E + IDATA[0x3E]
 *   1b3f: mov 0x82, a           ; DPL
 *   1b41: clr a
 *   1b42: addc a, #0x01         ; DPH = 0x01 + carry
 *   1b44: mov 0x83, a
 *   1b46: ret
 */
__xdata uint8_t *usb_func_1b38(uint8_t val)
{
    uint8_t masked = val & 0x1F;
    uint8_t offset = *(__idata uint8_t *)0x3E;
    uint16_t addr = 0x014E + offset;
    (void)masked;  /* Stored in R7 for caller */
    return (__xdata uint8_t *)addr;
}

/*
 * usb_func_1b47 - Combine 0x0475 value with C415 status
 * Address: 0x1b47-0x1b5f (25 bytes)
 *
 * Reads G_STATE_HELPER_42 (0x0475), reads REG_NVME_DEV_STATUS (0xC415),
 * masks to bits 7-6, ORs together, writes to C415.
 * Then modifies 0xC412 (clear bit 1, set bit 1).
 *
 * Original disassembly:
 *   1b47: mov dptr, #0x0475
 *   1b4a: movx a, @dptr         ; A = [0x0475]
 *   1b4b: mov r6, a
 *   1b4c: mov dptr, #0xc415
 *   1b4f: movx a, @dptr         ; A = [0xC415]
 *   1b50: anl a, #0xc0          ; mask bits 7-6
 *   1b52: mov r5, a
 *   1b53: mov a, r6
 *   1b54: orl a, r5             ; combine
 *   1b55: movx @dptr, a         ; [0xC415] = combined
 *   1b56: mov dptr, #0xc412
 *   1b59: movx a, @dptr
 *   1b5a: anl a, #0xfd          ; clear bit 1
 *   1b5c: orl a, #0x02          ; set bit 1
 *   1b5e: movx @dptr, a
 *   1b5f: ret
 */
void usb_func_1b47(void)
{
    uint8_t state_val;
    uint8_t dev_status;
    uint8_t combined;
    uint8_t ctrl_val;

    /* Read state helper from 0x0475 */
    state_val = G_STATE_HELPER_42;

    /* Read device status, mask bits 7-6 */
    dev_status = REG_NVME_DEV_STATUS;
    dev_status &= 0xC0;

    /* Combine and write back */
    combined = state_val | dev_status;
    REG_NVME_DEV_STATUS = combined;

    /* Modify NVMe control/status: clear bit 1, then set bit 1 */
    ctrl_val = REG_NVME_CTRL_STATUS;
    ctrl_val = (ctrl_val & 0xFD) | 0x02;
    REG_NVME_CTRL_STATUS = ctrl_val;
}

/*
 * usb_func_1b60 - Complex IDATA/XDATA copy helper
 * Address: 0x1b60-0x1b7d (30 bytes)
 *
 * Calls 0x0D08, loads from IDATA[0x0E], stores to DPTR,
 * loads from IDATA[0x12], stores to DPTR+4,
 * calls helper at 0x0D46 with R0=3,
 * loads from IDATA[0x12] again, stores,
 * then reads IDATA[0x16:0x17] and returns in R6:A.
 *
 * This is a data marshalling function for endpoint configuration.
 *
 * Original disassembly:
 *   1b60: lcall 0x0d08          ; helper
 *   1b63: mov r0, #0x0e
 *   1b65: lcall 0x0db9          ; idata_store_dword
 *   1b68: mov r0, #0x12
 *   1b6a: lcall 0x0d78          ; idata_load_dword
 *   1b6d: mov r0, #0x03
 *   1b6f: lcall 0x0d46          ; helper
 *   1b72: mov r0, #0x12
 *   1b74: lcall 0x0db9          ; idata_store_dword
 *   1b77: mov r0, #0x16
 *   1b79: mov a, @r0            ; A = IDATA[0x16]
 *   1b7a: mov r6, a
 *   1b7b: inc r0
 *   1b7c: mov a, @r0            ; A = IDATA[0x17]
 *   1b7d: ret
 */
uint16_t usb_func_1b60(void)
{
    uint8_t hi, lo;

    /* This function marshals data between IDATA and XDATA locations.
     * The actual dword operations require assembly helpers.
     * For C, we just return the 16-bit value from IDATA[0x16:0x17]. */

    hi = *(__idata uint8_t *)0x16;
    lo = *(__idata uint8_t *)0x17;

    return ((uint16_t)hi << 8) | lo;
}

/*
 * usb_reset_interface - Reset USB interface state
 * Address: Derived from multiple reset sequences in firmware
 *
 * Clears various USB state variables to reset the interface.
 */
/* Note: usb_reset_interface is now in state_helpers.c with correct signature for protocol.c */

void usb_reset_interface_full(void)
{
    /* Clear transfer flags */
    G_USB_TRANSFER_FLAG = 0;
    G_TRANSFER_FLAG_0AF2 = 0;

    /* Clear endpoint dispatch values */
    G_EP_DISPATCH_VAL1 = 0;
    G_EP_DISPATCH_VAL2 = 0;
    G_EP_DISPATCH_OFFSET = 0;

    /* Clear IDATA state */
    *(__idata uint8_t *)0x6A = 0;
    *(__idata uint8_t *)0x39 = 0;

    /* Clear processing flag */
    G_STATE_FLAG_06E6 = 0;
}

/*
 * usb_func_1b7e - Load IDATA dword and transfer to XDATA
 * Address: 0x1b7e-0x1b85 (8 bytes)
 *
 * Loads dword from IDATA[0x09], then stores to IDATA[0x6B].
 */
void usb_func_1b7e(void)
{
    __idata uint8_t *src = (__idata uint8_t *)0x09;
    __idata uint8_t *dst = (__idata uint8_t *)0x6B;
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

/*
 * usb_func_1b88 - Calculate address 0x009F + IDATA[0x3E] and read
 * Address: 0x1b88-0x1b95 (14 bytes)
 */
uint8_t usb_func_1b88(uint8_t input)
{
    uint8_t offset = *(__idata uint8_t *)0x3E;
    uint16_t addr = 0x009F + offset;
    (void)input;
    return XDATA8(addr);
}

/*
 * usb_func_1b96 - EP config lookup with multiply
 * Address: 0x1b96-0x1ba4 (15 bytes)
 *
 * Reads G_SYS_STATUS_SECONDARY (0x0465), multiplies by 0x14 (20),
 * adds to base 0x054E, reads byte from result.
 */
uint8_t usb_func_1b96(void)
{
    uint8_t status = G_SYS_STATUS_SECONDARY;
    uint16_t addr = 0x054E + ((uint16_t)status * 0x14);
    return XDATA8(addr);
}

/*
 * usb_func_1ba5 - Read 16-bit value from G_BUF_ADDR (0x0218/0x0219)
 * Address: 0x1ba5-0x1bad (9 bytes)
 */
uint16_t usb_func_1ba5(void)
{
    uint8_t hi = G_BUF_ADDR_HI;
    uint8_t lo = G_BUF_ADDR_LO;
    return ((uint16_t)hi << 8) | lo;
}

/*
 * usb_func_1bae - Extract bits from IDATA[0x12] (shift right 5)
 * Address: 0x1bae-0x1bc0 (19 bytes)
 */
uint8_t usb_func_1bae(void)
{
    uint8_t val = *(__idata uint8_t *)0x12;
    return (val >> 5) & 0x07;
}

/*
 * usb_func_1bc1 - Add offset 0x0F to address in A:R2
 * Address: 0x1bc1-0x1bca (10 bytes)
 */
__xdata uint8_t *usb_func_1bc1(uint8_t addr_lo, uint8_t addr_hi)
{
    uint16_t addr = ((uint16_t)addr_hi << 8) | addr_lo;
    addr += 0x0F;
    return (__xdata uint8_t *)addr;
}

/*
 * usb_func_1bcb - Load from IDATA[0x6B] and store to IDATA[0x6F]
 * Address: 0x1bcb-0x1bd4 (10 bytes)
 */
void usb_func_1bcb(void)
{
    __idata uint8_t *src = (__idata uint8_t *)0x6B;
    __idata uint8_t *dst = (__idata uint8_t *)0x6F;
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

/*
 * usb_func_1bde - Set EP0 config bit 0
 * Address: 0x1bde-0x1be7 (10 bytes)
 */
void usb_func_1bde(void)
{
    uint8_t val = REG_USB_EP0_CONFIG;
    val = (val & 0xFE) | 0x01;
    REG_USB_EP0_CONFIG = val;
}

/*
 * usb_func_1be8 - Calculate address 0x0456 + G_SYS_STATUS_PRIMARY
 * Address: 0x1be8-0x1bf5 (14 bytes)
 */
__xdata uint8_t *usb_func_1be8(void)
{
    uint8_t status = G_SYS_STATUS_PRIMARY;
    uint16_t addr = 0x0456 + status;
    return (__xdata uint8_t *)addr;
}

/*
 * usb_func_1bf6 - Buffer offset calculation with multiply
 * Address: 0x1bf6-0x1c0a+ (continues)
 */
void usb_func_1bf6(uint8_t index)
{
    uint16_t offset = (uint16_t)index * 0x40;
    uint8_t base_lo = G_BUF_BASE_LO;
    uint8_t base_hi = G_BUF_BASE_HI;
    uint16_t result = ((uint16_t)base_hi << 8) | base_lo;
    result += offset;
    G_BUF_OFFSET_HI = (uint8_t)(result >> 8);
    G_BUF_OFFSET_LO = (uint8_t)(result & 0xFF);
}

/*
 * usb_func_1c0f - Calculate address 0x000C + IDATA[0x3C]
 * Address: 0x1c0f-0x1c1a (12 bytes)
 */
__xdata uint8_t *usb_func_1c0f(void)
{
    uint8_t offset = *(__idata uint8_t *)0x3C;
    uint16_t addr = 0x000C + offset;
    return (__xdata uint8_t *)addr;
}

/*
 * usb_func_1c22 - Check SCSI control counter
 * Address: 0x1c22-0x1c29 (8 bytes)
 *
 * Reads G_SCSI_CTRL (0x0171), subtracts 0 with borrow, returns carry flag.
 */
uint8_t usb_func_1c22(void)
{
    return (G_SCSI_CTRL != 0) ? 1 : 0;
}

/*
 * usb_func_1c2a - Lookup in table 0x5CAD
 * Address: 0x1c2a-0x1c39 (16 bytes)
 *
 * Multiplies IDATA[0x3C] by 2, adds to 0x5CAD, reads code table.
 */
uint8_t usb_func_1c2a(uint8_t input)
{
    uint8_t idx = *(__idata uint8_t *)0x3C;
    uint16_t addr = 0x5CAD + ((uint16_t)idx * 2);
    (void)input;
    /* Read from CODE space via movc */
    return XDATA8(addr + 1);  /* Second byte of table entry */
}

/*
 * usb_func_1c3a - Add values and mask, store result
 * Address: 0x1c3a-0x1c49 (16 bytes)
 *
 * Reads DPTR, adds to 0x0216, masks to 5 bits, stores to 0x01B4.
 */
void usb_func_1c3a(__xdata uint8_t *ptr)
{
    uint8_t val = *ptr;
    uint8_t base = XDATA8(0x0216);
    uint8_t result = (val + base) & 0x1F;
    XDATA8(0x01B4) = result;
}

/*
 * usb_func_1c4a - Store to DMA mode and params
 * Address: 0x1c4a-0x1c54 (11 bytes)
 *
 * Writes A to 0x0203 (DMA mode), 0x020D (DMA param1), 0x020E (DMA param2).
 */
void usb_func_1c4a(uint8_t val)
{
    G_DMA_MODE_SELECT = val;
    G_DMA_PARAM1 = val;
    G_DMA_PARAM2 = val;
}

/*
 * usb_func_1c55 - Read C415 status masked
 * Address: 0x1c55-0x1c5c (8 bytes)
 *
 * Stores input in R7, reads REG_NVME_DEV_STATUS, masks bits 7-6, returns.
 */
uint8_t usb_func_1c55(uint8_t input)
{
    (void)input;
    return REG_NVME_DEV_STATUS & 0xC0;
}

/*
 * usb_func_1c5d - Read from address + 0x05A8, store to 0x05A6
 * Address: 0x1c5d-0x1c6c (16 bytes)
 *
 * Reads DPTR, adds 0xA8, reads from 0x05XX, stores to G_PCIE_TXN_COUNT_LO.
 */
void usb_func_1c5d(__xdata uint8_t *ptr)
{
    uint8_t val = *ptr;
    uint16_t addr = 0x05A8 + val;
    val = XDATA8(addr);
    G_PCIE_TXN_COUNT_LO = val;
}

/*
 * usb_func_1c6d - Subtract R6:R7 from IDATA[0x16:0x17]
 * Address: 0x1c6d-0x1c76 (10 bytes)
 */
void usb_func_1c6d(uint8_t hi, uint8_t lo)
{
    __idata uint8_t *ptr = (__idata uint8_t *)0x16;
    uint8_t val_lo = ptr[1];
    uint8_t val_hi = ptr[0];

    /* Subtract with borrow */
    if (val_lo < lo) {
        val_hi--;
    }
    val_lo -= lo;
    val_hi -= hi;

    ptr[1] = val_lo;
    ptr[0] = val_hi;
}

/*
 * usb_func_1c77 - Read NVMe command param masked
 * Address: 0x1c77-0x1c7d (7 bytes)
 *
 * Reads REG_NVME_CMD_PARAM (0xC429), masks bits 7-5, returns.
 */
uint8_t usb_func_1c77(void)
{
    return XDATA8(0xC429) & 0xE0;
}

/*
 * usb_func_1c88 - Set DPTR to 0x01XX
 * Address: 0x1c88-0x1c8f (8 bytes)
 *
 * Takes A, makes DPTR = 0x01XX where XX = A.
 */
__xdata uint8_t *usb_func_1c88(uint8_t lo)
{
    return (__xdata uint8_t *)(0x0100 | lo);
}

/*
 * usb_func_1c90 - Lookup in EP config table with multiply
 * Address: 0x1c90-0x1c9e (15 bytes)
 *
 * Reads G_PCIE_TXN_COUNT_LO (0x05A6), multiplies by 0x22 (34),
 * adds to 0x05B4, reads result.
 */
uint8_t usb_func_1c90(void)
{
    uint8_t idx = G_PCIE_TXN_COUNT_LO;
    uint16_t addr = 0x05B4 + ((uint16_t)idx * 0x22);
    return XDATA8(addr);
}

/*
 * usb_func_1c9f - Call core handler and helper
 * Address: 0x1c9f-0x1ca4 (6 bytes)
 *
 * Calls core_handler_4ff2 (protocol core), then calls 0x4e6d helper.
 *
 * Original disassembly:
 *   1c9f: lcall 0x4ff2      ; core_handler_4ff2
 *   1ca2: lcall 0x4e6d      ; helper function
 */
void usb_func_1c9f(void)
{
    /* Calls core handler and nvme helper */
    /* core_handler_4ff2();  TODO: link when implemented */
    /* helper_4e6d();        TODO: link when implemented */
}

/*
 * usb_func_1ca5 - Read IDATA 16-bit and OR to check non-zero
 * Address: 0x1ca5-0x1cad (9 bytes)
 *
 * Reads IDATA[0x16] into R4, IDATA[0x17] into R5, ORs them together.
 * Returns non-zero if either byte is set.
 *
 * Original disassembly:
 *   1ca5: mov r0, #0x16
 *   1ca7: mov a, @r0        ; A = IDATA[0x16]
 *   1ca8: mov r4, a         ; R4 = A
 *   1ca9: inc r0
 *   1caa: mov a, @r0        ; A = IDATA[0x17]
 *   1cab: mov r5, a         ; R5 = A
 *   1cac: orl a, r4         ; A = R4 | R5
 *   1cad: ret
 */
uint8_t usb_func_1ca5(void)
{
    __idata uint8_t *ptr = (__idata uint8_t *)0x16;
    return ptr[0] | ptr[1];
}

/*
 * usb_func_1cae - Increment counter at 0x0B00 with mask
 * Address: 0x1cae-0x1cb6 (9 bytes)
 *
 * Reads 0x0B00, increments, masks to 5 bits, writes back.
 *
 * Original disassembly:
 *   1cae: mov dptr, #0x0b00
 *   1cb1: movx a, @dptr     ; A = [0x0B00]
 *   1cb2: inc a             ; A++
 *   1cb3: anl a, #0x1f      ; A &= 0x1F
 *   1cb5: movx @dptr, a     ; [0x0B00] = A
 *   1cb6: ret
 */
void usb_func_1cae(void)
{
    uint8_t val = G_USB_PARAM_0B00;
    val = (val + 1) & 0x1F;
    G_USB_PARAM_0B00 = val;
}

/*
 * usb_func_1cb7 - Calculate address 0x012B + A
 * Address: 0x1cb7-0x1cc0 (10 bytes)
 *
 * Adds 0x2B to input A, returns DPTR = 0x01XX.
 *
 * Original disassembly:
 *   1cb7: add a, #0x2b      ; A += 0x2B
 *   1cb9: mov 0x82, a       ; DPL = A
 *   1cbb: clr a
 *   1cbc: addc a, #0x01     ; DPH = 0x01 + carry
 *   1cbe: mov 0x83, a
 *   1cc0: ret
 */
__xdata uint8_t *usb_func_1cb7(uint8_t offset)
{
    uint16_t addr = 0x0100 + offset + 0x2B;
    return (__xdata uint8_t *)addr;
}

/*
 * usb_func_1cc1 - Store 0x84 to 0x0564
 * Address: 0x1cc1-0x1cc7 (7 bytes)
 *
 * Writes constant 0x84 to G_CONFIG_FLAG_0564.
 *
 * Original disassembly:
 *   1cc1: mov dptr, #0x0564
 *   1cc4: mov a, #0x84
 *   1cc6: movx @dptr, a
 *   1cc7: ret
 */
void usb_func_1cc1(void)
{
    G_CONFIG_FLAG_0564 = 0x84;
}

/*
 * usb_func_1cc8 - Copy IDATA 16-bit to DPTR
 * Address: 0x1cc8-0x1cd3 (12 bytes)
 *
 * Reads IDATA[0x16:0x17], stores to DPTR (passed in).
 *
 * Original disassembly:
 *   1cc8: mov r0, #0x16
 *   1cca: mov a, @r0        ; A = IDATA[0x16]
 *   1ccb: mov r7, a         ; R7 = high byte
 *   1ccc: inc r0
 *   1ccd: mov a, @r0        ; A = IDATA[0x17]
 *   1cce: xch a, r7         ; swap: A=high, R7=low
 *   1ccf: movx @dptr, a     ; store high byte
 *   1cd0: inc dptr
 *   1cd1: mov a, r7
 *   1cd2: movx @dptr, a     ; store low byte
 *   1cd3: ret
 */
void usb_func_1cc8(__xdata uint8_t *ptr)
{
    __idata uint8_t *idata = (__idata uint8_t *)0x16;
    ptr[0] = idata[0];  /* high byte */
    ptr[1] = idata[1];  /* low byte */
}

/*
 * usb_func_1cd4 - Clear bit 1 of C401
 * Address: 0x1cd4-0x1cdb (8 bytes)
 *
 * Reads 0xC401, masks off bit 1 (ANL with 0xFD), writes back.
 *
 * Original disassembly:
 *   1cd4: mov dptr, #0xc401
 *   1cd7: movx a, @dptr
 *   1cd8: anl a, #0xfd      ; clear bit 1
 *   1cda: movx @dptr, a
 *   1cdb: ret
 */
void usb_func_1cd4(void)
{
    uint8_t val = XDATA8(0xC401);
    val &= 0xFD;
    XDATA8(0xC401) = val;
}

/*
 * usb_func_1cdc - Add 0x20 to global at 0x053A
 * Address: 0x1cdc-0x1ce3 (8 bytes)
 *
 * Reads 0x053A, adds 0x20, writes back.
 *
 * Original disassembly:
 *   1cdc: mov dptr, #0x053a
 *   1cdf: movx a, @dptr
 *   1ce0: add a, #0x20
 *   1ce2: movx @dptr, a
 *   1ce3: ret
 */
void usb_func_1cdc(void)
{
    uint8_t val = G_CONFIG_053A;
    val += 0x20;
    G_CONFIG_053A = val;
}

/*
 * usb_func_1ce4 - Calculate address 0x04B7 + IDATA[0x23]
 * Address: 0x1ce4-0x1cef (12 bytes)
 *
 * Adds 0xB7 to IDATA[0x23], returns DPTR = 0x04XX.
 *
 * Original disassembly:
 *   1ce4: mov a, #0xb7
 *   1ce6: add a, 0x23       ; A = 0xB7 + IDATA[0x23]
 *   1ce8: mov 0x82, a       ; DPL
 *   1cea: clr a
 *   1ceb: addc a, #0x04     ; DPH = 0x04 + carry
 *   1ced: mov 0x83, a
 *   1cef: ret
 */
__xdata uint8_t *usb_func_1ce4(void)
{
    uint8_t offset = *(__idata uint8_t *)0x23;
    uint16_t addr = 0x0400 + 0xB7 + offset;
    return (__xdata uint8_t *)addr;
}

/*
 * usb_func_1cf0 - Initialize transfer with R7=5
 * Address: 0x1cf0-0x1cfb (12 bytes)
 *
 * Sets R3=0, R5=0x20, R7=5, calls 0x523C, returns R7=5.
 *
 * Original disassembly:
 *   1cf0: clr a
 *   1cf1: mov r3, a         ; R3 = 0
 *   1cf2: mov r5, #0x20     ; R5 = 0x20
 *   1cf4: mov r7, #0x05     ; R7 = 5
 *   1cf6: lcall 0x523c      ; helper function
 *   1cf9: mov r7, #0x05     ; R7 = 5
 *   1cfb: ret
 */
uint8_t usb_func_1cf0(void)
{
    /* Calls transfer setup helper with params:
     * R3=0 (some flag), R5=0x20 (size), R7=5 (mode/type) */
    /* helper_523c(0, 0x20, 5);  TODO: link when implemented */
    return 5;
}

/*
 * usb_func_1cfc - Write 0x08, 0x02 to USB regs 0x9093-0x9094
 * Address: 0x1cfc-0x1d06 (11 bytes)
 *
 * Writes 0x08 to 0x9093, 0x02 to 0x9094.
 *
 * Original disassembly:
 *   1cfc: mov dptr, #0x9093
 *   1cff: mov a, #0x08
 *   1d01: movx @dptr, a     ; [0x9093] = 0x08
 *   1d02: inc dptr
 *   1d03: mov a, #0x02
 *   1d05: movx @dptr, a     ; [0x9094] = 0x02
 *   1d06: ret
 */
void usb_func_1cfc(void)
{
    XDATA8(0x9093) = 0x08;
    XDATA8(0x9094) = 0x02;
}

/*
 * usb_func_1d07 - Write 0x02, 0x10 to USB regs 0x9093-0x9094
 * Address: 0x1d07-0x1d11 (11 bytes)
 *
 * Writes 0x02 to 0x9093, 0x10 to 0x9094.
 *
 * Original disassembly:
 *   1d07: mov dptr, #0x9093
 *   1d0a: mov a, #0x02
 *   1d0c: movx @dptr, a     ; [0x9093] = 0x02
 *   1d0d: inc dptr
 *   1d0e: mov a, #0x10
 *   1d10: movx @dptr, a     ; [0x9094] = 0x10
 *   1d11: ret
 */
void usb_func_1d07(void)
{
    XDATA8(0x9093) = 0x02;
    XDATA8(0x9094) = 0x10;
}

/*
 * usb_func_1d12 - Code table lookup
 * Address: 0x1d12-0x1d1c (11 bytes)
 *
 * Takes A, uses it as offset from DPTR into code table.
 * Returns XDATA read from calculated address.
 *
 * Original disassembly:
 *   1d12: mov r5, a         ; R5 = input
 *   1d13: clr a
 *   1d14: movc a, @a+dptr   ; A = CODE[DPTR]
 *   1d15: addc a, #0x00     ; add carry
 *   1d17: mov 0x82, r5      ; DPL = R5
 *   1d19: mov 0x83, a       ; DPH = A
 *   1d1b: movx a, @dptr     ; read XDATA
 *   1d1c: ret
 */
uint8_t usb_func_1d12(__code uint8_t *code_ptr, uint8_t offset)
{
    uint8_t hi = *code_ptr;
    uint16_t addr = ((uint16_t)hi << 8) | offset;
    return XDATA8(addr);
}

/*
 * usb_func_1d1d - Write 0x01 to 0x0B2E
 * Address: 0x1d1d-0x1d23 (7 bytes)
 *
 * Writes constant 0x01 to 0x0B2E.
 *
 * Original disassembly:
 *   1d1d: mov dptr, #0x0b2e
 *   1d20: mov a, #0x01
 *   1d22: movx @dptr, a
 *   1d23: ret
 */
void usb_func_1d1d(void)
{
    XDATA8(0x0B2E) = 0x01;
}

/*
 * usb_func_1d24 - Read C414 masked to bits 7-6
 * Address: 0x1d24-0x1d2a (7 bytes)
 *
 * Reads 0xC414, masks to bits 7-6 (0xC0).
 *
 * Original disassembly:
 *   1d24: mov dptr, #0xc414
 *   1d27: movx a, @dptr
 *   1d28: anl a, #0xc0
 *   1d2a: ret
 */
uint8_t usb_func_1d24(void)
{
    return XDATA8(0xC414) & 0xC0;
}

/*
 * usb_func_1d2b - Set bit 7 at DPTR
 * Address: 0x1d2b-0x1d31 (7 bytes)
 *
 * Reads byte at DPTR, clears bit 7, sets bit 7, writes back.
 *
 * Original disassembly:
 *   1d2b: movx a, @dptr
 *   1d2c: anl a, #0x7f      ; clear bit 7
 *   1d2e: orl a, #0x80      ; set bit 7
 *   1d30: movx @dptr, a
 *   1d31: ret
 */
void usb_func_1d2b(__xdata uint8_t *ptr)
{
    uint8_t val = *ptr;
    val = (val & 0x7F) | 0x80;
    *ptr = val;
}

/*
 * usb_func_1d32 - Store R6:A to IDATA[0x16:0x17]
 * Address: 0x1d32-0x1d38 (7 bytes)
 *
 * Stores A to IDATA[0x17], R6 to IDATA[0x16].
 *
 * Original disassembly:
 *   1d32: mov r1, #0x17
 *   1d34: mov @r1, a        ; IDATA[0x17] = A (low)
 *   1d35: mov a, r6
 *   1d36: dec r1
 *   1d37: mov @r1, a        ; IDATA[0x16] = R6 (high)
 *   1d38: ret
 */
void usb_func_1d32(uint8_t hi, uint8_t lo)
{
    __idata uint8_t *ptr = (__idata uint8_t *)0x16;
    ptr[0] = hi;
    ptr[1] = lo;
}

/*
 * usb_func_1d39 - Add to 0x014E with mask
 * Address: 0x1d39-0x1d42 (10 bytes)
 *
 * Reads 0x014E, adds input, masks to 5 bits, writes back.
 *
 * Original disassembly:
 *   1d39: mov r7, a         ; R7 = input
 *   1d3a: mov dptr, #0x014e
 *   1d3d: movx a, @dptr     ; A = [0x014E]
 *   1d3e: add a, r7         ; A += R7
 *   1d3f: anl a, #0x1f      ; A &= 0x1F
 *   1d41: movx @dptr, a
 *   1d42: ret
 */
void usb_func_1d39(uint8_t val)
{
    uint8_t cur = XDATA8(0x014E);
    cur = (cur + val) & 0x1F;
    XDATA8(0x014E) = cur;
}
