/*
 * ASM2464PD Firmware - Function Stubs
 *
 * Stub implementations for functions that have not been fully reverse-engineered yet.
 * These stubs provide minimal implementations to allow the firmware to build.
 *
 * Each stub should be replaced with the actual implementation as the function is
 * reverse-engineered from the original firmware.
 */

#include "../types.h"
#include "../sfr.h"
#include "../registers.h"
#include "../globals.h"

/* External function declarations */
extern void phy_link_training(void);  /* 0xD702 - in phy.c */
extern void timer_wait(uint8_t timeout_lo, uint8_t timeout_hi, uint8_t mode);  /* 0xE80A - in timer.c */

/*===========================================================================
 * USB Transfer Functions
 *===========================================================================*/

/*
 * helper_1579 - Read PCIe transaction count and set up array access
 * Address: 0x1579-0x157c (4 bytes)
 *
 * Disassembly:
 *   1579: mov dptr, #0x05a6  ; G_PCIE_TXN_COUNT_LO
 *   157c: movx a, @dptr      ; Read value to A
 *   (falls through to helper_157d)
 *
 * This reads G_PCIE_TXN_COUNT_LO then falls through to helper_157d
 * which calculates array index into G_PCIE_DIRECTION (0x05B4) area.
 */
void helper_1579(void)
{
    /* Original just reads 0x05A6 into A and falls through to 0x157d
     * The fall-through behavior can't be directly replicated in C,
     * so we call helper_157d explicitly after reading. */
    uint8_t idx = G_PCIE_TXN_COUNT_LO;
    /* Calculate DPTR = 0x05B4 + (idx * 0x22) - done by helper_157d with A as input */
    (void)idx;  /* Used by subsequent operations via DPTR in original code */
}

/*
 * helper_157d - Set up array access with index calculation
 * Address: 0x157d-0x1585 (9 bytes)
 *
 * Disassembly:
 *   157d: mov dptr, #0x05b4  ; Base address (G_PCIE_DIRECTION area)
 *   1580: mov B, #0x22       ; Element size = 34 bytes
 *   1583: ljmp 0x0dd1        ; Array index calculation helper
 *
 * The 0x0dd1 function calculates: DPTR = DPTR + (A * B)
 * This sets DPTR to point to a 34-byte structure in an array at 0x05B4.
 */
void helper_157d(void)
{
    /* This sets up DPTR for array access - DPTR = 0x05B4 + (A * 0x22)
     * In context, A contains the index from prior call */
}

/*
 * helper_15d4 - Complete DPTR setup with carry handling
 * Address: 0x15d4-0x15db (8 bytes)
 *
 * Disassembly:
 *   15d4: mov DPL, a         ; Set low byte
 *   15d6: clr a
 *   15d7: addc a, #0x00      ; High byte with carry
 *   15d9: mov DPH, a
 *   15db: ret
 *
 * This completes a 16-bit address calculation where A contains the low byte
 * and carry flag may affect the high byte.
 *
 * Note: This function is typically called inline after an ADD that sets carry.
 * In C, we can't replicate this behavior directly - callers use the address
 * calculation pattern instead. The function returns the computed address
 * for low XDATA region (0x00xx or 0x01xx depending on carry).
 */
__xdata uint8_t *helper_15d4_ptr(uint8_t low_byte, uint8_t carry)
{
    uint16_t addr = low_byte;
    if (carry) {
        addr += 0x0100;  /* Carry propagates to high byte */
    }
    return (__xdata uint8_t *)addr;
}

/* Stub for compatibility - actual work done inline by callers */
void helper_15d4(void)
{
    /* This is a DPTR setup continuation - callers handle this inline */
}

/*
 * helper_15ef - Set DPTR to SCSI DMA parameter array element
 * Address: 0x15ef-0x15f9 (11 bytes)
 *
 * Disassembly:
 *   15ef: mov a, #0x40
 *   15f1: add a, r7          ; A = 0x40 + R7 (index)
 *   15f2: mov DPL, a
 *   15f4: clr a
 *   15f5: addc a, #0xce      ; DPH = 0xCE + carry
 *   15f7: mov DPH, a
 *   15f9: ret
 *
 * Sets DPTR = 0xCE40 + R7, pointing to SCSI DMA parameter registers.
 * The second parameter (b) is unused - likely R6 in original calling convention.
 *
 * Returns: Pointer to SCSI DMA register at 0xCE40 + index
 */
__xdata uint8_t *helper_15ef_ptr(uint8_t index)
{
    uint8_t low = 0x40 + index;
    uint16_t addr = 0xCE00 + low;  /* Carry only if low overflows */
    if (low < 0x40) {
        addr += 0x0100;  /* Handle overflow carry */
    }
    return (__xdata uint8_t *)addr;
}

/* Original signature for compatibility */
void helper_15ef(uint8_t a, uint8_t b)
{
    (void)a; (void)b;
    /* Sets DPTR = 0xCE40 + index - callers should use helper_15ef_ptr() */
}

/*
 * helper_15f1 - Set DPTR to SCSI DMA parameter (entry at add instruction)
 * Address: 0x15f1-0x15f9 (9 bytes)
 *
 * This is an alternate entry point into helper_15ef, starting at the 'add' instruction.
 * Param is added to 0x40 to form DPL, with DPH = 0xCE.
 *
 * Returns: Pointer to SCSI DMA register at 0xCE40 + param
 */
__xdata uint8_t *helper_15f1_ptr(uint8_t param)
{
    uint8_t low = 0x40 + param;
    uint16_t addr = 0xCE00 + low;
    if (low < 0x40) {
        addr += 0x0100;  /* Handle overflow carry */
    }
    return (__xdata uint8_t *)addr;
}

/* Original signature for compatibility */
void helper_15f1(uint8_t param)
{
    (void)param;
    /* Sets DPTR = 0xCE40 + param - callers should use helper_15f1_ptr() */
}

/*
 * transfer_func_1633 - Set bit 0 at specified register address
 * Address: 0x1633-0x1639 (7 bytes)
 *
 * Disassembly:
 *   1633: movx a, @dptr      ; Read current value (DPTR passed as param)
 *   1634: anl a, #0xfe       ; Clear bit 0
 *   1636: orl a, #0x01       ; Set bit 0
 *   1638: movx @dptr, a      ; Write back
 *   1639: ret
 *
 * Sets bit 0 of the register at the specified address.
 */
void transfer_func_1633(uint16_t addr)
{
    __xdata uint8_t *ptr = (__xdata uint8_t *)addr;
    uint8_t val = *ptr;
    val = (val & 0xFE) | 0x01;  /* Clear and set bit 0 */
    *ptr = val;
}

/*
 * helper_1646 - Get endpoint config value with array index calculation
 * Address: 0x1646-0x1658 (19 bytes)
 *
 * Disassembly:
 *   1646: mov dptr, #0x0465  ; G_SYS_STATUS_SECONDARY
 *   1649: movx a, @dptr      ; Read index value
 *   164a: mov B, #0x14       ; Element size = 20 bytes
 *   164d: mul ab             ; index * 20
 *   164e: add a, #0x4e       ; Add offset 0x4E
 *   1650: mov DPL, a
 *   1652: clr a
 *   1653: addc a, #0x05      ; DPH = 0x05 + carry
 *   1655: mov DPH, a
 *   1657: movx a, @dptr      ; Read value at calculated address
 *   1658: ret
 *
 * Returns: XDATA[0x054E + (G_SYS_STATUS_SECONDARY * 0x14)]
 */
uint8_t helper_1646(void)
{
    uint8_t idx = G_SYS_STATUS_SECONDARY;
    uint16_t addr = 0x054E + ((uint16_t)idx * 0x14);
    return XDATA_REG8(addr);
}

/*
 * helper_1755 - Set up address pointer (0x59 + offset)
 * Address: 0x1755
 *
 * Sets DPTR to the computed address.
 */
void helper_1755(uint8_t offset)
{
    (void)offset;
    /* TODO: Implement address setup */
}

/*
 * helper_159f - Write value via computed pointer
 * Address: 0x159f
 *
 * Writes the parameter to the address set up by prior helper.
 */
void helper_159f(uint8_t value)
{
    (void)value;
    /* TODO: Implement write operation */
}

/*
 * helper_166f - Set DPTR based on I_WORK_43
 * Address: 0x166f-0x1676 (8 bytes)
 *
 * Disassembly (full context from 0x166b):
 *   166b: mov a, #0x7c       ; A = 0x7C
 *   166d: add a, 0x43        ; A = 0x7C + I_WORK_43
 *   166f: mov DPL, a         ; DPL = A
 *   1671: clr a
 *   1672: addc a, #0x00      ; DPH = 0 + carry
 *   1674: mov DPH, a
 *   1676: ret
 *
 * This computes DPTR = 0x007C + I_WORK_43, with carry overflow to high byte.
 * Used for accessing state slot data at 0x007C base.
 *
 * Returns: Pointer to 0x007C + I_WORK_43 (or 0x017C if overflow)
 */
__xdata uint8_t *helper_166f_ptr(void)
{
    uint8_t low = 0x7C + I_WORK_43;
    uint16_t addr = low;  /* Base is 0x0000 */
    if (low < 0x7C) {
        addr += 0x0100;  /* Handle overflow carry to high byte */
    }
    return (__xdata uint8_t *)addr;
}

/* Stub for compatibility - callers should use helper_166f_ptr() */
void helper_166f(void)
{
    /* DPTR = 0x007C + I_WORK_43 - handled inline by callers */
}

/*
 * transfer_func_16b0 - Write value to SCSI DMA status register
 * Address: 0x16b0-0x16b6 (7 bytes)
 *
 * Disassembly:
 *   16b0: mov dptr, #0xce6e  ; REG_SCSI_DMA_STATUS
 *   16b3: movx @dptr, a      ; Write param
 *   16b4: inc a              ; param + 1
 *   16b5: movx @dptr, a      ; Write param + 1
 *   16b6: ret
 *
 * Writes param to REG_SCSI_DMA_STATUS_L, then writes param+1 to same location.
 */
void transfer_func_16b0(uint8_t param)
{
    REG_SCSI_DMA_STATUS_L = param;
    REG_SCSI_DMA_STATUS_L = param + 1;
}

/* 0x16e9: Helper with param - address calculation stub */
void helper_16e9(uint8_t param) { (void)param; }

/* 0x16eb: Helper with param - address calculation stub */
void helper_16eb(uint8_t param) { (void)param; }

/*
 * FUN_CODE_1b07 - Read from SCSI control array
 * Address: 0x1b07-0x1b13 (13 bytes)
 *
 * Disassembly:
 *   1b07: mov a, #0x71       ; Base offset
 *   1b09: add a, 0x3e        ; A = 0x71 + I_WORK_3E
 *   1b0b: mov DPL, a         ; (continues to helper_1b0b)
 *   1b0d: clr a
 *   1b0e: addc a, #0x01      ; DPH = 0x01 + carry
 *   1b10: mov DPH, a
 *   1b12: movx a, @dptr      ; Read from XDATA
 *   1b13: ret
 *
 * Returns: XDATA[0x0171 + I_WORK_3E]
 * This reads from G_SCSI_CTRL (0x0171) plus I_WORK_3E offset.
 * The G_SCSI_CTRL array stores SCSI command/control parameters indexed by I_WORK_3E.
 */
uint8_t FUN_CODE_1b07(void)
{
    uint8_t low = 0x71 + I_WORK_3E;
    uint16_t addr = 0x0100 + low;  /* Base is 0x0100, add 0x71 + offset */
    if (low < 0x71) {
        addr += 0x0100;  /* Handle overflow carry to high byte */
    }
    return *(__xdata uint8_t *)addr;
}

/*
 * helper_1b0b - Set DPTR and read from XDATA
 * Address: 0x1b0b-0x1b13 (9 bytes)
 *
 * Alternate entry point - A already contains low byte.
 * Computes DPTR = 0x0100 + A (with carry), reads and returns value.
 *
 * Parameters:
 *   low_byte: Pre-computed low byte of address
 *   carry: Carry flag from prior ADD operation
 *
 * Returns: XDATA[0x01xx] where xx = low_byte
 */
uint8_t helper_1b0b(uint8_t low_byte, uint8_t carry)
{
    uint16_t addr = 0x0100 + low_byte;
    if (carry) {
        addr += 0x0100;  /* Carry adds 0x100 to address */
    }
    return *(__xdata uint8_t *)addr;
}

/*
 * xdata_write_load_triple_1564 - Write value and load triple from 0x045E
 * Address: 0x1564-0x156e (11 bytes)
 *
 * Entry point called after caller sets A = value to write.
 * Flow: write A to memory (via 0x0be6), then load 3 bytes from 0x045E.
 *
 * Disassembly:
 *   1564: lcall 0x0be6       ; Write A to memory at (r2:r1) with mode r3
 *   1567: mov dptr, #0x045e  ; Set DPTR to 0x045E
 *   156a: lcall 0x0ddd       ; Load 3 bytes: r3=[045e], r2=[045f], r1=[0460]
 *   156d: mov a, r1          ; Return r1 in A
 *   156e: ret
 *
 * The function writes a value to memory, then reads the state params at
 * 0x045E-0x0460 and returns the third byte (r1 = [0x0460]).
 *
 * Parameters:
 *   value: Value to write (passed in A)
 *   r1_addr: Low byte of write address
 *   r2_addr: High byte of write address
 *   r3_mode: Memory type (1=XDATA, 0=idata, 0xfe=xram)
 *
 * Returns: XDATA[0x0460]
 */
uint8_t xdata_write_load_triple_1564(uint8_t value, uint8_t r1_addr, uint8_t r2_addr, uint8_t r3_mode)
{
    /* Write value to memory based on mode */
    if (r3_mode == 0x01) {
        /* XDATA write */
        __xdata uint8_t *ptr = (__xdata uint8_t *)((uint16_t)r2_addr << 8 | r1_addr);
        *ptr = value;
    } else if (r3_mode == 0x00) {
        /* idata write */
        *(__idata uint8_t *)r1_addr = value;
    }
    /* Mode 0xfe (xram) not commonly used here */

    /* Read and return byte at 0x0460 (third byte of the triple) */
    return XDATA8(0x0460);
}

/* Simpler version when caller just needs the read portion */
uint8_t load_triple_1564_read(void)
{
    return XDATA8(0x0460);
}

