/*
 * ASM2464PD Firmware - USB Driver
 *
 * USB interface controller for USB4/Thunderbolt to NVMe bridge
 * Handles USB enumeration, endpoint configuration, and data transfers
 *
 * USB registers are at 0x9000-0x91FF
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"

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
 * Address: 0x1bd7-0x???? (needs analysis)
 *
 * From ghidra.c usb_setup_endpoint:
 *   Configures endpoint parameters
 */
void usb_setup_endpoint(void)
{
    /* TODO: Implement based on 0x1bd7 disassembly */
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
    XDATA8(0x0B2E) = 0;  /* USB transfer flag */

    /* Clear IDATA[0x6A] */
    *(__idata uint8_t *)0x6A = 0;

    /* Clear processing complete flag in work area */
    XDATA8(0x06E6) = 0;  /* Processing complete flag */

    /* Original jumps to 0x039a which dispatches to 0xD810 (buffer handler) */
    /* TODO: Call buffer handler */
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
    if (XDATA8(0x000A) == 0) {
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
    REG_USB_TRANSFER_FLAG = 1;
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
    uint8_t status = REG_SYS_STATUS_PRIMARY;
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
    REG_STATE_FLAG_06E6 = 1;
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
        REG_EP_DISPATCH_VAL1 = ep_index1;

        /* Re-read (original firmware does this) */
        ep_index1 = REG_EP_DISPATCH_VAL1;

        /* If index >= 8, no endpoint to process - exit */
        if (ep_index1 >= 8) {
            break;
        }

        /* Read secondary status from endpoint base + ep_index1 */
        status = XDATA8(REG_USB_EP_BASE + ep_index1);

        /* Look up second endpoint index */
        ep_index2 = ep_index_table[status];

        /* Store to endpoint dispatch value 2 */
        REG_EP_DISPATCH_VAL2 = ep_index2;

        /* Re-read */
        ep_index2 = REG_EP_DISPATCH_VAL2;

        /* If second index >= 8, exit */
        if (ep_index2 >= 8) {
            break;
        }

        /* Look up offset from first endpoint index */
        offset = ep_offset_table[ep_index1];

        /* Calculate combined offset: offset + ep_index2 */
        REG_EP_DISPATCH_OFFSET = offset + ep_index2;

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