/*
 * mem_read_ptr_1bd7 - Set up address and read from memory
 * Address: 0x1bd7-0x1bdb (5 bytes)
 *
 * Called with A containing pre-computed low byte (after some add operation).
 * Sets up r1/r2 and jumps to generic read at 0x0bc8.
 *
 * Disassembly:
 *   1bd7: mov r1, a          ; r1 = A (low byte of address)
 *   1bd8: clr a              ; A = 0
 *   1bd9: addc a, r2         ; A = r2 + carry
 *   1bda: mov r2, a          ; r2 = updated high byte
 *   1bdb: ljmp 0x0bc8        ; Generic memory read
 *
 * The 0x0bc8 function reads from memory at (r2:r1) based on r3 mode:
 *   - r3 == 1: Read from XDATA at (r2:r1)
 *   - r3 != 1, carry clear: Read from idata at r1
 *   - r3 == 0xfe: Read from xram at r1
 *
 * Parameters:
 *   low_byte: Low byte of address (result of prior add)
 *   r2_hi: High byte before carry propagation
 *   r3_mode: Memory type
 *   carry: Carry flag from prior add operation
 *
 * Returns: Value read from computed address
 */
uint8_t mem_read_ptr_1bd7(uint8_t low_byte, uint8_t r2_hi, uint8_t r3_mode, uint8_t carry)
{
    uint16_t addr;
    uint8_t hi = r2_hi;

    /* Propagate carry to high byte */
    if (carry) {
        hi++;
    }

    addr = ((uint16_t)hi << 8) | low_byte;

    /* Read based on mode */
    if (r3_mode == 0x01) {
        /* XDATA read */
        return *(__xdata uint8_t *)addr;
    } else if (r3_mode == 0x00) {
        /* idata read */
        return *(__idata uint8_t *)low_byte;
    } else if (r3_mode == 0xfe) {
        /* xram indirect read */
        return *(__xdata uint8_t *)low_byte;
    }

    /* Default: XDATA read */
    return *(__xdata uint8_t *)addr;
}

/* 0x1b2e: Helper function - stub */
void helper_1b2e(void) {}

/* 0x1b30: Helper function - stub */
void helper_1b30(void) {}

/* 0x1c13: Helper function - stub */
void helper_1c13(void) {}

/* 0x1c9f: Protocol function - stub */
void FUN_CODE_1c9f(void) {}

/*===========================================================================
 * SCSI/Protocol Functions
 *===========================================================================*/

/*
 * helper_0cab - 32-bit subtraction: R4-R7 = R4-R7 - R0-R3
 * Address: 0x0cab-0x0cb8 (14 bytes)
 *
 * Disassembly:
 *   0cab: clr c            ; Clear carry for subtraction
 *   0cac: mov a, r7        ; Start with LSB
 *   0cad: subb a, r3       ; R7 - R3
 *   0cae: mov r7, a
 *   0caf: mov a, r6
 *   0cb0: subb a, r2       ; R6 - R2 - borrow
 *   0cb1: mov r6, a
 *   0cb2: mov a, r5
 *   0cb3: subb a, r1       ; R5 - R1 - borrow
 *   0cb4: mov r5, a
 *   0cb5: mov a, r4
 *   0cb6: subb a, r0       ; R4 - R0 - borrow (MSB)
 *   0cb7: mov r4, a
 *   0cb8: ret
 *
 * This performs 32-bit subtraction where:
 *   R4:R5:R6:R7 = R4:R5:R6:R7 - R0:R1:R2:R3
 * Result returned in R4:R5:R6:R7 (R4=MSB, R7=LSB)
 *
 * In C, this is called with SDCC convention where params are passed differently.
 * The function subtracts minuend (r4-r7) from subtrahend (r0-r3).
 */
uint8_t helper_0cab(uint8_t r0, uint8_t r1, uint8_t r6, uint8_t r7) {
    /* This is a 32-bit subtraction helper used by calling code
     * The actual implementation manipulates R4-R7 registers directly
     * In C, we return a dummy value as the real work is done via registers */
    (void)r0; (void)r1; (void)r6; (void)r7;
    return 0;  /* Actual result is in R4-R7 registers */
}

/*
 * helper_328a - Get USB link status (low 2 bits)
 * Address: 0x328a-0x3290 (7 bytes)
 *
 * Disassembly:
 *   328a: mov dptr, #0x9100  ; REG_USB_LINK_STATUS
 *   328d: movx a, @dptr      ; Read register
 *   328e: anl a, #0x03       ; Mask bits 0-1
 *   3290: ret
 *
 * Returns: REG_USB_LINK_STATUS & 0x03
 * These bits typically indicate USB link speed/state.
 */
uint8_t helper_328a(void)
{
    return REG_USB_LINK_STATUS & 0x03;
}

/*
 * helper_3298 - Get DMA status high bits
 * Address: 0x3298-0x329e (7 bytes)
 *
 * Disassembly:
 *   3298: mov dptr, #0xc8d9  ; REG_DMA_STATUS3
 *   329b: movx a, @dptr      ; Read register
 *   329c: anl a, #0xf8       ; Mask bits 3-7
 *   329e: ret
 *
 * Returns: REG_DMA_STATUS3 & 0xF8
 * These are the upper 5 bits of DMA status register 3.
 */
uint8_t helper_3298(void)
{
    return REG_DMA_STATUS3 & 0xF8;
}

/* 0x3578: Helper with param */
void helper_3578(uint8_t param) { (void)param; }

/* SCSI send CSW - stub */
void scsi_send_csw(uint8_t status, uint8_t param) { (void)status; (void)param; }

/* Interface ready check */
void interface_ready_check(uint8_t p1, uint8_t p2, uint8_t p3) {
    (void)p1; (void)p2; (void)p3;
}

/* Protocol compare 32-bit */
uint8_t protocol_compare_32bit(void) { return 0; }

/* Register poll stub */
void reg_poll(void) {}

/*===========================================================================
 * USB Descriptor Helpers
 *===========================================================================*/

/*
 * usb_descriptor_helper_a637 - Initialize USB descriptor state
 * Address: 0xa637-0xa643 (13 bytes)
 *
 * Disassembly:
 *   a637: mov a, #0x01
 *   a639: mov dptr, #0x0ad7   ; G_USB_DESC_STATE
 *   a63c: movx @dptr, a       ; Write 1
 *   a63d: mov dptr, #0x0ade   ; G_USB_DESC_INDEX
 *   a640: clr a
 *   a641: movx @dptr, a       ; Write 0
 *   a642: inc dptr            ; 0x0adf
 *   a643: ret
 *
 * Sets G_USB_DESC_STATE = 1, G_USB_DESC_INDEX = 0
 */
void usb_descriptor_helper_a637(void)
{
    XDATA_REG8(0x0ad7) = 0x01;  /* G_USB_DESC_STATE */
    XDATA_REG8(0x0ade) = 0x00;  /* G_USB_DESC_INDEX */
}

/* usb_descriptor_helper_a644 - moved to queue_handlers.c */

/*
 * usb_descriptor_helper_a648 - Calculate descriptor buffer address (entry at add)
 * Address: 0xa648-0xa650 (9 bytes)
 *
 * Alternate entry point into a644.
 * DPTR = 0x9E00 + R7, with R6 as high byte adjustment.
 */
/* usb_descriptor_helper_a648 - moved to queue_handlers.c */

/*
 * usb_descriptor_helper_a651 - Write to descriptor buffer (base 0x59)
 * Address: 0xa651-0xa65f (15 bytes)
 *
 * Disassembly:
 *   a651: subb a, #0x59       ; A = A - 0x59
 *   a653: mov r4, a           ; Save high adjustment
 *   a654: clr a
 *   a655: add a, r5           ; A = 0 + R5 (param)
 *   a656: mov 0x82, a         ; DPL = R5
 *   a658: mov a, #0x9e        ; Base high = 0x9E
 *   a65a: addc a, r4          ; DPH = 0x9E + R4 + carry
 *   a65b: mov 0x83, a
 *   a65d: mov a, r7           ; Value to write
 *   a65e: movx @dptr, a       ; Write R7 to buffer
 *   a65f: ret
 *
 * Writes R7 to descriptor buffer at 0x9E00 + R5 + adjustment.
 */
void usb_descriptor_helper_a651(uint8_t p1, uint8_t p2, uint8_t p3)
{
    /* Writes value to USB descriptor buffer */
    (void)p1; (void)p2; (void)p3;
}

/*
 * usb_descriptor_helper_a655 - Calculate buffer address and write (entry at add)
 * Address: 0xa655-0xa65f (11 bytes)
 *
 * Alternate entry point into a651.
 */
/* usb_descriptor_helper_a655 - moved to queue_handlers.c */

/* USB descriptor parsing - stub */
void usb_parse_descriptor(uint8_t p1, uint8_t p2) { (void)p1; (void)p2; }

/* USB transfer status check - stub */
uint8_t usb_get_xfer_status(void) { return 0; }

/* USB event handler - stub */
uint8_t usb_event_handler(void) { return 0; }

/*
 * parse_descriptor - Parse USB descriptor (0x04da)
 * Address: 0x04da
 *
 * This is a wrapper that calls the descriptor parser with one parameter.
 */
void parse_descriptor(uint8_t param) { (void)param; }

/*
 * usb_state_setup_4c98 - USB state setup
 * Address: 0x4c98
 *
 * Sets up USB state for transfer operations.
 */
void usb_state_setup_4c98(void) {}

/*
 * usb_helper_51ef - USB helper (abort path)
 * Address: 0x51ef
 *
 * Called in abort/error handling path.
 */
void usb_helper_51ef(void) {}

/*
 * usb_helper_5112 - USB helper
 * Address: 0x5112
 *
 * Called after setting transfer active flag in abort path.
 */
void usb_helper_5112(void) {}

/* usb_set_transfer_active_flag - IMPLEMENTED in usb.c */

/* nvme_read_status - IMPLEMENTED in nvme.c */

/* USB transfer parameter reads - stubs */
uint8_t usb_read_transfer_params_hi(void) { return 0; }
uint8_t usb_read_transfer_params_lo(void) { return 0; }

/*===========================================================================
 * Handler Functions
 *===========================================================================*/

/*
 * Note: handler_0327 and handler_039a are NOT standalone functions.
 * They are entries in a dispatch table at 0x0300+ that loads DPTR
 * with a target address and jumps to the common dispatcher.
 * The actual handlers are the addresses loaded into DPTR.
 */

/* 0x0327: Dispatch entry - loads DPTR=0xB1CB, jumps to 0x0300 */
void handler_0327_usb_power_init(void) {}

/* 0x039a: Dispatch entry - loads DPTR=0xD810, jumps to 0x0300 */
void handler_039a_buffer_dispatch(void) {}

/*
 * helper_9608 - Read-modify-write: clear bit 0, set bit 0
 * Address: 0x9608-0x960e (7 bytes)
 *
 * Entry point into cmd_start_trigger. DPTR must be set by caller.
 * Reads @DPTR, clears bit 0 (& 0xFE), sets bit 0 (| 0x01), writes back.
 */
static void helper_9608(void)
{
    /* DPTR is already set by caller - read, modify, write */
    /* In C we can't directly access DPTR, so this is done inline */
}

/*
 * helper_9627 - Write accumulated value to DPTR register
 * Address: 0x9627-0x962d (7 bytes)
 *
 * Writes A to @DPTR. In calling context, A has been modified.
 */
static void helper_9627(uint8_t val)
{
    (void)val;
    /* Value written by direct call in original - DPTR already set */
}

/*
 * helper_955e - Write value to CC89 and increment
 * Address: 0x955e-0x9565 (8 bytes)
 *
 * Writes A to @DPTR, increments DPTR, writes A again.
 */
static void helper_955e(uint8_t val)
{
    /* DPTR=CC89, writes val, inc to CC8A, writes val again */
    REG_XFER_DMA_CMD = val;
    REG_XFER_DMA_ADDR_LO = val;
}

/* Forward declaration */
extern void cmd_write_cc89_02(void);

/*
 * handler_d676 - Initialize PCIe/DMA with error halt
 * Address: 0xd676-0xd701 (140 bytes)
 *
 * This function initializes DMA registers with polling and error handling.
 * IMPORTANT: This function ends with an infinite loop (hang on error).
 *
 * Disassembly:
 *   d676: mov r3, #0xff           ; R3:R2:R1 = 0xFF234B (debug string addr)
 *   d678: mov r2, #0x23
 *   d67a: mov r1, #0x4b
 *   d67c: lcall 0x538d            ; uart_puts
 *   d67f: mov dptr, #0xcc32
 *   d682: lcall 0x9608            ; Set bit 0 of CC32
 *   d685: mov dptr, #0xe7fa
 *   d688: mov a, #0x0f
 *   d68a: movx @dptr, a           ; Write 0x0F to E7FA
 *   d68b: mov dptr, #0xcc88
 *   d68e-d693: Read CC88, clear bits 0-2, set bit 2, write back
 *   d696-d699: Inc to CC89, write 0x31
 *   d69c-d6a0: Poll CC89 bit 1
 *   d6a3: lcall 0x964f            ; cmd_write_cc89_02
 *   d6a6-d6b0: Setup CC31/CC32
 *   d6b1-d6b7: uart_puts with error message
 *   d6ba: sjmp 0xd6ba             ; **INFINITE LOOP - HANG**
 *   d6bc-d701: Error code determination based on R7, R5, R3
 */
void handler_d676(void)
{
    uint8_t val;

    /* Print debug message (string at 0xFF234B) */
    /* uart_puts(0xFF234B); */

    /* Set bit 0 of CC32 */
    val = REG_CPU_EXEC_STATUS;
    val = (val & 0xFE) | 0x01;
    REG_CPU_EXEC_STATUS = val;

    /* Write 0x0F to E7FA */
    REG_PHY_LINK_TRIGGER = 0x0F;

    /* CC88: clear bits 0-2, set bit 2 */
    val = REG_XFER_DMA_CTRL;
    val = (val & 0xF8) | 0x04;
    REG_XFER_DMA_CTRL = val;

    /* Write 0x31 to CC89 - start DMA mode 1 */
    REG_XFER_DMA_CMD = XFER_DMA_CMD_START | XFER_DMA_CMD_MODE;

    /* Poll CC89 until transfer complete */
    while (!(REG_XFER_DMA_CMD & XFER_DMA_CMD_DONE)) {
        /* Spin */
    }

    /* Write 0x02 to CC89 */
    cmd_write_cc89_02();

    /* Set bit 0 of CC31 */
    val = REG_CPU_EXEC_CTRL;
    val = (val & 0xFE) | 0x01;
    REG_CPU_EXEC_CTRL = val;

    /* Inc to CC32, clear bit 0 */
    val = REG_CPU_EXEC_STATUS;
    val &= 0xFE;
    REG_CPU_EXEC_STATUS = val;

    /* Print error message (string at 0xFF235C) */
    /* uart_puts(0xFF235C); */

    /* ERROR: Infinite loop - hang the system */
    /* This is intentional - the function never returns */
    while (1) {
        /* Hang forever */
    }
}

/* Forward declarations for handler_e3d8 */
extern void helper_e3b7(uint8_t param);
extern void helper_3578(uint8_t param);
extern void dispatch_039a(void);  /* 0xD810 usb_buffer_handler */

/*
 * handler_e3d8 - Event handler with conditional processing
 * Address: 0xe3d8-0xe3f8 (33 bytes)
 *
 * Disassembly:
 *   e3d8: mov dptr, #0x0b41  ; G_USB_STATE_0B41
 *   e3db: movx a, @dptr      ; Read flags
 *   e3dc: jz 0xe3e3          ; Skip if zero
 *   e3de: mov r7, #0x03      ; Param = 3
 *   e3e0: lcall 0xe3b7       ; Call helper_e3b7
 *   e3e3: mov dptr, #0x0aee  ; G_STATE_CHECK_0AEE
 *   e3e6: movx a, @dptr      ; Read state
 *   e3e7: mov r7, a          ; R7 = state
 *   e3e8: lcall 0x3578       ; helper_3578
 *   e3eb: lcall 0xd810       ; dispatch_039a (usb_buffer_handler)
 *   e3ee: clr a
 *   e3ef: mov dptr, #0x07e8  ; G_SYS_FLAGS_07E8
 *   e3f2: movx @dptr, a      ; Clear flags
 *   e3f3: mov dptr, #0x0b2f  ; G_INTERFACE_READY_0B2F
 *   e3f6: inc a              ; A = 1
 *   e3f7: movx @dptr, a      ; Set ready flag
 *   e3f8: ret
 */
void handler_e3d8(void)
{
    uint8_t flags;

    /* Check USB state flags */
    flags = G_USB_STATE_0B41;
    if (flags != 0) {
        /* Call helper_e3b7 with param = 3 */
        helper_e3b7(3);
    }

    /* Read state and call helper_3578 */
    flags = G_STATE_CHECK_0AEE;
    helper_3578(flags);

    /* Call USB buffer handler (dispatch_039a) */
    dispatch_039a();

    /* Clear system flags and set interface ready */
    G_SYS_FLAGS_07E8 = 0;
    G_INTERFACE_READY_0B2F = 1;
}

/*
 * helper_dd42 - State update based on param and 0x0af1 flag
 * Address: 0xdd42-0xdd77 (54 bytes)
 *
 * Disassembly:
 *   dd42: mov dptr, #0x0af1  ; G_STATE_FLAG_0AF1
 *   dd45: movx a, @dptr      ; Read flag
 *   dd46: jnb 0xe0.5, 0xdd72 ; If bit 5 clear, goto default
 *   dd49: mov a, r7          ; Get param
 *   dd4a: jz 0xdd72          ; If param == 0, goto default
 *   dd4c: cjne a, #0x02, dd51; If param != 2, check next
 *   dd4f: sjmp 0xdd72        ; Default case
 *   dd51: mov a, r7
 *   dd52: cjne a, #0x04, dd5c; If param != 4, check next
 *   dd55: mov dptr, #0xe7e3  ; Write 0x30
 *   dd58: mov a, #0x30
 *   dd5a: movx @dptr, a
 *   dd5b: ret
 *   dd5c: mov a, r7
 *   dd5d: cjne a, #0x01, dd67; If param != 1, check next
 *   dd60: mov dptr, #0xe7e3  ; Write 0xcc
 *   dd63: mov a, #0xcc
 *   dd65: movx @dptr, a
 *   dd66: ret
 *   dd67: mov a, r7
 *   dd68: cjne a, #0xff, dd77; If param != 0xff, return
 *   dd6b: mov dptr, #0xe7e3  ; Write 0xfc
 *   dd6e: mov a, #0xfc
 *   dd70: movx @dptr, a
 *   dd71: ret
 *   dd72: mov dptr, #0xe7e3  ; Default: write 0
 *   dd75: clr a
 *   dd76: movx @dptr, a
 *   dd77: ret
 *
 * Based on param value, writes specific values to REG 0xe7e3
 * if bit 5 of G_STATE_FLAG_0AF1 is set.
 */
void helper_dd42(uint8_t param)
{
    uint8_t flag = G_STATE_FLAG_0AF1;

    /* If bit 5 is clear, write 0 to PHY_LINK_CTRL */
    if (!(flag & 0x20)) {
        REG_PHY_LINK_CTRL = 0;
        return;
    }

    /* If param == 0 or param == 2, write 0 */
    if (param == 0 || param == 2) {
        REG_PHY_LINK_CTRL = 0;
        return;
    }

    /* Handle specific param values */
    if (param == 4) {
        REG_PHY_LINK_CTRL = 0x30;
        return;
    }

    if (param == 1) {
        REG_PHY_LINK_CTRL = 0xcc;
        return;
    }

    if (param == 0xff) {
        REG_PHY_LINK_CTRL = 0xfc;
        return;
    }

    /* Default: do nothing (return without writing) */
}

/* Forward declaration for helper_e6d2 dependencies */
extern void helper_e396(void);
extern uint8_t helper_0dc5(uint32_t val);
extern void helper_d17a(void);
extern void handler_e478(void);  /* was: dispatch_0638, Bank1:0xE478 */

/*
 * helper_e6d2 - Protocol setup with 32-bit parameter
 * Address: 0xe6d2-0xe6e6 (21 bytes)
 *
 * Disassembly:
 *   e6d2: lcall 0xe396       ; Call helper
 *   e6d5: mov r7, #0x00      ; 32-bit value = 0x00010080
 *   e6d7: mov r6, #0x80
 *   e6d9: mov r5, #0x01
 *   e6db: mov r4, #0x00
 *   e6dd: mov dptr, #0x0b1d
 *   e6e0: lcall 0x0dc5       ; Store 32-bit value
 *   e6e3: lcall 0xd17a       ; Finalize
 *   e6e6: ret
 *
 * Calls helper_e396, then stores 0x00010080 to 0x0b1d, then calls d17a.
 */
uint8_t helper_e6d2(void)
{
    helper_e396();

    /* Store 32-bit value 0x00010080 to 0x0b1d using helper_0dc5 */
    /* The DPTR is set to 0x0b1d before calling 0x0dc5 */
    /* r7:r6:r5:r4 = 0x00:0x80:0x01:0x00 = 0x00010080 (little endian read) */
    G_DMA_WORK_0B1D = 0x00;  /* r4 */
    G_DMA_WORK_0B1E = 0x01;  /* r5 */
    G_DMA_WORK_0B1F = 0x80;  /* r6 */
    G_DMA_WORK_0B20 = 0x00;  /* r7 */

    helper_d17a();

    return 0;  /* Result in r7 */
}

/*
 * handler_e529 - Store param and process transfer
 * Address: 0xe529-0xe544 (28 bytes)
 *
 * Disassembly:
 *   e529: mov dptr, #0x0aa3  ; G_STATE_RESULT_0AA3
 *   e52c: mov a, r7          ; Get param
 *   e52d: movx @dptr, a      ; Store it
 *   e52e: clr a
 *   e52f: mov r7, a          ; R7 = 0
 *   e530: lcall 0xdd42       ; helper_dd42
 *   e533: lcall 0xe6d2       ; helper_e6d2
 *   e536: mov a, r7          ; Check result
 *   e537: jz 0xe544          ; Return if zero
 *   e539: mov dptr, #0x0aa3  ; G_STATE_RESULT_0AA3
 *   e53c: movx a, @dptr      ; Read param
 *   e53d: mov dptr, #0x7000  ; Log buffer base
 *   e540: movx @dptr, a      ; Write to log
 *   e541: lcall 0xe478       ; dispatch_0638 -> handler_e478
 *   e544: ret
 *
 * Stores param, calls helper functions, and if result is non-zero,
 * writes saved param to 0x7000 and dispatches to Bank1 handler.
 */
void handler_e529(uint8_t param)
{
    uint8_t result;

    /* Store param to G_STATE_RESULT_0AA3 */
    G_STATE_RESULT_0AA3 = param;

    /* Call helper_dd42 with param = 0 */
    helper_dd42(0);

    /* Call helper_e6d2 and get result */
    result = helper_e6d2();

    /* If result non-zero, process further */
    if (result != 0) {
        /* Read back saved param and write to flash buffer */
        G_FLASH_BUF_BASE = G_STATE_RESULT_0AA3;
        /* Dispatch to Bank1 handler_e478 */
        handler_e478();  /* was: dispatch_0638 */
    }
}

/*
 * handler_e90b - Write to PCIe control register
 * Address: 0xe90b-0xe911 (7 bytes)
 *
 * Disassembly:
 *   e90b: mov dptr, #0xcc81  ; REG_CPU_INT_CTRL
 *   e90e: mov a, #0x04       ; Value = 4
 *   e910: movx @dptr, a      ; Write
 *   e911: ljmp 0xbe8b        ; Tail call to FUN_CODE_be8b
 *
 * Triggers CPU interrupt then tail-calls FUN_CODE_be8b.
 */
extern void FUN_CODE_be8b(void);
void handler_e90b(void)
{
    REG_CPU_INT_CTRL = CPU_INT_CTRL_TRIGGER;
    FUN_CODE_be8b();
}

/*===========================================================================
 * NVMe Utility Functions
 *===========================================================================*/

void nvme_util_advance_queue(void) {}
void nvme_util_check_command_ready(void) {}
void nvme_util_clear_completion(void) {}

/*===========================================================================
 * PCIe/System Functions
 *===========================================================================*/

/*
 * helper_e3b7 - Timer setup and conditional power/link control
 * Address: 0xe3b7-0xe3d7 (33 bytes)
 *
 * Disassembly:
 *   e3b7: mov dptr, #0xcc17  ; REG_TIMER1_CSR
 *   e3ba: lcall 0xbd0d       ; Write 0x04 then 0x02 to @dptr
 *   e3bd: mov a, r7          ; Get param
 *   e3be: jnb 0xe0.0, e3c8   ; Skip if bit 0 clear
 *   e3c1: mov dptr, #0x92c4  ; REG_POWER_CTRL_92C4
 *   e3c4: movx a, @dptr
 *   e3c5: anl a, #0xfe       ; Clear bit 0
 *   e3c7: movx @dptr, a
 *   e3c8: mov a, r7
 *   e3c9: jnb 0xe0.1, e3d7   ; Skip if bit 1 clear
 *   e3cc: mov dptr, #0xb480  ; REG_TUNNEL_LINK_CTRL
 *   e3cf: lcall 0xbceb       ; Set bit 0 in @dptr
 *   e3d2: clr a
 *   e3d3: mov r7, a          ; r7 = 0
 *   e3d4: lcall 0xc2e6       ; Process log entries with param=0
 *   e3d7: ret
 *
 * Checks param bits and modifies registers accordingly:
 * - Always: Write 04, 02 to REG_TIMER1_CSR (start timer)
 * - Bit 0 set: Clear bit 0 of REG_POWER_CTRL_92C4
 * - Bit 1 set: Set bit 0 of REG_TUNNEL_LINK_CTRL, call log processor
 */
extern void process_log_entries(uint8_t param);  /* 0xc2e6 */

void helper_e3b7(uint8_t param)
{
    /* Write 0x04 then 0x02 to REG_TIMER1_CSR (start timer) */
    REG_TIMER1_CSR = 0x04;
    REG_TIMER1_CSR = 0x02;

    /* If bit 0 set: clear bit 0 of REG_POWER_CTRL_92C4 */
    if (param & 0x01) {
        REG_POWER_CTRL_92C4 = REG_POWER_CTRL_92C4 & 0xFE;
    }

    /* If bit 1 set: set bit 0 of REG_TUNNEL_LINK_CTRL and call log processor */
    if (param & 0x02) {
        REG_TUNNEL_LINK_CTRL = (REG_TUNNEL_LINK_CTRL & 0xFE) | 0x01;
        process_log_entries(0);
    }
}

/*
 * helper_e396 - Protocol initialization setup
 * Address: 0xe396-0xe3b6 (33 bytes)
 *
 * Disassembly:
 *   e396: lcall 0xb8b9       ; Call helper
 *   e399: lcall 0xb833       ; Set up base
 *   e39c: mov a, #0x03
 *   e39e: movx @dptr, a      ; Write 0x03
 *   e39f: clr a
 *   e3a0: mov r5, a          ; R5 = 0
 *   e3a1: mov r7, #0x9f      ; R7 = 0x9F
 *   e3a3: lcall 0xbe02       ; Call delay/wait
 *   e3a6: mov dptr, #0x0b21
 *   e3a9: mov a, #0x80
 *   e3ab: movx @dptr, a      ; [0x0b21] = 0x80
 *   e3ac: mov dptr, #0x0b24
 *   e3af: mov a, #0xd8
 *   e3b1: movx @dptr, a      ; [0x0b24] = 0xd8
 *   e3b2: inc dptr           ; dptr = 0x0b25
 *   e3b3: mov a, #0x20
 *   e3b5: movx @dptr, a      ; [0x0b25] = 0x20
 *   e3b6: ret
 */
void helper_e396(void)
{
    /* Complex initialization - calls multiple sub-helpers */
    /* For now, just set up the values at the known addresses */
    G_DMA_WORK_0B21 = 0x80;
    G_DMA_WORK_0B24 = 0xd8;
    G_DMA_WORK_0B25 = 0x20;
}

/*
 * helper_d17a - Protocol finalization
 * Address: 0xd17a-0xd196 (29 bytes, first return path)
 *
 * Calls multiple sub-helpers and returns a status value in r7.
 * Returns 0 on success, non-zero otherwise.
 */
void helper_d17a(void)
{
    /* Complex finalization - calls multiple sub-helpers */
    /* Stub implementation */
}

void pcie_bank1_helper_e902(void) {}
void startup_init(void) {}
void sys_event_dispatch_05e8(void) {}
void sys_init_helper_bbc7(void) {}
void sys_timer_handler_e957(void) {}

/*
 * pcie_lane_config_helper - PCIe lane configuration state machine
 * Address: 0xc089-0xc104 (124 bytes)
 *
 * Complex lane configuration state machine that iterates up to 4 times,
 * configuring link state registers (0xB434) and calling phy_link_training (0xd702).
 *
 * Algorithm:
 *   1. Store param to G_FLASH_ERROR_1 (0x0AA9)
 *   2. Set G_STATE_COUNTER_0AAC = 1
 *   3. Read B434 low nibble -> G_STATE_HELPER_0AAB
 *   4. Set G_FLASH_RESET_0AAA = 0
 *   5. Loop up to 4 times:
 *      - If param < 0x0F, check if G_STATE_HELPER_0AAB == param
 *      - Otherwise check if G_STATE_HELPER_0AAB == 0x0F
 *      - Merge state values, write to B434, call d702, delay 200ms
 *   6. Return loop count - 4
 *
 * This is CRITICAL for eGPU - it trains the PCIe link.
 */
void pcie_lane_config_helper(uint8_t param)
{
    uint8_t lane_state, counter, temp;

    G_FLASH_ERROR_1 = param;
    G_STATE_COUNTER_0AAC = 1;

    /* Read current lane state from B434 low nibble */
    lane_state = REG_PCIE_LINK_STATE & 0x0F;
    G_STATE_HELPER_0AAB = lane_state;
    G_FLASH_RESET_0AAA = 0;

    /* Loop up to 4 times for link training */
    for (counter = 0; counter < 4; counter++) {
        temp = G_FLASH_ERROR_1;

        if (temp < 0x0F) {
            /* Check if we've reached target lane config */
            if (G_STATE_HELPER_0AAB == temp) {
                return;  /* Success */
            }
            /* Merge lane state with counter */
            temp = (temp | (G_STATE_COUNTER_0AAC ^ 0x0F)) & G_STATE_HELPER_0AAB;
        } else {
            /* Full lane mode - check for 0x0F */
            if (G_STATE_HELPER_0AAB == 0x0F) {
                return;  /* Success */
            }
            /* Set all lanes active */
            temp = G_STATE_COUNTER_0AAC | G_STATE_HELPER_0AAB;
        }

        G_STATE_HELPER_0AAB = temp;

        /* Update B434 with new lane state */
        lane_state = REG_PCIE_LINK_STATE;
        REG_PCIE_LINK_STATE = temp | (lane_state & 0xF0);

        /* Call PHY link training (0xD702) */
        phy_link_training();

        /* Wait ~200ms for link to train (0xE80A with r4=0, r5=199, r7=2) */
        timer_wait(0x00, 0xC7, 0x02);

        /* Shift counter for next iteration */
        G_STATE_COUNTER_0AAC = G_STATE_COUNTER_0AAC * 2;
        G_FLASH_RESET_0AAA++;
    }
}

/*===========================================================================
 * Main Event Handler Wrappers
 *===========================================================================*/

/* Note: The following handler functions are implemented in main.c:
 * - event_state_handler (line 842) - calls dispatch_0494
 * - error_state_config (line 907) - calls dispatch_0606
 * - phy_register_config (line 970) - calls dispatch_0589
 * - flash_command_handler (line 1025) - calls dispatch_0525
 */

/*===========================================================================
 * UART/Log Buffer Functions
 *===========================================================================*/

/*
 * uart_read_byte_dace - Read byte from log buffer
 * Address: 0xdace-0xdad8 (11 bytes)
 *
 * Disassembly:
 *   dace: add a, 0x21        ; A = A + I_LOG_INDEX
 *   dad0: mov 0x82, a        ; DPL = result
 *   dad2: clr a
 *   dad3: addc a, #0x70      ; DPH = 0x70 + carry
 *   dad5: mov 0x83, a        ; (gives DPTR = 0x7000 + offset)
 *   dad7: movx a, @dptr      ; Read byte
 *   dad8: ret
 *
 * This reads from the log buffer at 0x7000 + I_LOG_INDEX + input_offset.
 * The input A value is added to I_LOG_INDEX to form the offset.
 *
 * Note: Entry expects A to contain offset. In C, we pass as param.
 * Returns: XDATA[0x7000 + I_LOG_INDEX + offset]
 */
uint8_t uart_read_byte_dace(void)
{
    /* Read from log buffer at base 0x7000 + I_LOG_INDEX */
    uint16_t addr = 0x7000 + I_LOG_INDEX;
    return XDATA_REG8(addr);
}

/*
 * uart_write_byte_daeb - Calculate log buffer write address
 * Address: 0xdaeb-0xdaf4 (10 bytes)
 *
 * Disassembly:
 *   daeb: mov a, #0xfc       ; Base offset
 *   daed: add a, 0x21        ; A = 0xFC + I_LOG_INDEX
 *   daef: mov 0x82, a        ; DPL = result
 *   daf1: clr a
 *   daf2: addc a, #0x09      ; DPH = 0x09 + carry
 *   daf4: ret
 *
 * This calculates address 0x09FC + I_LOG_INDEX for writing.
 * Returns: DPTR pointing to 0x09FC + I_LOG_INDEX (in DPH:DPL = 0x83:0x82)
 *
 * Note: This is an address calculation that returns DPTR, used by caller.
 * In C, we just return the high byte; caller uses DPTR afterwards.
 */
uint8_t uart_write_byte_daeb(uint8_t b)
{
    /* This calculates DPTR = 0x09FC + I_LOG_INDEX
     * Original returns with DPTR set up for caller to use
     * The param b is the byte to write (in R7) */
    (void)b;
    /* In original, DPH is returned in A */
    uint8_t low = 0xFC + I_LOG_INDEX;
    uint8_t high = 0x09;
    if (low < 0xFC) high++;  /* Handle carry */
    return high;
}

/*
 * uart_write_daff - Calculate alternate log buffer address
 * Address: 0xdaff-0xdb08 (10 bytes)
 *
 * Disassembly:
 *   daff: mov a, #0x1c       ; Base offset
 *   db01: add a, 0x21        ; A = 0x1C + I_LOG_INDEX
 *   db03: mov 0x82, a        ; DPL = result
 *   db05: clr a
 *   db06: addc a, #0x0a      ; DPH = 0x0A + carry
 *   db08: ret
 *
 * This calculates address 0x0A1C + I_LOG_INDEX.
 * Returns: DPH (0x0A possibly + carry) in A
 */
uint8_t uart_write_daff(void)
{
    /* Calculate DPTR = 0x0A1C + I_LOG_INDEX */
    uint8_t low = 0x1C + I_LOG_INDEX;
    uint8_t high = 0x0A;
    if (low < 0x1C) high++;  /* Handle carry */
    return high;
}

/*===========================================================================
 * Code Functions (FUN_CODE_xxxx)
 *===========================================================================*/

/*
 * Note: FUN_CODE_050c and FUN_CODE_0511 are dispatch table entries,
 * not standalone functions. They load DPTR with target addresses.
 */
void FUN_CODE_050c(void) {}
void FUN_CODE_0511(uint8_t p1, uint8_t p2, uint8_t p3) { (void)p1; (void)p2; (void)p3; }

/*
 * Helper functions used by FUN_CODE_11a2
 * These are inline address calculation helpers
 */

/* 0x15b7: Set DPTR = 0x0171 + I_WORK_43 (slot table offset 0x71) */
static __xdata uint8_t *get_slot_addr_71(void)
{
    return (__xdata uint8_t *)(0x0171 + I_WORK_43);
}

/* 0x15d4: Set DPTR low byte, high from carry (address in 0x00xx) */
static __xdata uint8_t *get_addr_from_slot(uint8_t base)
{
    return (__xdata uint8_t *)((uint16_t)base + I_WORK_43);
}

/* 0x159f: Store A to @dptr, set DPTR = 0x014E + slot */
static __xdata uint8_t *get_slot_addr_4e(void)
{
    return (__xdata uint8_t *)(0x014E + I_WORK_43);
}

/* 0x166a: Store A to @dptr, set DPTR = 0x007C + slot */
static __xdata uint8_t *get_slot_addr_7c(void)
{
    return (__xdata uint8_t *)(0x007C + I_WORK_43);
}

/* 0x1755: Set DPTR from A (address in 0x00xx) */
static __xdata uint8_t *get_addr_low(uint8_t addr)
{
    return (__xdata uint8_t *)addr;
}

/* 0x1646: Read from G_EP_INDEX * 0x14 + 0x054E */
static uint8_t get_ep_config_4e(void)
{
    uint16_t addr = (uint16_t)G_SYS_STATUS_SECONDARY * 0x14 + 0x054E;
    return *(__xdata uint8_t *)addr;
}

/* 0x523c: DMA setup transfer helper */
extern void dma_setup_transfer(uint8_t r3, uint8_t r5, uint8_t r7);

/*
 * FUN_CODE_11a2 - SCSI/DMA transfer state machine
 * Address: 0x11a2-0x152x (~500 bytes)
 *
 * Processes SCSI command state and manages DMA transfers.
 * Input: param in R7 (0 = initialize, non-0 = active transfer check)
 * Output: result in R7 (0 = not ready, 1 = ready/success)
 *
 * Uses: I_WORK_3F (transfer count), I_WORK_40-46 (work vars)
 * Reads: CE51/CE55/CE60/CE6E (SCSI DMA registers)
 * Writes: G_0470-0476 (command state), G_053A (NVMe param)
 */
uint8_t FUN_CODE_11a2(uint8_t param)
{
    uint8_t val;
    __xdata uint8_t *ptr;

    /* Copy slot index from I_QUEUE_IDX to I_WORK_43 */
    I_WORK_43 = I_QUEUE_IDX;

    if (param != 0) {
        /* Active transfer check path (param != 0) */
        /* Read SCSI tag index into I_WORK_3F */
        I_WORK_3F = REG_SCSI_TAG_IDX;

        /* Check slot table at 0x0171 + slot */
        ptr = get_slot_addr_71();
        val = *ptr;

        if (val == 0xFF) {
            /* Tag is complete - copy tag value to slot tables */
            uint8_t tag_val = REG_SCSI_TAG_VALUE;

            /* Store to 0x009F + slot */
            ptr = get_addr_from_slot(0x9F);
            *ptr = tag_val;

            /* Store to 0x0171 + slot */
            ptr = get_slot_addr_71();
            *ptr = tag_val;

            /* Clear NVMe parameter */
            G_NVME_PARAM_053A = 0;
        }
        /* Fall through to check I_WORK_3F value */
    } else {
        /* Transfer initialization path (param == 0) */
        val = G_SCSI_CMD_PARAM_0470;

        if (val & 0x01) {
            /* Bit 0 set - use G_DMA_LOAD_PARAM2 directly */
            I_WORK_3F = G_DMA_LOAD_PARAM2;
        } else {
            /* Calculate from endpoint config table */
            uint8_t ep_idx = G_SYS_STATUS_SECONDARY;
            uint16_t addr = (uint16_t)ep_idx * 0x14 + 0x054B;
            uint8_t base_count = *(__xdata uint8_t *)addr;

            /* Load transfer params and calculate count */
            /* dma_load_transfer_params does: R7 = 16-bit div result */
            /* Simplified: just use the base count */
            I_WORK_3F = base_count;

            /* Call again and check if remainder is non-zero */
            /* If so, increment count */
            /* (Simplified - actual code does complex division) */
        }

        /* Check bit 3 for division path */
        val = G_SCSI_CMD_PARAM_0470;
        if (val & 0x08) {
            /* Get multiplier from EP config */
            uint8_t mult = get_ep_config_4e();

            if (mult != 0) {
                /* G_XFER_DIV_0476 = I_WORK_3F / mult */
                G_XFER_DIV_0476 = I_WORK_3F / mult;

                /* Check remainder, if non-zero increment */
                if ((I_WORK_3F % mult) != 0) {
                    G_XFER_DIV_0476++;
                }
            } else {
                G_XFER_DIV_0476 = I_WORK_3F;
            }

            /* Check USB status for slot table update */
            val = REG_USB_STATUS;
            if (val & USB_STATUS_ACTIVE) {
                ptr = get_slot_addr_71();
                val = *ptr;
                if (val == 0xFF) {
                    /* Update slot tables from G_XFER_DIV_0476 */
                    uint8_t div_result = G_XFER_DIV_0476;
                    ptr = get_addr_from_slot(0x9F);
                    *ptr = div_result;
                    ptr = get_slot_addr_71();
                    *ptr = div_result;
                    G_NVME_PARAM_053A = 0;
                }

                /* Update C414 bit 7 based on comparison */
                ptr = get_addr_from_slot(0x9F);
                val = *ptr;
                /* Swap nibbles and subtract 1, compare with R7 (slot high) */
                uint8_t swapped = ((I_WORK_43 >> 4) | (I_WORK_43 << 4)) - 1;
                if (val == swapped) {
                    /* Set bit 7 of C414 */
                    REG_NVME_DATA_CTRL = (REG_NVME_DATA_CTRL & 0x7F) | 0x80;
                } else {
                    /* Clear bit 7 of C414 */
                    REG_NVME_DATA_CTRL = REG_NVME_DATA_CTRL & 0x7F;
                }
            }
        }
    }

    /* Check transfer count range */
    /* if I_WORK_3F >= 0x81, return 0 */
    if (I_WORK_3F == 0 || I_WORK_3F > 0x80) {
        /* Call dma_setup_transfer(0, 0x24, 0x05) and return 0 */
        dma_setup_transfer(0, 0x24, 0x05);
        return 0;
    }

    /* Check bit 2 of G_SCSI_CMD_PARAM_0470 */
    val = G_SCSI_CMD_PARAM_0470;
    if (val & 0x04) {
        /* Simple path - store helpers */
        G_STATE_HELPER_41 = 0;
        G_STATE_HELPER_42 = I_WORK_3F & 0x1F;
        return 1;
    }

    /* Check if I_WORK_3F == 1 (single transfer) */
    if (I_WORK_3F == 1) {
        /* Read CE60 into I_WORK_40 */
        I_WORK_40 = REG_XFER_STATUS_CE60;

        /* Check range */
        if (I_WORK_40 >= 0x40) {
            return 0;
        }

        /* Write to SCSI DMA status register */
        REG_SCSI_DMA_STATUS_L = I_WORK_40;
        G_STATE_HELPER_41 = I_WORK_40;
        G_STATE_HELPER_42 = I_WORK_40 + I_WORK_3F;

        /* Call helpers with calculated addresses */
        ptr = get_addr_low(0x59 + I_WORK_43);
        /* FUN_CODE_1755 would write here */

        ptr = get_slot_addr_4e();
        *ptr = I_WORK_40;

        ptr = get_slot_addr_7c();
        *ptr = I_WORK_40;

        /* Write 1 to slot addr 71 */
        ptr = get_slot_addr_71();
        *ptr = 1;

        return 1;
    }

    /* Multi-transfer path - read tag status */
    ptr = get_addr_from_slot(0x9F);
    I_WORK_42 = *ptr;
    I_WORK_44 = get_ep_config_4e();

    /* Complex state machine based on I_WORK_42 and I_WORK_44 */
    /* Simplified: just return success for valid transfers */
    if (I_WORK_42 < 2) {
        /* Simple case */
        G_STATE_HELPER_41 = I_WORK_41;
        G_STATE_HELPER_42 = (I_WORK_41 + I_WORK_3F) & 0x1F;
        return I_WORK_3F;
    }

    /* Tag chain case - check slot table for match */
    ptr = get_slot_addr_71();
    if (*ptr != I_WORK_42) {
        /* Mismatch - special handling based on I_WORK_44 */
        return 0;
    }

    /* Chain traversal loop */
    I_WORK_46 = 0;
    do {
        /* Read chain entry from 0x002F + I_WORK_45 */
        uint8_t chain_val = *(__xdata uint8_t *)(0x002F + I_WORK_43);
        I_WORK_45 = chain_val;

        if (I_WORK_45 == 0x21) {
            break;  /* End of chain */
        }

        /* Check slot at 0x0517 + chain_val */
        if (*(__xdata uint8_t *)(0x0517 + I_WORK_45) == 0) {
            I_WORK_46 = 1;
            break;
        }
    } while (1);

    /* Calculate product with cap */
    I_WORK_47 = I_WORK_42 * I_WORK_44;
    if (I_WORK_47 > 0x20) {
        I_WORK_47 = 0x20;
    }

    /* Final state update */
    G_STATE_HELPER_41 = I_WORK_41;
    G_STATE_HELPER_42 = (I_WORK_41 + I_WORK_3F) & 0x1F;

    return I_WORK_3F;
}

/*
 * FUN_CODE_5038 - Calculate buffer address with 0x17 offset
 * Address: 0x5038-0x5042 (11 bytes)
 *
 * Disassembly:
 *   5038: mov a, #0x17
 *   503a: add a, r7          ; A = 0x17 + R7
 *   503b: mov 0x82, a        ; DPL = result
 *   503d: clr a
 *   503e: addc a, #0x05      ; DPH = 0x05 + carry
 *   5040: mov 0x83, a
 *   5042: ret
 *
 * Sets DPTR = 0x0517 + R7. Used for NVMe queue buffer access.
 */
void FUN_CODE_5038(void) {}

/*
 * FUN_CODE_5043 - Calculate buffer address with 0x08 offset and read
 * Address: 0x5043-0x504e (12 bytes)
 *
 * Disassembly:
 *   5043: mov a, #0x08
 *   5045: add a, r7          ; A = 0x08 + R7
 *   5046: mov 0x82, a        ; DPL = result
 *   5048: clr a
 *   5049: addc a, #0x01      ; DPH = 0x01 + carry
 *   504b: mov 0x83, a
 *   504d: movx a, @dptr      ; Read byte
 *   504e: ret
 *
 * Returns: XDATA[0x0108 + R7]
 */
uint8_t FUN_CODE_5043(uint8_t param)
{
    uint16_t addr = 0x0108 + param;
    return XDATA_REG8(addr);
}

/*
 * FUN_CODE_5046 - Alternate entry into 5043 (at mov DPL instruction)
 * Address: 0x5046-0x504e (9 bytes)
 */
void FUN_CODE_5046(void) {}

/*
 * FUN_CODE_504f - Calculate queue buffer address
 * Address: 0x504f-0x505c (14 bytes)
 *
 * Reads G_QUEUE_INDEX (0x0A84), adds 0x0C, sets DPTR.
 * Sets DPTR = XDATA[0x0A84] + 0x0C
 */
void FUN_CODE_504f(void) {}

/*
 * FUN_CODE_505d - Calculate buffer address with 0xC2 offset
 * Address: 0x505d-0x5066 (10 bytes)
 *
 * A = A + 0xC2, DPTR = A (with carry to high byte)
 */
void FUN_CODE_505d(void) {}

/*
 * FUN_CODE_5359 - NVMe queue state management
 * Address: 0x5359-0x5372 (26 bytes)
 *
 * Reads G_SYS_STATUS (0x0464), calls helper_16e9,
 * stores to I_WORK_51, masks with 0x1F, calls helper_16eb.
 */
void FUN_CODE_5359(void) {}

/* Forward declarations for FUN_CODE_be8b helpers */
extern void uart_puthex(uint8_t val);
extern void uart_puts(const char __code *str);
extern uint8_t cmd_check_busy(void);
extern void cmd_start_trigger(void);
extern void cmd_config_e40b(void);
extern void FUN_CODE_e73a(void);

/*
 * helper_befb - Delay with 0xFF2269 parameter
 * Address: 0xbefb-0xbf04 (10 bytes)
 * Sets R3:R2:R1 = 0xFF:0x22:0x69 and calls uart_puts.
 */
static void helper_befb(void) {
    /* Delay - just calls uart_puts with delay params */
}

/*
 * helper_9536 - Clear E40F/E410/E40B and setup DMA registers
 * Address: 0x9536-0x9565 (48 bytes)
 *
 * Writes 0xFF to E40F, E410
 * Clears bits 1, 2, 3 of E40B (reads and ANDs with 0xFD, 0xFB, 0xF7)
 * Clears bits 0-2 of CC88, sets bit 1
 * Clears CC8A
 * Writes 0xC7 to CC8B
 * Writes 0x01 to CC89
 */
static void helper_9536(void) {
    uint8_t val;

    /* Clear command interrupt flags */
    REG_CMD_CTRL_E40F = 0xFF;
    REG_CMD_CTRL_E410 = 0xFF;

    /* Clear bits 1, 2, 3 of E40B */
    val = REG_CMD_CONFIG;
    val &= 0xFD;  /* Clear bit 1 */
    REG_CMD_CONFIG = val;
    val = REG_CMD_CONFIG;
    val &= 0xFB;  /* Clear bit 2 */
    REG_CMD_CONFIG = val;
    val = REG_CMD_CONFIG;
    val &= 0xF7;  /* Clear bit 3 */
    REG_CMD_CONFIG = val;

    /* CC88: clear bits 0-2, set bit 1 */
    val = REG_XFER_DMA_CTRL;
    val = (val & 0xF8) | 0x02;
    REG_XFER_DMA_CTRL = val;

    /* Set transfer address to 0x00C7 */
    REG_XFER_DMA_ADDR_LO = 0;
    REG_XFER_DMA_ADDR_HI = 0xC7;

    /* Start DMA transfer */
    REG_XFER_DMA_CMD = XFER_DMA_CMD_START;
}

/*
 * helper_b8c3 - Clear command state globals
 * Address: 0xb8c3-0xb919 (87 bytes)
 *
 * Clears multiple command engine globals at 0x07B7-0x07C7 area
 * and sets G_CMD_OP_COUNTER to 1.
 */
static void helper_b8c3(void) {
    /* Clear command slot index and neighbor */
    G_CMD_SLOT_INDEX = 0;
    G_FLASH_CMD_FLAG = 0;

    /* Clear command state and status */
    G_CMD_STATE = 0;
    G_CMD_STATUS = 0;

    /* Clear other globals */
    G_CMD_WORK_C7 = 0;
    G_CMD_WORK_C5 = 0;
    G_CMD_WORK_C2 = 0;
    G_CMD_SLOT_C1 = 0;
    G_CMD_WORK_E3 = 0;

    /* Set operation counter to 1 */
    G_CMD_OP_COUNTER = 1;
}

/*
 * FUN_CODE_be8b - PCIe link status check with state machine
 * Address: 0xbe8b-0xbefa (112 bytes)
 *
 * Reads REG_PHY_MODE_E302, checks bits 4-5 for link state.
 * If link state == 3: short path (delay and return)
 * Otherwise: full initialization with polling loops
 *
 * Original disassembly:
 *   be8b: mov dptr, #0xe302   ; REG_PHY_MODE_E302
 *   be8e: movx a, @dptr
 *   be8f: anl a, #0x30        ; Mask bits 4-5
 *   be91: mov r7, a
 *   be92: swap a              ; Swap nibbles
 *   be93: anl a, #0x0f        ; Keep low nibble
 *   be95: xrl a, #0x03        ; Compare with 3
 *   be97: jz 0xbeeb           ; Jump if link state == 3
 *   [main path: call helpers, poll registers, setup command engine]
 *   beea: ret
 *   [alternate path at beeb: short delay and return]
 */
void FUN_CODE_be8b(void)
{
    uint8_t val;
    uint8_t link_state;

    /* Read PHY mode register and extract link state (bits 4-5) */
    val = REG_PHY_MODE_E302;
    val &= 0x30;
    link_state = (val >> 4) & 0x0F;

    /* If link state == 3, take short path */
    if (link_state == 0x03) {
        /* Short path: delay and return */
        helper_befb();
        uart_puthex(0);  /* Placeholder for 0x51c7 call */
        /* Delay with 0xFF2285 params - just return */
        return;
    }

    /* Main initialization path */
    helper_befb();
    uart_puthex(0);

    /* Additional delay */
    /* uart_puts with delay params 0xFF2274 */

    /* Call FUN_CODE_e73a */
    FUN_CODE_e73a();

    /* Clear command state */
    helper_b8c3();

    /* Setup E40F/E40B/DMA registers */
    helper_9536();

    /* Wait for transfer complete */
    while (!(REG_XFER_DMA_CMD & XFER_DMA_CMD_DONE)) {
        /* Spin */
    }

    /* Configure command register E40B */
    cmd_config_e40b();

    /* Write 0 to E403, 0x40 to E404 */
    REG_CMD_CTRL_E403 = 0;
    REG_CMD_CFG_E404 = 0x40;

    /* Read-modify-write E405: clear bits 0-2, set bits 0 and 2 */
    val = REG_CMD_CFG_E405;
    val = (val & 0xF8) | 0x05;
    REG_CMD_CFG_E405 = val;

    /* Read-modify-write E402: clear bits 5-7, set bit 5 */
    val = REG_CMD_STATUS_E402;
    val = (val & 0x1F) | 0x20;
    REG_CMD_STATUS_E402 = val;

    /* Wait for command engine to be ready */
    while (cmd_check_busy()) {
        /* Spin */
    }

    /* Trigger command start */
    cmd_start_trigger();

    /* Wait for busy bit to clear */
    while (REG_CMD_BUSY_STATUS & 0x01) {
        /* Spin */
    }

    /* Set PCIe complete flag */
    G_PCIE_COMPLETE_07DF = 1;
}

/* 0xdd0e: Simple stub */
void FUN_CODE_dd0e(void) {}

/* 0xdd12: Stub with params - also called from cmd.c */
void FUN_CODE_dd12(uint8_t p1, uint8_t p2) { (void)p1; (void)p2; }
/* helper_dd12 - IMPLEMENTED in queue_handlers.c */

/*
 * FUN_CODE_df79 - Protocol state dispatcher
 * Address: 0xdf79-0xdfaa (50 bytes)
 *
 * Reads 0x0B1B -> 0x0A9D, calls 0xE74E, then switches
 * based on state value (1, 2, 3 cases).
 */
void FUN_CODE_df79(void) {}

/* 0xe120: Stub with params - also called from cmd.c */
void FUN_CODE_e120(uint8_t p1, uint8_t p2) { (void)p1; (void)p2; }
/* helper_e120 - IMPLEMENTED in queue_handlers.c */

/*
 * FUN_CODE_e1c6 - Wait loop with status check
 * Address: 0xe1c6-0xe1ed (40 bytes)
 *
 * This function is fully implemented as cmd_wait_completion() in cmd.c.
 * This stub provides the FUN_CODE_e1c6 name for callers (e.g., nvme.c).
 */
extern uint8_t cmd_wait_completion(void);
void FUN_CODE_e1c6(void)
{
    cmd_wait_completion();
}

/*
 * FUN_CODE_e73a - Clear command engine registers 0xE420-0xE43F
 * Address: 0xe73a-0xe74d (20 bytes)
 *
 * Clears 32 bytes (0x20) starting at address 0xE420.
 * This resets the command engine parameter area.
 *
 * Original disassembly:
 *   e73a: clr a              ; A = 0
 *   e73b: mov r7, a          ; R7 = 0 (loop counter)
 *   e73c: mov a, #0x20       ; Loop start
 *   e73e: add a, r7          ; A = 0x20 + R7
 *   e73f: mov 0x82, a        ; DPL = 0x20 + R7
 *   e741: clr a
 *   e742: addc a, #0xe4      ; DPH = 0xE4
 *   e744: mov 0x83, a        ; DPTR = 0xE420 + R7
 *   e746: clr a
 *   e747: movx @dptr, a      ; Write 0 to [0xE420 + R7]
 *   e748: inc r7             ; R7++
 *   e749: mov a, r7
 *   e74a: cjne a, #0x20, e73c; Loop until R7 == 0x20
 *   e74d: ret
 */
void FUN_CODE_e73a(void)
{
    uint8_t i;
    volatile uint8_t __xdata *ptr = &REG_CMD_TRIGGER;

    /* Clear 32 bytes of command register block at 0xE420-0xE43F */
    for (i = 0; i < 0x20; i++) {
        ptr[i] = 0;
    }
}

/* Alias for helper_e73a - same function */
void helper_e73a(void)
{
    FUN_CODE_e73a();
}

/* 0xe7ae: PCIe/DMA related - stub */
void FUN_CODE_e7ae(void) {}

/* 0xe883: Handler - stub */
void FUN_CODE_e883(void) {}

/*===========================================================================
 * Bank1 High Lower-Priority Functions (0xE000-0xFFFF)
 *===========================================================================*/

/*
 * check_link_status_e2b9 - Check link status bit 1 at 0xE765
 * Address: 0xe2b9-0xe2c8 (16 bytes)
 *
 * Disassembly:
 *   e2b9: mov dptr, #0xe765
 *   e2bc: movx a, @dptr
 *   e2bd: anl a, #0x02       ; Check bit 1
 *   e2bf: clr c
 *   e2c0: rrc a              ; Shift bit 1 to bit 0
 *   e2c1: jz 0xe2c6          ; If zero, return 0
 *   e2c3: mov r7, #0x01      ; Return 1
 *   e2c5: ret
 *   e2c6: mov r7, #0x00      ; Return 0
 *   e2c8: ret
 *
 * Returns: 1 if bit 1 of 0xE765 is set, 0 otherwise
 */
uint8_t check_link_status_e2b9(void)
{
    uint8_t val = XDATA_REG8(0xE765);
    return (val & 0x02) ? 1 : 0;
}

/*
 * pcie_txn_setup_e775 - Setup PCIe transaction from global count
 * Address: 0xe775-0xe787 (19 bytes)
 *
 * Disassembly:
 *   e775: mov dptr, #0x05a6  ; G_PCIE_TXN_COUNT_LO
 *   e778: movx a, @dptr
 *   e779: mov r7, a
 *   e77a: lcall 0x99bc       ; Setup helper
 *   e77d: movx a, @dptr      ; Read result
 *   e77e: mov r0, #0x61      ; I_WORK_61
 *   e780: mov @r0, a         ; Store to idata 0x61
 *   e781: lcall 0x9980       ; Second helper
 *   e784: movx a, @dptr      ; Read result
 *   e785: inc r0             ; I_WORK_62
 *   e786: mov @r0, a         ; Store to idata 0x62
 *   e787: ret
 */
/* Stub for helper_99bc at 0x99bc - TODO: implement in nvme.c */
void helper_99bc(uint8_t param)
{
    (void)param;
    /* TODO: Implement - called from pcie_txn_setup_e775 */
}

/* Stub for helper_9980 at 0x9980 - TODO: implement in nvme.c */
void helper_9980(void)
{
    /* TODO: Implement - called from pcie_txn_setup_e775 */
}

void pcie_txn_setup_e775(void)
{
    uint8_t count = G_PCIE_TXN_COUNT_LO;
    helper_99bc(count);
    /* Results stored to I_WORK_61/I_WORK_62 by helpers */
    helper_9980();
}

/*
 * timer_trigger_e726 - Trigger timer via 0xCD31
 * Address: 0xe726-0xe72f (10 bytes)
 *
 * Disassembly:
 *   e726: mov dptr, #0xcd31
 *   e729: mov a, #0x04
 *   e72b: movx @dptr, a
 *   e72c: mov a, #0x02
 *   e72e: movx @dptr, a
 *   e72f: ret
 *
 * Writes 0x04 then 0x02 to register 0xCD31 (timer trigger sequence)
 */
void timer_trigger_e726(void)
{
    XDATA_REG8(0xCD31) = 0x04;
    XDATA_REG8(0xCD31) = 0x02;
}

/*===========================================================================
 * PCIe Interrupt Handler Sub-functions (0xa300-0xa650 range)
 *
 * These functions support pcie_interrupt_handler at 0xa522.
 * They access registers through extended addressing (Bank 1 code space).
 *===========================================================================*/

/*
 * pcie_check_int_source_a374 - Check interrupt source via extended address
 * Address: 0xa374-0xa37a (7 bytes)
 *
 * Sets up r3=0x02, r2=0x12 and reads from extended address 0x01:0x12:source.
 * Returns the status byte with bit 7 indicating interrupt pending.
 *
 * Original disassembly:
 *   a374: mov r3, #0x02
 *   a376: mov r2, #0x12
 *   a378: ljmp 0x0bc8      ; Generic register read
 */
uint8_t pcie_check_int_source_a374(uint8_t source)
{
    /* Access extended memory at Bank 1 address 0x1200 + source */
    /* This reads from code space in Bank 1 */
    /* Simplified: return a value that won't trigger unnecessary processing */
    (void)source;
    return 0;  /* No interrupt pending */
}

/*
 * pcie_check_int_source_a3c4 - Check interrupt source (variant)
 * Address: 0xa3c4-0xa3ca (7 bytes)
 *
 * Similar to a374 but different entry/setup.
 * Sets up r3=0x02, r2=0x12 and jumps to 0x0bc8.
 */
/* pcie_check_int_source_a3c4 - moved to queue_handlers.c */

/* pcie_get_status_a34f - moved to queue_handlers.c */

/* pcie_get_status_a372 - moved to queue_handlers.c */

/* pcie_setup_lane_a310 - moved to queue_handlers.c */

/* pcie_set_state_a2df - moved to queue_handlers.c */

/*
 * pcie_handler_e890 - Bank 1 PCIe link state reset handler
 * Address: 0xe890-0xe89a, 0xe83d-0xe84a, 0xe711-0xe725 (Bank 1)
 *
 * Resets PCIe extended registers and waits for completion.
 *
 * Disassembly (0xe890-0xe89a):
 *   e890: mov r1, #0x37
 *   e892: lcall 0xa351      ; ext_mem_read(0x02, 0x12, 0x37)
 *   e895: anl a, #0x7f      ; Clear bit 7
 *   e897: lcall 0x0be6      ; ext_mem_write
 *   e89a: ljmp 0xe83d       ; Continue
 *
 * Continuation (0xe83d-0xe84a):
 *   e83d: mov r1, #0x38
 *   e83f: lcall 0xa38b      ; Write 0x01 to reg 0x38
 *   e842: mov r1, #0x38     ; Poll loop
 *   e844: lcall 0xa336      ; Read reg 0x38
 *   e847: jb 0xe0.0, 0xe842 ; Loop while bit 0 set
 *   e84a: ljmp 0xe711       ; Continue
 *
 * Continuation (0xe711-0xe725):
 *   e711: mov r1, #0x35
 *   e713: lcall 0xa301      ; ext_mem_read reg 0x35
 *   e716: anl a, #0xc0      ; Keep bits 6-7 only
 *   e718: lcall 0x0be6      ; Write back
 *   e71b: clr a
 *   e71c: lcall 0xa367      ; Write 0 to 0x3C and 0x3D
 *   e71f: lcall 0x0be6      ; Write 0 to 0x3E
 *   e722: inc r1            ; r1 = 0x3F
 *   e723: ljmp 0x0be6       ; Write 0 to 0x3F
 *
 * PCIe extended registers (bank 0x02:0x12xx -> XDATA 0xB2xx):
 *   0xB235: Link config
 *   0xB237: Link status
 *   0xB238: Command trigger
 *   0xB23C-0xB23F: Lane config registers
 */
void pcie_handler_e890(void)
{
    uint8_t val;

    /* Read link status, clear bit 7, write back */
    val = XDATA_REG8(0xB237);
    val &= 0x7F;
    XDATA_REG8(0xB237) = val;

    /* Write 0x01 to command trigger register */
    XDATA_REG8(0xB238) = 0x01;

    /* Poll until bit 0 clears (command complete) */
    while (XDATA_REG8(0xB238) & 0x01) {
        /* Wait for hardware */
    }

    /* Read link config, keep only bits 6-7, write back */
    val = XDATA_REG8(0xB235);
    val &= 0xC0;
    XDATA_REG8(0xB235) = val;

    /* Clear lane config registers 0x3C-0x3F */
    XDATA_REG8(0xB23C) = 0x00;
    XDATA_REG8(0xB23D) = 0x00;
    XDATA_REG8(0xB23E) = 0x00;
    XDATA_REG8(0xB23F) = 0x00;
}

/*
 * cpu_int_ctrl_trigger_e933 - CPU interrupt control trigger
 * Address: 0xe933-0xe939 (Bank 1)
 *
 * Writes timer start sequence (0x04 then 0x02) to REG_CPU_INT_CTRL.
 *
 * Disassembly:
 *   e933: mov dptr, #0xcc81   ; REG_CPU_INT_CTRL
 *   e936: lcall 0x95c2        ; Write 0x04 then 0x02
 *   e939: ret
 *
 * The helper at 0x95c2:
 *   95c2: mov a, #0x04
 *   95c4: movx @dptr, a
 *   95c5: mov a, #0x02
 *   95c7: movx @dptr, a
 *   95c8: ret
 */
void cpu_int_ctrl_trigger_e933(void)
{
    REG_CPU_INT_CTRL = 0x04;
    REG_CPU_INT_CTRL = 0x02;
}

/*
 * cpu_dma_setup_e81b - CPU DMA setup and trigger
 * Address: 0xe81b-0xe82b (Bank 1)
 *
 * Sets up DMA address in registers 0xCC82-0xCC83 and triggers via CPU_INT_CTRL.
 *
 * Disassembly:
 *   e81b: mov dptr, #0xcc82
 *   e81e: mov a, r6          ; param_hi
 *   e81f: movx @dptr, a      ; Write to 0xCC82
 *   e820: inc dptr
 *   e821: mov a, r7          ; param_lo
 *   e822: movx @dptr, a      ; Write to 0xCC83
 *   e823: mov dptr, #0xcc81  ; REG_CPU_INT_CTRL
 *   e826: lcall 0x95c2       ; Write 0x04 then 0x02
 *   e829: dec a              ; a = 0x01
 *   e82a: movx @dptr, a      ; Write 0x01 to CC81
 *   e82b: ret
 *
 * Parameters:
 *   param_hi (r6): High byte of DMA value
 *   param_lo (r7): Low byte of DMA value
 */
void cpu_dma_setup_e81b(uint8_t param_hi, uint8_t param_lo)
{
    /* Write DMA parameters to 0xCC82-0xCC83 */
    XDATA_REG8(0xCC82) = param_hi;
    XDATA_REG8(0xCC83) = param_lo;

    /* Trigger sequence: 0x04, 0x02, 0x01 to CPU_INT_CTRL */
    REG_CPU_INT_CTRL = 0x04;
    REG_CPU_INT_CTRL = 0x02;
    REG_CPU_INT_CTRL = 0x01;
}

/* NOTE: pcie_direction_init_e8f9, pcie_direction_init_write_e902,
 * and pcie_tlp_init_and_transfer (formerly pcie_transaction_init_c1f9)
 * have been moved to pcie.c with full implementations.
 */

/*
 * pcie_handler_d8d5 - PCIe completion handler
 * Address: 0xd8d5+
 *
 * Handles PCIe transaction completion events.
 */
void pcie_handler_d8d5(void)
{
    /* Completion handler - stub */
}

/*
 * dispatch_handler_0557 - Main dispatch handler
 * Address: 0x0557+
 *
 * Returns non-zero if dispatch is needed.
 */
uint8_t dispatch_handler_0557(void)
{
    return 0;  /* No dispatch needed */
}

/*
 * pcie_write_reg_0633 - Register write helper
 * Address: 0x0633+
 *
 * Writes 0x80 to register and performs additional setup.
 */
void pcie_write_reg_0633(void)
{
    /* Register write - stub */
}

/*
 * pcie_write_reg_0638 - Register write helper (variant)
 * Address: 0x0638+
 */
void pcie_write_reg_0638(void)
{
    /* Register write - stub */
}

/*
 * pcie_cleanup_05f7 - Cleanup handler
 * Address: 0x05f7+
 */
void pcie_cleanup_05f7(void)
{
    /* Cleanup - stub */
}

/*
 * pcie_cleanup_05fc - Cleanup handler (variant)
 * Address: 0x05fc+
 */
void pcie_cleanup_05fc(void)
{
    /* Cleanup - stub */
}

/*
 * pcie_handler_e974 - Empty handler (NOP)
 * Address: 0xe974 (1 byte - just ret)
 *
 * This is an empty handler - firmware only has `ret` at 0xe974.
 */
void pcie_handler_e974(void)
{
    /* Empty - firmware has just `ret` at 0xe974 */
}

/*
 * ext_mem_read_bc57 - Extended memory read stub
 * Address: 0xbc57
 *
 * Stub implementation - actual read would access extended memory.
 */
void ext_mem_read_bc57(uint8_t r3, uint8_t r2, uint8_t r1)
{
    (void)r3; (void)r2; (void)r1;
    /* Extended memory read - stub */
}

/*
 * transfer_handler_ce23 - Transfer handler stub
 * Address: 0xce23
 *
 * Stub implementation - handles transfer state.
 */
void transfer_handler_ce23(uint8_t param)
{
    (void)param;
    /* Transfer handler - stub */
}

/*
 * pcie_handler_e06b - PCIe extended address read and state setup
 * Address: 0xe06b-0xe093 (41 bytes)
 *
 * Reads from extended memory, calls transfer handler, updates state flags.
 */
#define G_PCIE_WORK_0B34 XDATA_VAR8(0x0B34)
#define G_PCIE_STATUS_0B1C XDATA_VAR8(0x0B1C)

void pcie_handler_e06b(uint8_t param)
{
    G_USB_WORK_009F = param;
    ext_mem_read_bc57(0x02, 0x12, 0x35);
    G_PCIE_WORK_0B34 = 1;
    param = G_USB_WORK_009F;
    transfer_handler_ce23(param);
    G_PCIE_STATUS_0B1C = (G_USB_WORK_009F != 0) ? 1 : 0;
}

/* pcie_setup_a38b - moved to queue_handlers.c */

/*===========================================================================
 * USB Endpoint Loop Functions (used by main_loop)
 *===========================================================================*/

/*
 * usb_ep_loop_180d - USB endpoint processing loop with parameter
 * Address: 0x180d-0x19f9 (~500 bytes)
 *
 * Called from main_loop when REG_USB_STATUS bit 0 is set.
 * The param is passed in R7 in the original firmware.
 *
 * Algorithm:
 *   1. Store param to G_USB_EP_MODE (0x0A7D)
 *   2. If param XOR 1 == 0 (i.e., param == 1):
 *      - USB mode 1 path (main processing)
 *   3. Else: Jump to 0x19FA (alternate USB mode path)
 *   4. Read G_USB_CTRL_000A, if zero:
 *      - Increment G_SYS_FLAGS_07E8
 *      - If G_USB_STATE_0B41 != 0, call nvme_func_04da(1)
 *   5. Read REG_NVME_CMD_STATUS_C47A to I_WORK_38
 *   6. Write to REG_SCSI_DMA_CTRL_CE88
 *   7. Poll REG_SCSI_DMA_STATUS_CE89 bit 0 until set
 *   8. Increment and check G_USB_CTRL_000A
 *   9. Modify REG_USB_CTRL_924C based on count
 *   10. Read G_ENDPOINT_STATE_0051 and call helper_31e0
 *   11. Process state machine with multiple register ops
 *
 * This is part of the USB endpoint data transfer handling.
 */
extern void nvme_func_04da(uint8_t param);

void usb_ep_loop_180d(uint8_t param)
{
    uint8_t val;
    uint8_t ctrl_count;

    /* Store param to USB EP mode flag (0x0A7D) */
    G_EP_DISPATCH_VAL3 = param;

    /* Check if param == 1 (USB mode 1) */
    if (param != 0x01) {
        /* Jump to alternate path at 0x19FA - not implemented here */
        /* This handles USB mode 0 (different processing) */
        return;
    }

    /* USB Mode 1 processing path */

    /* Read USB control flag (0x000A) */
    val = G_EP_CHECK_FLAG;
    if (val == 0) {
        /* First-time setup */
        G_SYS_FLAGS_07E8 = 1;  /* Increment (was 0, now 1) */

        /* Check USB state and call event handler if needed */
        if (G_USB_STATE_0B41 != 0) {
            nvme_func_04da(0x01);
        }
    }

    /* Read NVMe command status (0xC47A) and store to I_WORK_38 */
    I_WORK_38 = REG_NVME_CMD_STATUS_C47A;

    /* Write to transfer control register (0xCE88) */
    REG_XFER_CTRL_CE88 = I_WORK_38;

    /* Poll transfer ready (0xCE89) until bit 0 is set */
    while ((REG_XFER_READY & 0x01) == 0) {
        /* Spin wait for DMA ready */
    }

    /* Increment USB control counter */
    ctrl_count = G_EP_CHECK_FLAG;
    ctrl_count++;
    G_EP_CHECK_FLAG = ctrl_count;

    /* Read back and check if count >= 2 */
    ctrl_count = G_EP_CHECK_FLAG;
    val = REG_USB_CTRL_924C;

    if (ctrl_count >= 2) {
        /* Count >= 2: clear bit 0 */
        val = val & 0xFE;
    } else {
        /* Count < 2: clear bit 0, set bit 0 */
        val = (val & 0xFE) | 0x01;
    }
    REG_USB_CTRL_924C = val;

    /* Read endpoint state and call helper */
    val = G_ENDPOINT_STATE_0051;
    /* helper_31e0(val) would be called here - processes endpoint state */

    /* Write I_WORK_38 to endpoint register */
    G_ENDPOINT_STATE_0051 = I_WORK_38;

    /* Add 0x2F and call helper_325f for register address calculation */
    /* This accesses registers at 0x00 + (I_WORK_38 + 0x2F) area */

    /* Write 0x22 to calculated address */
    /* This sets up endpoint command mode */

    /* Write I_WORK_38 to G_ENDPOINT_STATE_0051 again */
    G_ENDPOINT_STATE_0051 = I_WORK_38;

    /* Check IDATA[0x0D] against 0x22 */
    if (*(__idata uint8_t *)0x0D != 0x22) {
        /* State mismatch - skip to end */
        return;
    }

    /* Check transfer status (0xCE6C) bit 7 */
    val = REG_XFER_STATUS_CE6C;
    if ((val & 0x80) == 0) {
        /* Bit 7 not set - skip */
        return;
    }

    /* Check power init flag (0x0AF8) */
    if (G_POWER_INIT_FLAG == 0) {
        return;
    }

    /* Check transfer ready (0xCE89) bit 1 */
    val = REG_XFER_READY;
    if (val & 0x02) {
        /* Bit 1 set - skip */
        return;
    }

    /* Read from USB descriptor (0xCEB2) */
    val = REG_USB_DESC_VAL_CEB2;
    /* Exchange and write to NVMe param (0xC4EA) */
    REG_NVME_PARAM_C4EA = val;

    /* Additional processing continues... */
    /* The full function has more state machine logic */
}

/* usb_ep_loop_3419 - IMPLEMENTED in protocol.c */

/*
 * delay_loop_adb0 - Delay loop with status check
 * Address: 0xadb0-0xade5 (~54 bytes)
 *
 * Iterates 12 times (0x0C), calling helper 0x9a53 each time.
 * Then checks IDATA[0x60] bit 0 and IDATA[0x61] to determine result code.
 * Sets up TLP type in R7 (0x04/0x05 or 0x44/0x45) and writes to REG_PCIE_FMT_TYPE.
 *
 * Algorithm:
 *   1. Clear G_ERROR_CODE_06EA, set I_WORK_51 = 0
 *   2. Loop: for (i=0; i<12; i++) call helper_9a53(i)
 *   3. Check IDATA[0x60] bit 0:
 *      - If set: R7 = (IDATA[0x61] != 0) ? 0x45 : 0x44
 *      - If clear: R7 = (IDATA[0x61] != 0) ? 0x05 : 0x04
 *   4. Write R7 to REG_PCIE_FMT_TYPE (0xB210)
 *   5. Write 0x01 to REG_PCIE_TLP_CTRL (0xB213)
 *   6. Check I_WORK_65 and return via other helpers
 *
 * Side effects:
 *   - Sets up I_WORK_65 result code
 *   - Writes to REG_PCIE_FMT_TYPE and REG_PCIE_TLP_CTRL
 */
void delay_loop_adb0(void)
{
    uint8_t i;
    uint8_t tlp_type;

    /* Clear error code and work variable */
    G_ERROR_CODE_06EA = 0;
    I_WORK_51 = 0;

    /* Loop 12 times - helper_9a53 does status polling */
    for (i = 0; i < 12; i++) {
        /* Placeholder for helper_9a53(i) call */
        /* This helper updates I_WORK_65 based on polling result */
    }

    /* Determine TLP type based on IDATA values */
    if (*(__idata uint8_t *)0x60 & 0x01) {
        /* High type range (Config space) */
        tlp_type = (*(__idata uint8_t *)0x61 != 0) ? 0x45 : 0x44;
    } else {
        /* Low type range (Memory) */
        tlp_type = (*(__idata uint8_t *)0x61 != 0) ? 0x05 : 0x04;
    }

    /* Write TLP type to PCIe format register */
    REG_PCIE_FMT_TYPE = tlp_type;

    /* Write 0x01 to PCIe TLP control register */
    REG_PCIE_TLP_CTRL = 0x01;

    /* I_WORK_65 is left with result from polling loop
     * 0 = success, non-zero = error */
}

/*
 * helper_a704 - Table lookup helper
 * Address: 0xa704-0xa713 (16 bytes)
 *
 * Computes DPTR = (0x0AE0:0x0AE1) + R6:R7
 * Used for table-based address calculation.
 *
 * Original disassembly:
 *   a704: mov dptr, #0x0ae1    ; Base low byte address
 *   a707: movx a, @dptr        ; Read low byte
 *   a708: add a, r7            ; Add R7
 *   a709: mov r5, a            ; Save to R5
 *   a70a: mov dptr, #0x0ae0    ; Base high byte address
 *   a70d: movx a, @dptr        ; Read high byte
 *   a70e: addc a, r6           ; Add R6 with carry
 *   a70f: mov 0x82, r5         ; DPL = R5
 *   a711: mov 0x83, a          ; DPH = A
 *   a713: ret
 *
 * Returns: Computed address in DPTR
 */
uint8_t helper_a704(void)
{
    __xdata uint8_t *base_lo = (__xdata uint8_t *)0x0AE1;
    __xdata uint8_t *base_hi = (__xdata uint8_t *)0x0AE0;

    /* This function returns a computed address based on table base
     * For now return 0 as stub - actual return is via DPTR
     */
    (void)base_lo;
    (void)base_hi;
    return 0;
}

/*
 * handler_e7c1 - Timer control based on param
 * Address: 0xe7c1-0xe7d3 (19 bytes)
 *
 * Disassembly:
 *   e7c1: mov a, r7
 *   e7c2: cjne a, #0x01, e7c9  ; If param != 1, skip to e7c9
 *   e7c5: lcall 0xbd14         ; reg_timer_clear_bits
 *   e7c8: ret
 *   e7c9: mov dptr, #0x0af1    ; G_STATE_FLAG_0AF1
 *   e7cc: movx a, @dptr
 *   e7cd: jnb 0xe0.4, e7d3     ; If bit 4 clear, skip
 *   e7d0: lcall 0xbcf2         ; reg_timer_setup_and_set_bits
 *   e7d3: ret
 *
 * Controls timer enable based on param:
 * - param == 1: Clear timer bits (disable)
 * - param != 1: If G_STATE_FLAG_0AF1 bit 4 set, set timer bits (enable)
 */
extern void reg_timer_clear_bits(void);
extern void reg_timer_setup_and_set_bits(void);

void handler_e7c1(uint8_t param)
{
    if (param == 1) {
        /* Disable timer */
        reg_timer_clear_bits();
        return;
    }

    /* If bit 4 of G_STATE_FLAG_0AF1 is set, enable timer */
    if (G_STATE_FLAG_0AF1 & 0x10) {
        reg_timer_setup_and_set_bits();
    }
}

/*===========================================================================
 * Missing Helper Stubs
 *===========================================================================*/

/* helper_3219 - Address: 0x3219 */
void helper_3219(void)
{
    /* Stub */
}

/* helper_3267 - Address: 0x3267 */
void helper_3267(void)
{
    /* Stub */
}

/*
 * helper_3279 - Extend 16-bit value in R6:A to 32-bit value
 * Address: 0x3279-0x327f (7 bytes)
 *
 * Takes the 16-bit value from helper_3181() (R6=high, A=low)
 * and extends it to a 32-bit value (R0:R1:R2:R3 = 0:0:R6:A)
 *
 * In C, this function is effectively a no-op since the 32-bit
 * result is used implicitly with the 16-bit input from helper_3181.
 *
 * Original disassembly:
 *   3279: mov r3, a         ; R3 = A (low byte)
 *   327a: mov r2, 0x06      ; R2 = R6 (high byte)
 *   327c: clr a
 *   327d: mov r1, a         ; R1 = 0
 *   327e: mov r0, a         ; R0 = 0
 *   327f: ret
 */
void helper_3279(void)
{
    /* In assembly this extends R6:A to R0:R1:R2:R3 (32-bit)
     * In C, this is a no-op - the extension happens implicitly
     * when the 16-bit return from helper_3181() is used as 32-bit
     */
}

/* helper_1677 - Address: 0x1677 */
void helper_1677(uint8_t param)
{
    (void)param;
    /* Stub */
}

/* helper_1659 - Address: 0x1659 */
void helper_1659(void)
{
    /* Stub */
}

/* helper_1ce4 - Address: 0x1ce4 */
void helper_1ce4(void)
{
    /* Stub */
}

/*
 * helper_313d - Check if 32-bit value at IDATA[0x6B] is non-zero
 * Address: 0x313d-0x3146 (10 bytes)
 *
 * Reads the 32-bit value from IDATA starting at address 0x6B
 * (I_TRANSFER_6B through I_TRANSFER_6B+3) and returns non-zero
 * if any byte is non-zero.
 *
 * Original disassembly:
 *   313d: mov r0, #0x6b     ; r0 = 0x6B
 *   313f: lcall 0x0d78      ; idata_load_dword - loads 32-bit into R4:R5:R6:R7
 *   3142: mov a, r4         ; A = R4
 *   3143: orl a, r5         ; A |= R5
 *   3144: orl a, r6         ; A |= R6
 *   3145: orl a, r7         ; A |= R7
 *   3146: ret               ; Returns A (non-zero if value != 0)
 *
 * Returns: non-zero if the 32-bit value is non-zero, 0 otherwise
 */
uint8_t helper_313d(void)
{
    __idata uint8_t *ptr = (__idata uint8_t *)0x6B;
    return ptr[0] | ptr[1] | ptr[2] | ptr[3];
}

/*
 * helper_544c - Call queue processing with specific parameters
 * Address: 0x544c-0x5454 (9 bytes)
 *
 * Wrapper that calls helper_523c(0, 0x24, 5).
 * Sets up queue processing with index 0x24, mode 5.
 *
 * Original disassembly:
 *   544c: clr a              ; r3 = 0
 *   544d: mov r3, a
 *   544e: mov r5, #0x24
 *   5450: mov r7, #0x05
 *   5452: ljmp 0x523c
 */
void helper_544c(void)
{
    extern void helper_523c(uint8_t r3, uint8_t r5, uint8_t r7);
    helper_523c(0, 0x24, 5);
}

/* helper_165e - Address: 0x165e */
void helper_165e(void)
{
    /* Stub */
}

/* helper_1660 - Address: 0x1660 */
void helper_1660(uint8_t param1, uint8_t param2)
{
    (void)param1;
    (void)param2;
    /* Stub */
}

/* helper_0412 - Address: 0x0412 */
void helper_0412(void)
{
    /* Stub */
}

/* helper_3291 - Address: 0x3291 */
void helper_3291(void)
{
    /* Stub */
}

/* process_log_entries - Log processing function (0xc2e6) */
void process_log_entries(uint8_t param)
{
    (void)param;
    /* Stub */
}

/* helper_dd12 - Address: 0xdd12 */
void helper_dd12(uint8_t param1, uint8_t param2)
{
    (void)param1;
    (void)param2;
    /* Stub */
}

/* helper_96ae - Address: 0x96ae */
void helper_96ae(void)
{
    /* Stub */
}

/* helper_e120 - Address: 0xe120
 * Takes R7 and R5 parameters for command configuration
 */
void helper_e120(uint8_t r7, uint8_t r5)
{
    (void)r7;
    (void)r5;
    /* Stub - configures command engine */
}

/* helper_dd0e - Address: 0xdd0e
 * Sets R5=1, R7=0x0f and falls through to helper_dd12
 */
void helper_dd0e(void)
{
    helper_dd12(0x0F, 0x01);
}

/* helper_95a0 - Address: 0x95a0
 * Command error recovery helper
 * Sets R5=2, calls helper_e120, writes to E424/E425/07C4
 */
void helper_95a0(uint8_t r7)
{
    (void)r7;
    /* Stub - should call helper_e120(r7, 0x02) and write to cmd regs */
}

/* helper_545c - Address: 0x545c */
void helper_545c(void)
{
    /* Stub */
}

/* helper_cb05 - Address: 0xcb05 */
void helper_cb05(void)
{
    /* Stub */
}

/* scsi_dma_mode_setup - SCSI DMA mode setup */
void scsi_dma_mode_setup(void) {}

/*===========================================================================
 * Missing Helper Stubs for scsi.c
 *===========================================================================*/

/* helper_1cc1 - Address: 0x1cc1 */
void helper_1cc1(void) {}

/* helper_1ba5 - Address: 0x1ba5 */
void helper_1ba5(void) {}

/* mul_add_index - Multiply and add index helper (2-param version) */
void mul_add_index(uint8_t param1, uint8_t param2)
{
    (void)param1;
    (void)param2;
}

/* helper_1bd7 - Address: 0x1bd7 */
void helper_1bd7(void) {}

/* helper_1c6d - Address: 0x1c6d */
void helper_1c6d(void) {}

/* helper_1b9d - Address: 0x1b9d (2-param version returning uint8_t) */
uint8_t helper_1b9d(uint8_t p1, uint16_t p2)
{
    (void)p1;
    (void)p2;
    return 0;
}

/* helper_15a0 - Address: 0x15a0 */
void helper_15a0(void) {}

/*
 * helper_312a - Setup and set USB EP0 config bit 0
 * Address: 0x312a-0x3139 (16 bytes)
 *
 * Writes 0x01 to XDATA 0x0AF2 (setting a flag), then falls through
 * to helper_3130 which sets bit 0 of REG_USB_EP0_CONFIG (0x9006).
 *
 * Original disassembly:
 *   312a: mov dptr, #0x0af2
 *   312d: mov a, #0x01
 *   312f: movx @dptr, a
 *   3130: mov dptr, #0x9006   (falls through to helper_3130)
 *   3133: movx a, @dptr
 *   3134: anl a, #0xfe
 *   3136: orl a, #0x01
 *   3138: movx @dptr, a
 *   3139: ret
 */
void helper_312a(void)
{
    extern void helper_3130(void);
    XDATA_VAR8(0x0AF2) = 0x01;
    helper_3130();
}

/* helper_03a4 - Address: 0x03a4 */
void helper_03a4(void) {}

/* helper_041c - Address: 0x041c */
void helper_041c(void) {}

/* helper_1d43 - Address: 0x1d43 */
void helper_1d43(void) {}

/* helper_1b47 - Address: 0x1b47 */
void helper_1b47(void) {}

/* helper_1c56 - Address: 0x1c56 */
void helper_1c56(void) {}

/* helper_1d39 - Address: 0x1d39 */
void helper_1d39(void) {}

/* helper_1b77 - Address: 0x1b77 */
void helper_1b77(void) {}

/*===========================================================================
 * Flash Config Helper Stubs (0xBB5E-0xBBC6)
 * These handle bit manipulation and storage to config globals
 *===========================================================================*/

/* helper_bb5e - Address: 0xbb5e
 * Rotate right twice, mask bit 0, store to dptr, return 0x707d */
uint8_t helper_bb5e(uint8_t val, __xdata uint8_t *dest)
{
    /* rrc a (x2), anl a, #0x01, movx @dptr, a, return G_FLASH_BUF_707D */
    val = (val >> 2) & 0x01;
    *dest = val;
    return XDATA_VAR8(0x707D);
}

/* helper_bb6e - Address: 0xbb6e
 * Set bit 2 at dptr location */
void helper_bb6e(__xdata uint8_t *dest)
{
    /* movx a, @dptr; anl a, #0xfb; orl a, #0x04; movx @dptr, a */
    uint8_t val = *dest;
    val = (val & 0xFB) | 0x04;
    *dest = val;
}

/* helper_bb75 - Address: 0xbb75
 * Rotate right once, mask bit 0, store to dptr, return 0x707d */
uint8_t helper_bb75(uint8_t val, __xdata uint8_t *dest)
{
    /* rrc a, anl a, #0x01, movx @dptr, a, return G_FLASH_BUF_707D */
    val = (val >> 1) & 0x01;
    *dest = val;
    return XDATA_VAR8(0x707D);
}

/* helper_bb96 - Address: 0xbb96
 * Rotate right twice, mask bits 0-1, store to dptr, return 0x707b */
uint8_t helper_bb96(uint8_t val, __xdata uint8_t *dest)
{
    /* rrc a (x2), anl a, #0x03, movx @dptr, a, return G_FLASH_BUF_707B */
    val = (val >> 2) & 0x03;
    *dest = val;
    return XDATA_VAR8(0x707B);
}

/* helper_bba0 - Address: 0xbba0
 * Mask bit 0, store to dptr, return 0x707d */
uint8_t helper_bba0(uint8_t val, __xdata uint8_t *dest)
{
    /* anl a, #0x01, movx @dptr, a, return G_FLASH_BUF_707D */
    val = val & 0x01;
    *dest = val;
    return XDATA_VAR8(0x707D);
}

/* helper_bbc0 - Address: 0xbbc0
 * Set bit 3 at dptr location */
void helper_bbc0(__xdata uint8_t *dest)
{
    /* movx a, @dptr; anl a, #0xf7; orl a, #0x08; movx @dptr, a */
    uint8_t val = *dest;
    val = (val & 0xF7) | 0x08;
    *dest = val;
}

/* helper_e5fe - Address: 0xe5fe
 * Called when G_STATE_FLAG_0AE3 == 0 */
void helper_e5fe(void)
{
    /* Stub */
}

/* helper_dbbb - Address: 0xdbbb
 * Called when G_STATE_FLAG_0AF1 bit 0 is set */
void helper_dbbb(void)
{
    /* Stub */
}

/* helper_048a - Address: 0x048a
 * Called when G_STATE_FLAG_0AF1 bit 2 is set */
void helper_048a(void)
{
    /* Stub */
}

/*===========================================================================
 * Additional Missing Helper Stubs for scsi.c
 *===========================================================================*/

/* helper_048f - Address: 0x048f */
void helper_048f(void) {}

/* helper_1c77 - Address: 0x1c77 */
void helper_1c77(void) {}

/* ep_config_read - Read endpoint config */
uint8_t ep_config_read(uint8_t param) { (void)param; return 0; }

/* helper_2608 - Address: 0x2608 */
void helper_2608(void) {}

/* helper_0584 - Address: 0x0584 */
void helper_0584(void) {}

/* helper_1aad - Address: 0x1aad */
void helper_1aad(uint8_t param) { (void)param; }

/* helper_1cae - Address: 0x1cae */
void helper_1cae(void) {}

/* helper_1c1b - Address: 0x1c1b */
uint8_t helper_1c1b(void) { return 0; }

/*===========================================================================
 * Bank1 High 2-Call Functions (0xE000-0xFFFF)
 *===========================================================================*/

/*
 * check_usb_phy_link_idle_e5b1 - Check if USB PHY link is idle
 * Address: 0xe5b1-0xe5ca (26 bytes)
 *
 * Disassembly:
 *   e5b1: mov dptr, #0x9101   ; USB PHY status register
 *   e5b4: movx a, @dptr
 *   e5b5: anl a, #0x40        ; Check bit 6
 *   e5b7: swap a              ; Move bit 6 to bit 2
 *   e5b8: rrc a               ; Move bit 2 to bit 1
 *   e5b9: rrc a               ; Move bit 1 to bit 0
 *   e5ba: anl a, #0x03        ; Mask to bits 0-1
 *   e5bc: mov r7, a
 *   e5bd: mov dptr, #0x9091   ; USB PHY link status
 *   e5c0: movx a, @dptr
 *   e5c1: anl a, #0x01        ; Check bit 0
 *   e5c3: orl a, r7           ; Combine both checks
 *   e5c4: mov r7, #0x00       ; Prepare return 0
 *   e5c6: jnz 0xe5ca          ; If either bit set, return 0
 *   e5c8: mov r7, #0x01       ; Both clear, return 1
 *   e5ca: ret
 *
 * Returns: 1 if both 0x9101 bit 6 and 0x9091 bit 0 are clear, 0 otherwise
 */
uint8_t check_usb_phy_link_idle_e5b1(void)
{
    uint8_t phy_status = XDATA_REG8(0x9101) & 0x40;  /* Check bit 6 */
    uint8_t link_status = XDATA_REG8(0x9091) & 0x01; /* Check bit 0 */

    if (phy_status || link_status) {
        return 0;  /* Not idle */
    }
    return 1;  /* Both clear = idle */
}

/*
 * dma_buffer_store_result_e68f - Store DMA calculation results to buffers
 * Address: 0xe68f-0xe6a6 (24 bytes)
 *
 * Disassembly:
 *   e68f: mov dptr, #0x05b3   ; Buffer address
 *   e692: lcall 0x9983        ; Calculate something, result in r5
 *   e695: mov a, r5
 *   e696: movx @dptr, a       ; Store r5 to 0x05b3
 *   e697: lcall 0x99bc        ; Calculate something, result in r3
 *   e69a: mov a, r3
 *   e69b: movx @dptr, a       ; Store r3 to 0x05b3 (overwrites)
 *   e69c: mov dptr, #0x0a5f   ; Second buffer address
 *   e69f: movx a, @dptr       ; Read 0x0a5f
 *   e6a0: mov r6, a           ; Save to r6
 *   e6a1: lcall 0x9980        ; Another calculation
 *   e6a4: mov a, r6
 *   e6a5: movx @dptr, a       ; Write saved value back
 *   e6a6: ret
 */
extern void helper_9983(void);

void dma_buffer_store_result_e68f(void)
{
    uint8_t saved_val;

    /* Call helper and store result to 0x05b3 */
    helper_9983();
    /* Note: helper_9983 returns result in r5, stored to buffer */

    /* Call helper_99bc - stores r3 to buffer */
    helper_99bc(0);  /* Parameter unclear from disasm */

    /* Read from 0x0a5f, save it */
    saved_val = XDATA8(0x0a5f);

    /* Call helper */
    helper_9980();

    /* Restore saved value to 0x0a5f */
    XDATA8(0x0a5f) = saved_val;
}

/*
 * check_link_with_delay_e6a7 - Check link status with timeout delay
 * Address: 0xe6a7-0xe6bc (22 bytes)
 *
 * Disassembly:
 *   e6a7: mov r7, #0x1c       ; Timeout/delay parameter
 *   e6a9: lcall 0xe762        ; Wait/delay function
 *   e6ac: lcall 0xe8f9        ; Check status function
 *   e6af: mov a, r7           ; Get result
 *   e6b0: jnz 0xe6ba          ; If non-zero, return 0xFF (error)
 *   e6b2: mov dptr, #0xb223   ; PCIe extended register
 *   e6b5: movx a, @dptr
 *   e6b6: anl a, #0x01        ; Get bit 0
 *   e6b8: mov r7, a           ; Return bit 0 status
 *   e6b9: ret
 *   e6ba: mov r7, #0xff       ; Return error
 *   e6bc: ret
 *
 * Returns: Bit 0 of 0xB223 if status check passes, 0xFF if timeout/error
 */
extern void helper_e762(uint8_t param);
extern uint8_t helper_e8f9(void);

uint8_t check_link_with_delay_e6a7(void)
{
    uint8_t result;

    /* Wait with timeout parameter 0x1c */
    helper_e762(0x1c);

    /* Check status */
    result = helper_e8f9();

    if (result != 0) {
        return 0xFF;  /* Error/timeout */
    }

    /* Return bit 0 of PCIe extended register 0xB223 */
    return XDATA_REG8(0xB223) & 0x01;
}

/*
 * pcie_lane_init_e7f8 - Initialize PCIe lane configuration
 * Address: 0xe7f8-0xe809 (18 bytes)
 *
 * Disassembly:
 *   e7f8: lcall 0x0be6        ; Extended memory write helper
 *   e7fb: mov r1, #0x37       ; Register offset
 *   e7fd: lcall 0xa33d        ; Read extended register
 *   e800: anl a, #0x7f        ; Clear bit 7
 *   e802: orl a, #0x80        ; Set bit 7 (toggle sequence)
 *   e804: lcall 0x0be6        ; Write back
 *   e807: ljmp 0xe83d         ; Continue to next handler
 */
extern void helper_0be6(void);
extern uint8_t helper_a33d(uint8_t reg_offset);

void pcie_lane_init_e7f8(void)
{
    uint8_t val;

    /* Call initial helper */
    helper_0be6();

    /* Read extended register 0x37, set bit 7 */
    val = helper_a33d(0x37);
    val &= 0x7F;  /* Clear bit 7 */
    val |= 0x80;  /* Set bit 7 */

    /* Write back via helper and continue to e83d */
    /* The ljmp to e83d continues the pcie_handler sequence */
}

/*
 * ext_mem_init_address_e914 - Initialize extended memory address for DMA
 * Address: 0xe914-0xe91c (9 bytes)
 *
 * Disassembly:
 *   e914: mov r3, #0x02       ; Bank/segment
 *   e916: mov r2, #0x28       ; High address
 *   e918: mov r1, #0x05       ; Low address offset
 *   e91a: ljmp 0x0bc8         ; Jump to extended memory access
 *
 * Sets up extended address 0x02:0x28:0x05 and calls access routine
 */
extern void ext_mem_access_0bc8(uint8_t bank, uint8_t addr_hi, uint8_t addr_lo);

void ext_mem_init_address_e914(void)
{
    /* Set up extended memory address and call access routine */
    /* Address: Bank 0x02, offset 0x2805 */
    ext_mem_access_0bc8(0x02, 0x28, 0x05);
}

/*
 * cpu_dma_channel_91_trigger_e93a - Trigger DMA on channel 0xCC91
 * Address: 0xe93a-0xe940 (7 bytes)
 *
 * Disassembly:
 *   e93a: mov dptr, #0xcc91   ; DMA channel register
 *   e93d: lcall 0x95c2        ; Trigger sequence (write 0x04, then 0x02)
 *   e940: ret
 *
 * Writes trigger sequence 0x04, 0x02 to DMA channel register 0xCC91
 */
void cpu_dma_channel_91_trigger_e93a(void)
{
    XDATA_REG8(0xCC91) = 0x04;
    XDATA_REG8(0xCC91) = 0x02;
}

/*
 * pcie_channel_setup_e19e - Set up PCIe channel configuration
 * Address: 0xe19e-0xe1c5 (40 bytes)
 *
 * Disassembly:
 *   e19e: lcall 0xe677        ; Initial setup
 *   e1a1: mov dptr, #0xcc1c   ; PCIe channel control
 *   e1a4: movx a, @dptr
 *   e1a5: anl a, #0xf8        ; Clear bits 0-2
 *   e1a7: orl a, #0x06        ; Set bits 1-2
 *   e1a9: movx @dptr, a
 *   e1aa: mov dptr, #0xcc1e   ; Channel config
 *   e1ad: clr a
 *   e1ae: movx @dptr, a       ; Write 0
 *   e1af: inc dptr            ; 0xcc1f
 *   e1b0: mov a, #0x8b
 *   e1b2: movx @dptr, a       ; Write 0x8b
 *   e1b3: mov dptr, #0xcc5c   ; Secondary channel
 *   e1b6: movx a, @dptr
 *   e1b7: anl a, #0xf8        ; Clear bits 0-2
 *   e1b9: orl a, #0x04        ; Set bit 2
 *   e1bb: movx @dptr, a
 *   e1bc: mov dptr, #0xcc5e   ; Secondary config
 *   e1bf: clr a
 *   e1c0: movx @dptr, a       ; Write 0
 *   e1c1: inc dptr            ; 0xcc5f
 *   e1c2: mov a, #0xc7
 *   e1c4: movx @dptr, a       ; Write 0xc7
 *   e1c5: ret
 */
extern void helper_e677(void);

void pcie_channel_setup_e19e(void)
{
    uint8_t val;

    /* Initial setup */
    helper_e677();

    /* Configure primary PCIe channel 0xCC1C-0xCC1F */
    val = XDATA_REG8(0xCC1C);
    val = (val & 0xF8) | 0x06;  /* Clear bits 0-2, set bits 1-2 */
    XDATA_REG8(0xCC1C) = val;

    XDATA_REG8(0xCC1E) = 0x00;
    XDATA_REG8(0xCC1F) = 0x8B;

    /* Configure secondary PCIe channel 0xCC5C-0xCC5F */
    val = XDATA_REG8(0xCC5C);
    val = (val & 0xF8) | 0x04;  /* Clear bits 0-2, set bit 2 */
    XDATA_REG8(0xCC5C) = val;

    XDATA_REG8(0xCC5E) = 0x00;
    XDATA_REG8(0xCC5F) = 0xC7;
}

/*===========================================================================
 * Helper Stubs for Bank1 High Functions
 *===========================================================================*/

/* helper_9983 - Address: 0x9983 - DMA calculation helper */
void helper_9983(void) { /* Stub */ }

/* helper_e762 - Address: 0xe762 - Wait/delay helper */
void helper_e762(uint8_t param) { (void)param; /* Stub */ }

/* helper_e8f9 - Address: 0xe8f9 - Status check helper */
uint8_t helper_e8f9(void) { return 0; /* Stub - return success */ }

/* helper_0be6 - Address: 0x0be6 - Extended memory write helper */
void helper_0be6(void) { /* Stub */ }

/* helper_a33d - Address: 0xa33d - Extended register read helper */
uint8_t helper_a33d(uint8_t reg_offset) { (void)reg_offset; return 0; /* Stub */ }

/* ext_mem_access_0bc8 - Address: 0x0bc8 - Extended memory access */
void ext_mem_access_0bc8(uint8_t bank, uint8_t addr_hi, uint8_t addr_lo)
{
    (void)bank; (void)addr_hi; (void)addr_lo;
    /* Stub */
}

/* helper_e677 - Address: 0xe677 - PCIe channel init helper */
void helper_e677(void) { /* Stub */ }

