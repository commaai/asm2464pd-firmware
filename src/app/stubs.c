/*
 * ASM2464PD Firmware - Function Stubs
 *
 * Stub implementations for functions that have not been fully reverse-engineered yet.
 * These stubs provide minimal implementations to allow the firmware to build.
 *
 * Each stub should be replaced with the actual implementation as the function is
 * reverse-engineered from the original firmware.
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/* External function declarations */
extern void phy_link_training(void);  /* 0xD702 - in phy.c */
extern void timer_wait(uint8_t timeout_lo, uint8_t timeout_hi, uint8_t mode);  /* 0xE80A - in timer.c */
extern uint8_t banked_load_byte(uint8_t addrlo, uint8_t addrhi, uint8_t memtype);  /* 0x0bc8 - in utils.c */
extern uint8_t pcie_read_status_a334(void);  /* 0xa334 - in queue_handlers.c */
extern void pcie_handler_e890(void);  /* 0xe890 - in pcie.c */
extern uint8_t get_pcie_status_flags_e00c(void);  /* 0xe00c - in pcie.c */
extern void pcie_lane_init_e7f8(void);  /* 0xe7f8 - defined later in this file */
extern void clear_pcie_status_bytes_e8cd(void);  /* 0xe8cd - in pcie.c */

/* PCIe status work bytes used by transfer_handler_ce23 */
#ifndef G_PCIE_WORK_0B34
#define G_PCIE_WORK_0B34      XDATA_VAR8(0x0B34)
#endif

/* Forward declarations for functions defined later in this file */
void pcie_handler_e06b(uint8_t param);
void cmd_trigger_params(uint8_t param1, uint8_t param2);
void helper_95e1(uint8_t r7, uint8_t r5);

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

/* transfer_func_16b0 - moved to dma.c */

/*
 * helper_16e9 - Calculate DPTR address 0x0456 + param
 * Address: 0x16e9-0x16f2 (10 bytes)
 *
 * Disassembly:
 *   16e9: add a, #0x56       ; A = param + 0x56
 *   16eb: mov DPL, a         ; DPL = A
 *   16ed: clr a
 *   16ee: addc a, #0x04      ; DPH = 0x04 + carry
 *   16f0: mov DPH, a
 *   16f2: ret
 *
 * Returns: XDATA pointer at 0x0456 + param
 */
__xdata uint8_t * helper_16e9(uint8_t param)
{
    return (__xdata uint8_t *)(0x0456 + param);
}

/*
 * helper_16eb - Calculate DPTR address 0x0400 + param (mid-entry of 16e9)
 * Address: 0x16eb-0x16f2 (8 bytes)
 *
 * Disassembly:
 *   16eb: mov DPL, a         ; DPL = param (param already in A)
 *   16ed: clr a
 *   16ee: addc a, #0x04      ; DPH = 0x04 + carry
 *   16f0: mov DPH, a
 *   16f2: ret
 *
 * Returns: XDATA pointer at 0x0400 + param
 */
__xdata uint8_t * helper_16eb(uint8_t param)
{
    return (__xdata uint8_t *)(0x0400 + param);
}

/*
 * helper_173b - DMA/queue pointer helper used by SCSI path
 * Address: 0x173b (entry point only)
 *
 * The original firmware tail-calls into a small routine that prepares
 * DPTR before issuing a DMA-related request. The exact side effects are
 * still unknown, but we stub it out so the firmware links and higher
 * level logic can continue to be reverse engineered.
 */
void helper_173b(void)
{
    /* TODO: Implement once behavior at 0x173b is understood. */
}

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
uint8_t scsi_read_ctrl_indexed(void)
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

/*
 * helper_1b2e - Calculate DPTR address 0x0108 + param
 * Address: 0x1b2e-0x1b37 (10 bytes)
 */
__xdata uint8_t * helper_1b2e(uint8_t param)
{
    return (__xdata uint8_t *)(0x0108 + param);
}

/*
 * helper_1b30 - Calculate DPTR address 0x0100 + param (mid-entry of 1b2e)
 * Address: 0x1b30-0x1b37 (8 bytes)
 */
__xdata uint8_t * helper_1b30(uint8_t param)
{
    return (__xdata uint8_t *)(0x0100 + param);
}

/*
 * helper_1c13 - Calculate DPTR address from param
 * Address: 0x1c13-0x1c1a (8 bytes)
 */
__xdata uint8_t * helper_1c13(uint8_t param)
{
    return (__xdata uint8_t *)(uint16_t)param;
}

/*
 * helper_1cf0 - DMA setup helper wrapper
 * Address: 0x1cf0-0x1cfb (12 bytes)
 *
 * Disassembly:
 *   1cf0: clr a           ; A = 0
 *   1cf1: mov r3, a       ; R3 = 0
 *   1cf2: mov r5, #0x20   ; R5 = 0x20
 *   1cf4: mov r7, #0x05   ; R7 = 0x05
 *   1cf6: lcall 0x523c    ; Call helper_523c(0, 0x20, 0x05)
 *   1cf9: mov r7, #0x05   ; Return value = 5
 *   1cfb: ret
 *
 * Calls helper_523c with r3=0, r5=0x20, r7=5 and returns 5.
 */
extern void helper_523c(uint8_t r3, uint8_t r5, uint8_t r7);

uint8_t helper_1cf0(void)
{
    helper_523c(0, 0x20, 5);
    return 5;
}

/* 0x9980: pcie_store_to_05b8 - Implemented in pcie.c:1459 */
/* 0x99bc: pcie_store_r7_to_05b7 - Implemented in pcie.c:1575 */

/*
 * FUN_CODE_1c9f - Check core state and return status
 * Address: 0x1c9f-0x1cad (15 bytes)
 *
 * Calls initialization functions, then returns nonzero if
 * either byte of I_CORE_STATE is nonzero.
 */
uint8_t core_state_check(void)
{
    /* Original calls 0x4ff2 and 0x4e6d for setup */
    return I_CORE_STATE_L | I_CORE_STATE_H;
}

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

/*
 * helper_3578 - Protocol dispatch helper
 * Address: 0x3578-0x360e (approximately 150+ bytes)
 *
 * From ghidra.c FUN_CODE_3578:
 *   - Stores param to G_LOG_PROCESSED_INDEX
 *   - Checks G_SYS_FLAGS_07EF
 *   - If zero: calls usb_get_sys_status_offset, protocol_dispatch, etc.
 *   - Complex state machine for USB/PCIe transfer handling
 */
extern uint8_t usb_get_sys_status_offset(void);
extern void protocol_dispatch(void);
extern void usb_set_done_flag(void);

void helper_3578(uint8_t param)
{
    uint8_t status;

    G_LOG_PROCESSED_INDEX = param;

    if (G_SYS_FLAGS_07EF == 0) {
        status = usb_get_sys_status_offset();
        G_PCIE_TXN_COUNT_LO = status;
        protocol_dispatch();

        /* Complex transfer handling - simplified */
        if (status != 0 && XDATA8(0x05A6) == 0x04) {
            /* Transfer complete path */
            XDATA8(0xB455) = 2;
            XDATA8(0xB455) = 4;
            XDATA8(0xB2D5) = 1;
            REG_PCIE_STATUS = 8;
        }
    }
}

/*
 * helper_1c5d - USB/Transfer table lookup helper
 * Address: 0x1c5d-0x1c6b (15 bytes)
 *
 * Reads a value from a table based on G_SYS_STATUS_PRIMARY (0x0464)
 * and stores result in G_PCIE_TXN_COUNT_LO (0x05a6).
 *
 * From ghidra.c FUN_CODE_1c5d:
 *   G_PCIE_TXN_COUNT_LO = table[0x05A8 + *param_1];
 *   where param_1 points to G_SYS_STATUS_PRIMARY (0x0464)
 */
void helper_1c5d(void)
{
    uint8_t idx = G_SYS_STATUS_PRIMARY;
    /* Table lookup at 0x05A8 + idx */
    G_PCIE_TXN_COUNT_LO = XDATA8(0x05a8 + idx);
}

/*
 * scsi_send_csw - Send SCSI Command Status Wrapper
 * Address: 0x4977-0x4a0b (approx 149 bytes)
 *
 * From ghidra.c FUN_CODE_4977:
 *   - Stores param1/param2 in G_FLASH_ERROR_0/1
 *   - Polls for completion based on status bits
 *   - Checks REG_CPU_LINK_CEF3 and B294 register for interrupts
 *   - Calls handler_3adb on completion or FUN_CODE_0395 on poll
 */
extern void handler_3adb(uint8_t param);

/*
 * handler_0395 - Polling/wait dispatch entry
 * Address: 0x0395 (dispatch entry), target: 0xDA8F
 *
 * From ghidra.c: jump_bank_0(0xda8f)
 * This is a wait/poll function called during CSW sending.
 */
void handler_0395(void)
{
    /* Dispatch to 0xDA8F - wait/poll function */
    /* For now, no-op since it's a poll loop */
}

void scsi_send_csw(uint8_t status, uint8_t param)
{
    uint8_t flags, regval;

    G_FLASH_ERROR_0 = param;
    G_FLASH_ERROR_1 = status;

    while (1) {
        flags = G_FLASH_ERROR_1;

        /* Check bit 1 for interrupt handling */
        if ((flags >> 1) & 1) {
            if (G_FLASH_ERROR_0 == 0) {
                regval = REG_CPU_LINK_CEF3;
                if ((regval >> 3) & 1) {
                    REG_CPU_LINK_CEF3 = 8;  /* Clear interrupt */
                    return;
                }
            } else {
                regval = XDATA8(0xB294);
                if ((regval >> 5) & 1) {
                    XDATA8(0xB294) = 0x20;
                    return;
                }
                if (XDATA8(0x05AD) != 0 && ((regval >> 4) & 1)) {
                    XDATA8(0xB294) = 0x10;
                    return;
                }
            }
        }

        /* Check bit 0 for completion */
        if (flags & 1) {
            if (G_FLASH_ERROR_0 == 0) {
                if ((int8_t)REG_CPU_LINK_CEF2 < 0) {
                    REG_CPU_LINK_CEF2 = 0x80;
                    handler_3adb(0);
                    return;
                }
            } else {
                regval = XDATA8(0xB294);
                if ((regval >> 4) & 1) {
                    XDATA8(0xB294) = 0x10;
                    handler_3adb(0);
                    return;
                }
            }
        }

        /* Polling call */
        handler_0395();

        if (flags != 0) {
            return;
        }
    }
}

/* Interface ready check */
void interface_ready_check(uint8_t p1, uint8_t p2, uint8_t p3) {
    (void)p1; (void)p2; (void)p3;
}

/*
 * protocol_compare_32bit - Compare 32-bit values and return carry
 *
 * Compares IDATA registers R4:R5:R6:R7 with R0:R1:R2:R3.
 * Returns 1 if R4567 < R0123 (carry set), 0 otherwise.
 */
uint8_t protocol_compare_32bit(void)
{
    volatile uint8_t *idata = (__idata uint8_t *)0x00;
    uint8_t r4 = idata[4], r5 = idata[5], r6 = idata[6], r7 = idata[7];
    uint8_t r0 = idata[0], r1 = idata[1], r2 = idata[2], r3 = idata[3];
    uint32_t val1 = ((uint32_t)r4 << 24) | ((uint32_t)r5 << 16) | ((uint32_t)r6 << 8) | r7;
    uint32_t val2 = ((uint32_t)r0 << 24) | ((uint32_t)r1 << 16) | ((uint32_t)r2 << 8) | r3;
    return (val1 < val2) ? 1 : 0;
}

/*
 * reg_poll - Register dispatch table polling
 */
void reg_poll(uint8_t param)
{
    (void)param;
}

/*===========================================================================
 * USB Descriptor Helpers
 *===========================================================================*/

/* usb_descriptor_helper_a637 - moved to usb.c */

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

/*
 * usb_parse_descriptor - DMA/buffer configuration for USB descriptor parsing
 * Address: Related to usb_parse_descriptor in ghidra.c
 *
 * Configures DMA and buffer registers based on parameters.
 * param1 bits control mode:
 *   - (param1 & 6) == 0: Use param2 for DMA config
 *   - else: Use fixed 0xa0 DMA config
 */
void usb_parse_descriptor(uint8_t param1, uint8_t param2)
{
    uint8_t val;

    if ((param1 & 0x06) == 0) {
        /* Mode 0: Use param2 for buffer configuration */
        XDATA_REG8(0xC4E9) = param2 | 0x80;
        val = REG_NVME_DMA_CTRL_ED;
        REG_NVME_DMA_CTRL_ED = (val & 0xC0) | param2;
        XDATA_REG8(0x905B) = REG_NVME_DMA_ADDR_LO;
        XDATA_REG8(0x905C) = REG_NVME_DMA_ADDR_HI;
    } else {
        /* Mode 1: Use fixed configuration */
        XDATA_REG8(0xC4E9) = 0xA0;
        val = XDATA8(0x0056);
        XDATA_REG8(0x905B) = val;
        XDATA_REG8(0x905B) = val;
        val = XDATA8(0x0057);
        XDATA_REG8(0x905C) = val;
        XDATA_REG8(0x905C) = val;
    }

    /* Clear buffer control registers */
    XDATA_REG8(0x905A) = 0;
    REG_USB_EP_BUF_LEN_LO = 0;
    REG_DMA_STATUS = 0;
    REG_USB_EP_BUF_LEN_HI = 0;
    REG_USB_EP_CTRL_0F = 0;

    /* Setup based on param1 bit 4 */
    if ((param1 & 0x10) == 0) {
        REG_DMA_CTRL = 0x03;
    }
}

/*
 * usb_get_xfer_status - Get USB transfer status
 *
 * Returns current transfer status from USB status register.
 */
uint8_t usb_get_xfer_status(void)
{
    return REG_USB_STATUS & 0x0F;
}

/*
 * usb_event_handler - Handle USB events
 * Address: Related to 0x4660 in ghidra.c
 *
 * Calls reg_wait_bit_set to wait for USB ready state.
 */
extern void reg_wait_bit_set(uint16_t addr);

uint8_t usb_event_handler(void)
{
    reg_wait_bit_set(0x07);
    return 0;
}

/*
 * parse_descriptor - Parse USB descriptor dispatch entry (0x04da)
 * Address: 0x04da -> dispatches to 0xe3b7
 *
 * Dispatch table entry that calls the descriptor parser.
 * Actual function at 0xe3b7 reads 0xcc17 and updates registers.
 */
void parse_descriptor(uint8_t param)
{
    uint8_t result;

    /* Read descriptor config and get result in param */
    result = XDATA8(0xCC17);

    /* Check bit 0 - if set, clear bit 0 of 0x92c4 */
    if (param & 0x01) {
        XDATA8(0x92C4) = XDATA8(0x92C4) & 0xFE;
    }

    /* Check bit 1 - if set, additional handling needed */
    if (param & 0x02) {
        /* Additional error/state handling */
    }

    (void)result;
}

/*
 * usb_state_setup_4c98 - USB state setup
 * Address: 0x4c98-0x4cdb (68 bytes)
 *
 * Sets up USB state for transfer operations.
 * From ghidra.c FUN_CODE_4c98:
 *   - Copies LUN from 0x0af4 to G_SYS_STATUS_SECONDARY (0x0465)
 *   - Calculates address into table at 0x047a+LUN -> G_SYS_STATUS_PRIMARY (0x0464)
 *   - Calls FUN_CODE_1c5d (helper function)
 *   - Sets/clears bit 0 of REG_NVME_QUEUE_CFG based on LUN
 *   - If USB not connected: clears various state
 */
extern void helper_1c5d(void);

void usb_state_setup_4c98(void)
{
    uint8_t lun;

    /* Copy LUN from 0x0af4 to secondary status */
    lun = XDATA8(0x0af4);
    G_SYS_STATUS_SECONDARY = lun;

    /* Store corresponding value to primary status
     * Original: reads from table at 0x047a + lun
     * For LUN 0-15, valid range is 0x047a to 0x0489 */
    G_SYS_STATUS_PRIMARY = XDATA8(0x047a + lun);

    /* Call helper function */
    helper_1c5d();

    /* Configure NVMe queue based on LUN */
    if (lun == 0) {
        REG_NVME_QUEUE_CFG &= ~NVME_QUEUE_CFG_MASK_LO;  /* Clear bits 0-1 */
    } else {
        REG_NVME_QUEUE_CFG = (REG_NVME_QUEUE_CFG & 0xFC) | 0x01;  /* Set bit 0 */
    }

    /* If USB not connected (bit 0 clear), reset state */
    if ((REG_USB_STATUS & USB_STATUS_ACTIVE) != 0x01) {
        XDATA8(0x0056) = 0;
        XDATA8(0x0057) = 0;
        XDATA8(0x0108) = 1;
    }
}

/*
 * usb_helper_51ef - USB helper (abort path)
 * Address: 0x51ef-0x5215 (39 bytes)
 *
 * Checks for "USBC" signature at 0x9119-0x911e.
 * If 0x911a != 0x1f or 0x9119 != 0x00, returns early.
 * If signature is "USBC" (at 0x911b-0x911e), continues processing.
 */
void usb_helper_51ef(void)
{
    /* Check header bytes */
    if (XDATA8(0x911a) != 0x1f || XDATA8(0x9119) != 0x00) {
        return;
    }

    /* Check for "USBC" signature at 0x911b */
    if (XDATA8(0x911b) != 'U') return;
    if (XDATA8(0x911c) != 'S') return;
    if (XDATA8(0x911d) != 'B') return;
    if (XDATA8(0x911e) != 'C') return;

    /* Signature valid - processing continues in caller */
}

/*
 * usb_helper_5112 - USB helper
 * Address: 0x5112-0x5156 (69 bytes)
 *
 * Called after setting transfer active flag. Copies status from USB buffer
 * to IDATA variables and extracts transfer parameters.
 *
 * From ghidra.c:
 *   usb_copy_status_to_buffer();
 *   DAT_INTMEM_6b = DAT_EXTMEM_9126;  // Residue byte 3
 *   DAT_INTMEM_6c = DAT_EXTMEM_9125;  // Residue byte 2
 *   DAT_INTMEM_6d = DAT_EXTMEM_9124;  // Residue byte 1
 *   DAT_INTMEM_6e = DAT_EXTMEM_9123;  // Residue byte 0
 *   DAT_EXTMEM_0af3 = DAT_EXTMEM_9127 & 0x80;  // Direction bit
 *   DAT_EXTMEM_0af4 = DAT_EXTMEM_9128 & 0x0f;  // LUN
 *   FUN_CODE_4d92();  // Continue with transfer setup
 */
extern void usb_copy_status_to_buffer(void);
extern void scsi_handle_init_4d92(void);

void usb_helper_5112(void)
{
    usb_copy_status_to_buffer();

    /* Copy residue bytes to IDATA 0x6b-0x6e (transfer length) */
    I_TRANSFER_6B = XDATA8(0x9126);
    I_TRANSFER_6C = XDATA8(0x9125);
    I_TRANSFER_6D = XDATA8(0x9124);
    I_TRANSFER_6E = XDATA8(0x9123);

    /* Extract direction bit (bit 7) */
    XDATA8(0x0af3) = XDATA8(0x9127) & 0x80;

    /* Extract LUN (bits 0-3) */
    XDATA8(0x0af4) = XDATA8(0x9128) & 0x0f;

    /* Continue with transfer setup */
    scsi_handle_init_4d92();
}

/* usb_set_transfer_active_flag - IMPLEMENTED in usb.c */

/* nvme_read_status - IMPLEMENTED in nvme.c */

/*
 * usb_read_transfer_params_hi/lo - Read transfer parameters
 * Address: Part of 0x31a5-0x31ac
 *
 * Returns high/low byte of transfer parameters from 0x0AFA/0x0AFB.
 */
uint8_t usb_read_transfer_params_hi(void) { return G_TRANSFER_PARAMS_HI; }
uint8_t usb_read_transfer_params_lo(void) { return G_TRANSFER_PARAMS_LO; }

/*===========================================================================
 * Handler Functions
 *===========================================================================*/

/*
 * Note: handler_0327 and handler_039a are dispatch table entries at 0x0300+
 * that load DPTR with a target address and jump to the common dispatcher.
 * These wrappers call the actual target functions directly.
 */

/*
 * handler_0327_usb_power_init - USB power initialization dispatch
 * Address: 0x0327 (dispatch entry), target: 0xB1CB (usb_power_init)
 *
 * From ghidra.c: jump_bank_0(usb_power_init)
 */
extern void usb_power_init(void);
void handler_0327_usb_power_init(void)
{
    usb_power_init();
}

/*
 * handler_039a_buffer_dispatch - USB buffer handler dispatch
 * Address: 0x039a (dispatch entry), target: 0xD810 (usb_buffer_handler)
 *
 * From ghidra.c: jump_bank_0(usb_buffer_handler)
 */
extern void usb_buffer_handler(void);
void handler_039a_buffer_dispatch(void)
{
    usb_buffer_handler();
}

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

/*
 * helper_e7c1 - State check and conditional call helper
 * Address: 0xe7c1-0xe7d3 (19 bytes)
 *
 * Disassembly:
 *   e7c1: mov a, r7            ; Get param
 *   e7c2: cjne a, #0x01, 0xe7c9 ; If param != 1, skip
 *   e7c5: lcall 0xbd14         ; Call helper_bd14
 *   e7c8: ret
 *   e7c9: mov dptr, #0x0af1    ; G_STATE_FLAG_0AF1
 *   e7cc: movx a, @dptr        ; Read flag
 *   e7cd: jnb 0xe0.4, 0xe7d3   ; If bit 4 clear, skip
 *   e7d0: lcall 0xbcf2         ; Call helper_bcf2
 *   e7d3: ret
 *
 * If param == 1: calls helper_bd14
 * Else: if G_STATE_FLAG_0AF1 bit 4 set, calls helper_bcf2
 */
void helper_e7c1(uint8_t param)
{
    if (param == 0x01) {
        /* Call helper_bd14 - state update function */
        /* TODO: implement helper_bd14 call */
        return;
    }

    /* Check G_STATE_FLAG_0AF1 bit 4 */
    if (G_STATE_FLAG_0AF1 & 0x10) {
        /* Call helper_bcf2 - state sync function */
        /* TODO: implement helper_bcf2 call */
    }
}

/*
 * helper_057a - Dispatch to event handler via address lookup
 * Address: 0x057a-0x057e (5 bytes)
 *
 * Disassembly:
 *   057a: mov dptr, #0xe0d9    ; Target handler address
 *   057d: ajmp 0x0311          ; Jump to dispatch table handler
 *
 * This is part of a dispatch table in the low memory area.
 * Sets DPTR to a handler address and jumps to the dispatcher.
 * The dispatcher at 0x0311 performs an indirect call to DPTR.
 *
 * The param selects which entry to dispatch (R7 is passed through).
 */
void helper_057a(uint8_t param)
{
    (void)param;

    /* This dispatches to the handler at 0xe0d9 */
    /* The actual dispatch mechanism uses DPTR and indirect jump */
    /* For C implementation, we would call the handler directly */
    /* TODO: implement handler_e0d9 call when available */
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
 *   e911: ljmp 0xbe8b        ; Tail call to pcie_link_state_init
 *
 * Triggers CPU interrupt then tail-calls pcie_link_state_init.
 */
extern void pcie_link_state_init(void);
void handler_e90b(void)
{
    REG_CPU_INT_CTRL = CPU_INT_CTRL_TRIGGER;
    pcie_link_state_init();
}

/*===========================================================================
 * NVMe Utility Functions
 *===========================================================================*/

/*
 * nvme_util_advance_queue - Advance NVMe command queue
 * Address: 0x49ae-0x4a0d (approx 96 bytes)
 *
 * From ghidra.c (line 9348):
 *   - Calls nvme_check_scsi_ctrl()
 *   - Iterates through command slots
 *   - Checks G_USB_INDEX_COUNTER against slot index
 *   - Updates G_SCSI_CTRL and calls nvme_util_get_queue_depth
 */
extern void nvme_check_scsi_ctrl(void);
extern void nvme_calc_addr_04b7(uint8_t param);
extern void nvme_util_get_queue_depth(uint8_t p1, uint8_t p2);

void nvme_util_advance_queue(void)
{
    uint8_t i, limit;

    nvme_check_scsi_ctrl();

    limit = XDATA8(0x053b);  /* Command count */
    for (i = 0; i <= limit; i++) {
        nvme_calc_addr_04b7(i - (limit + 1));
        if (G_USB_INDEX_COUNTER == XDATA8(0x053b)) {
            nvme_calc_addr_04b7(0);
            G_USB_INDEX_COUNTER = 0xff;
            G_SCSI_CTRL--;
            if (XDATA8(0x0b01) != 0) {
                /* FUN_CODE_4eb3() - error recovery */
                return;
            }
            nvme_util_get_queue_depth(1, i);
            return;
        }
    }
}

/*
 * nvme_util_check_command_ready - Check if NVMe command is ready
 * Address: 0x3e22-0x3ebf (approx 158 bytes)
 *
 * From ghidra.c (line 7722):
 *   - Reads REG_NVME_QUEUE_PENDING (0xC516)
 *   - Waits for queue to be ready
 *   - Checks REG_CPU_LINK_CEF3 for interrupts
 *   - Calls handler_2608 on interrupt
 */
extern void handler_2608(void);
extern void dma_setup_transfer(uint8_t p1, uint8_t p2, uint8_t p3);

void nvme_util_check_command_ready(void)
{
    uint8_t pending, status;

    pending = REG_NVME_QUEUE_PENDING & 0x3F;
    I_WORK_38 = pending;

    /* Increment and store back */
    I_WORK_39 = XDATA8(0xC516) + 1;
    XDATA8(0xC516) = I_WORK_39;

    /* Wait for queue ready with interrupt check */
    while (1) {
        status = REG_CPU_LINK_CEF3;
        if ((status >> 3) & 1) {
            REG_CPU_LINK_CEF3 = 8;  /* Clear interrupt */
            handler_2608();
        }

        /* Check completion */
        if ((REG_NVME_LINK_STATUS >> 1) & 1) {
            break;  /* Ready */
        }
    }
}

/*
 * nvme_util_clear_completion - Clear NVMe completion status
 * Address: 0x4850-0x48c7 (approx 120 bytes)
 *
 * From ghidra.c (line 8756):
 *   - Sets G_STATE_FLAG_06E6 = 1
 *   - Loops checking REG_NVME_LINK_STATUS
 *   - Clears primary/secondary status
 *   - Processes queue entries
 */
extern void usb_get_descriptor_ptr(void);

uint8_t nvme_util_clear_completion(void)
{
    uint8_t i, status, pending;

    G_STATE_FLAG_06E6 = 1;

    for (i = 0; i < 0x20; i++) {
        status = REG_NVME_LINK_STATUS;
        if (((status >> 1) & 1) == 0) {
            return status;
        }

        G_SYS_STATUS_PRIMARY = 0;
        G_SYS_STATUS_SECONDARY = 0;

        /* Process based on transfer flag */
        pending = REG_NVME_QUEUE_STATUS & 0x3F;
        I_WORK_38 = pending;

        usb_get_descriptor_ptr();

        REG_NVME_QUEUE_TRIGGER = 0xFF;
    }

    return 0;
}

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

    /* If bit 0 set: clear bit 0 of REG_POWER_MISC_CTRL */
    if (param & 0x01) {
        REG_POWER_MISC_CTRL = REG_POWER_MISC_CTRL & 0xFE;
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

/*
 * pcie_bank1_helper_e902 - PCIe write direction setup and transfer
 * Address: 0xe902-0xe90a (9 bytes)
 *
 * Sets G_PCIE_DIRECTION to 1 (write) and calls pcie_tlp_init_and_transfer.
 *
 * Original disassembly:
 *   e902: mov dptr, #0x05ae    ; G_PCIE_DIRECTION
 *   e905: mov a, #0x01
 *   e907: movx @dptr, a        ; set direction = write
 *   e908: ljmp 0xc1f9          ; tail call to pcie_tlp_init_and_transfer
 */
extern uint8_t pcie_tlp_init_and_transfer(void);

void pcie_bank1_helper_e902(void)
{
    G_PCIE_DIRECTION = 1;  /* Set write direction */
    pcie_tlp_init_and_transfer();
}

/*
 * startup_init - Startup initialization
 * Address: 0x50db-0x50fe (approx 36 bytes)
 *
 * From ghidra.c (line 9597):
 *   - Checks G_EP_DISPATCH_OFFSET < 0x20
 *   - Calls usb_get_descriptor_length, usb_convert_speed, nvme_build_cmd
 *   - Performs initialization loop
 */
extern void usb_get_descriptor_length(uint8_t param);
extern void usb_convert_speed(uint8_t param);
extern uint8_t nvme_build_cmd(uint8_t param);

void startup_init(void)
{
    uint8_t offset;

    offset = G_EP_DISPATCH_OFFSET;
    if (offset < 0x20) {
        /* Clear dispatch offset temporarily */
        G_EP_DISPATCH_OFFSET = 0;

        /* Get descriptor length with offset + 0x0C */
        usb_get_descriptor_length(offset + 0x0C);

        /* Convert speed with offset + 0x2F */
        usb_convert_speed(offset + 0x2F);

        /* Build NVMe command */
        nvme_build_cmd(0);

        /* Restore and finalize */
        usb_convert_speed(G_EP_DISPATCH_OFFSET + 0x2F);
    }
}

/*
 * sys_event_dispatch_05e8 - System event dispatcher
 * Address: 0x05e8-0x05ec (5 bytes)
 *
 * From ghidra.c (line 2108):
 *   jump_bank_1(&LAB_CODE_9d90)
 *
 * This is a bank switch dispatch to 0x9D90 in bank 1.
 */
extern void handler_9d90(void);

void sys_event_dispatch_05e8(void)
{
    /* Dispatch to event handler at 0x9D90 in bank 1 */
    handler_9d90();
}

/*
 * sys_init_helper_bbc7 - System init helper
 * Address: 0xbbc7-0xbbc9 (3 bytes)
 *
 * From ghidra.c (line 16491):
 *   write_xdata_reg(0, 0x12, 0xb, 1)
 *
 * Writes to XDATA register with specific parameters.
 * WARNING: Ghidra shows this as infinite loop / no return.
 */
void sys_init_helper_bbc7(void)
{
    /* Write configuration to register area */
    /* Parameters: 0, 0x12, 0x0b, 1 suggest:
     * - Base offset 0
     * - Value 0x12
     * - Register/mode 0x0b
     * - Count/flag 1 */
    XDATA8(0x0B12) = 0x01;  /* Simplified write */
}

/*
 * sys_timer_handler_e957 - System timer/watchdog handler
 * Address: 0xe957-0xe95e (8 bytes)
 *
 * From ghidra.c (line 21395):
 *   FUN_CODE_db09()
 *
 * Calls the DB09 handler for timer/watchdog processing.
 */
extern void handler_db09(void);

void sys_timer_handler_e957(void)
{
    handler_db09();
}

/* Helper stubs for system functions */

/*
 * usb_get_descriptor_length - Get USB descriptor length
 *
 * Gets the length field from a USB descriptor at the given offset.
 */
void usb_get_descriptor_length(uint8_t param)
{
    uint8_t len = XDATA8(0x9E00 + param);
    (void)len;
}

/*
 * usb_convert_speed - Convert USB speed mode
 *
 * Converts USB speed mode value at the given offset.
 */
void usb_convert_speed(uint8_t param)
{
    uint8_t speed = XDATA8(0x9E00 + param);
    (void)speed;
}

void handler_9d90(void) {}

/*
 * handler_db09 - Flash read trigger handler
 * Address: 0xdb09-0xdb44 (60 bytes)
 *
 * Reads G_FLASH_READ_TRIGGER and dispatches based on state:
 *   state 0: I_FLASH_STATE_4D = 0, continue
 *   state 1: I_FLASH_STATE_4D = 0x80, continue
 *   other: return immediately
 *
 * Then stores state to XDATA work area and calls DMA handler.
 *
 * Original disassembly:
 *   db09: mov dptr, #0x0213    ; G_FLASH_READ_TRIGGER
 *   db0c: movx a, @dptr
 *   db0d: mov r6, a
 *   db0e: jnz db14             ; if state != 0
 *   db10: mov 0x4d, a          ; I_FLASH_STATE_4D = 0
 *   db12: sjmp db20
 *   db14: cjne a, #0x01, db1d  ; if state != 1
 *   db18: mov 0x4d, #0x80      ; I_FLASH_STATE_4D = 0x80
 *   db1b: sjmp db20
 *   db1d: mov r7, #0x00        ; state > 1: return 0
 *   db1f: ret
 *   db20: ... continue with store and DMA
 *   db35: lcall 0xb838         ; store to 0x0AAD area
 *   db38: mov a, #0x80
 *   db3a: movx @dptr, a        ; write 0x80 to 0x0AB2
 *   db3b: mov r5, #0x03
 *   db3d: mov r7, #0x03
 *   db3f: lcall 0xbe02         ; dispatch_04c1
 *   db42: mov r7, #0x01
 *   db44: ret
 */
extern void dispatch_04c1(void);

void handler_db09(void)
{
    uint8_t state;

    /* Read flash read trigger state */
    state = G_FLASH_READ_TRIGGER;

    /* Set I_FLASH_STATE_4D based on state */
    if (state == 0) {
        I_FLASH_STATE_4D = 0;
    } else if (state == 1) {
        I_FLASH_STATE_4D = 0x80;
    } else {
        /* State > 1: early return */
        return;
    }

    /* Store state to XDATA work area (0x0AAD-0x0AB0) */
    /* Original stores R4:R5:R6:R7 = 0:0:0:I_FLASH_STATE_4D */
    XDATA8(0x0AAD) = I_FLASH_STATE_4D;
    XDATA8(0x0AAE) = 0;
    XDATA8(0x0AAF) = 0;
    XDATA8(0x0AB0) = 0;

    /* Clear 0x0AB1 */
    XDATA8(0x0AB1) = 0;

    /* Write 0x80 to 0x0AB2 */
    XDATA8(0x0AB2) = 0x80;

    /* Call DMA handler with R5=3, R7=3 */
    dispatch_04c1();
}

/*
 * usb_get_descriptor_ptr - Get USB descriptor pointer
 * Note: This is a simplified no-op. The actual logic is handled
 * by nvme_calc_dptr_0100_base (0x31d8) which is already implemented.
 */
void usb_get_descriptor_ptr(void) {}

/*
 * nvme_util_get_queue_depth - Process NVMe queue and update status
 * Address: ~0x46xx (approximate)
 *
 * This function coordinates queue status updates based on the mode flag
 * and queue index parameters.
 *
 * Parameters:
 *   p1 - Mode flag (0 or 1)
 *   p2 - Queue index
 *
 * From ghidra.c analysis:
 *   - Stores params to G_EP_DISPATCH_VAL3/VAL4
 *   - Calls helper functions for queue management
 *   - Sets EP queue control and status
 *   - Updates REG_NVME_DATA_CTRL
 *   - Handles SCSI control state
 */
extern uint16_t helper_1b77(void);
extern uint8_t helper_1c1b(void);
extern uint16_t usb_read_buf_addr_pair(void);
extern void nvme_subtract_idata_16(uint8_t hi, uint8_t lo);
extern void nvme_set_ep_queue_ctrl_84(void);
extern void helper_1aad(uint8_t param);
extern void usb_init_pcie_txn_state(void);
extern void helper_1b47(void);
extern uint8_t nvme_get_dev_status_upper(void);
extern uint8_t nvme_get_cmd_param_upper(void);
extern void usb_add_masked_counter(uint8_t param);
extern void nvme_inc_circular_counter(uint8_t param);
extern void power_check_status(uint8_t param);

void nvme_util_get_queue_depth(uint8_t p1, uint8_t p2)
{
    uint8_t param_for_aad;
    uint8_t scsi_ctrl;
    uint8_t temp;

    /* Store parameters */
    G_EP_DISPATCH_VAL3 = p2;   /* param_3 in ghidra */
    G_EP_DISPATCH_VAL4 = p1;   /* param_2 in ghidra */

    /* Call helper functions */
    helper_1b77();

    /* Check helper_1c1b result */
    if (helper_1c1b() == 0) {
        /* If helper returns 0, do buffer address pair processing */
        usb_read_buf_addr_pair();
        nvme_subtract_idata_16(0, 0);  /* Simplified - actual uses R6/R7 */
    }

    /* Set EP queue control */
    nvme_set_ep_queue_ctrl_84();

    /* Update queue status */
    G_EP_QUEUE_STATUS = G_EP_DISPATCH_VAL3;

    /* Select param for helper_1aad based on mode */
    param_for_aad = (G_EP_DISPATCH_VAL4 == 1) ? 1 : 2;
    helper_1aad(param_for_aad);

    /* Initialize PCIe transaction state */
    usb_init_pcie_txn_state();

    /* Clear high bit of NVME data control */
    REG_NVME_DATA_CTRL &= ~NVME_DATA_CTRL_BIT7;

    /* Handle SCSI control state */
    scsi_ctrl = G_SCSI_CTRL;
    if (scsi_ctrl == 0) {
        /* Call helper_1b47 */
        helper_1b47();
    } else {
        /* Update G_EP_DISPATCH_VAL3 with device status */
        temp = G_EP_DISPATCH_VAL3;
        temp = temp + G_DMA_WORK_0216;
        temp |= nvme_get_dev_status_upper();
        G_EP_DISPATCH_VAL3 = temp;
    }

    /* Update with command param upper bits */
    G_EP_DISPATCH_VAL3 |= nvme_get_cmd_param_upper();

    /* Handle queue increment based on SCSI control */
    if (scsi_ctrl != 0) {
        usb_add_masked_counter(G_DMA_WORK_0216);
        temp = scsi_ctrl - 1;  /* Simplified from FUN_CODE_4f37 */
    } else {
        temp = scsi_ctrl - 1;
    }
    nvme_inc_circular_counter(temp);

    /* Power check if mode flag is 0 */
    if (G_EP_DISPATCH_VAL4 == 0) {
        power_check_status(G_USB_PARAM_0B00);
    }
}

/* handler_2608 - moved to dma.c */

/* pcie_lane_config_helper - moved to pcie.c */

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

/* uart_read_byte_dace - moved to uart.c */
/* uart_write_byte_daeb - moved to uart.c */
/* uart_write_daff - moved to uart.c */

/*===========================================================================
 * Code Functions (FUN_CODE_xxxx)
 *===========================================================================*/

/*
 * Note: FUN_CODE_050c and FUN_CODE_0511 are dispatch table entries,
 * not standalone functions. They load DPTR with target addresses.
 */
extern void pcie_trigger_cc11_e8ef(void);
extern void helper_e50d_full(uint8_t div_bits, uint8_t threshold_hi, uint8_t threshold_lo);

/*
 * pcie_trigger_trampoline - Trampoline to pcie_trigger_cc11_e8ef (0xe8ef)
 * Address: 0x050c
 * Original: mov dptr, #0xe8ef; ajmp 0x0300
 */
void pcie_trigger_trampoline(uint8_t param)
{
    (void)param;
    pcie_trigger_cc11_e8ef();
}

/*
 * timer_config_trampoline - Trampoline to helper_e50d (0xe50d)
 * Address: 0x0511
 * Original: mov dptr, #0xe50d; ajmp 0x0300
 * Params: p1→threshold_hi, p2→threshold_lo, p3→div_bits
 */
void timer_config_trampoline(uint8_t p1, uint8_t p2, uint8_t p3)
{
    helper_e50d_full(p3, p1, p2);
}

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
 * scsi_dma_transfer_process - SCSI/DMA transfer state machine
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
uint8_t scsi_dma_transfer_process(uint8_t param)
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
 * addr_calc_high_borrow - Calculate high byte with borrow check
 * Address: 0x5038-0x5042 (11 bytes)
 *
 * From ghidra.c: return '\x05' - (((0xe8 < param_1) << 7) >> 7)
 * Returns 0x05 if param <= 0xe8, otherwise 0x04 (borrow occurred)
 * Used for address calculation high byte.
 */
uint8_t addr_calc_high_borrow(uint8_t param)
{
    /* Returns 0x05 or 0x04 based on whether param > 0xE8 */
    if (param > 0xE8) {
        return 0x04;  /* Borrow occurred */
    }
    return 0x05;
}

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
uint8_t buf_read_offset_08(uint8_t param)
{
    uint16_t addr = 0x0108 + param;
    return XDATA_REG8(addr);
}

/*
 * buf_read_base - Alternate entry into buf_read_offset_08 (at mov DPL instruction)
 * Address: 0x5046-0x504e (9 bytes)
 *
 * From ghidra.c: return *(undefined1 *)CONCAT11('\x01' - (in_PSW >> 7), param_1)
 * Reads from address (0x01xx or 0x00xx based on carry) + param
 */
uint8_t buf_read_base(uint8_t param)
{
    /* Read from 0x0100 + param (assuming no carry from prior add) */
    return XDATA8(0x0100 + param);
}

/*
 * queue_buf_addr_high - Calculate queue buffer address high byte
 * Address: 0x504f-0x505c (14 bytes)
 *
 * From ghidra.c: bVar1 = DAT_EXTMEM_0a84; return -(((0xf3 < bVar1) << 7) >> 7)
 * Returns 0x00 if XDATA[0x0A84] <= 0xF3, otherwise 0xFF (borrow indicator)
 */
uint8_t queue_buf_addr_high(void)
{
    uint8_t idx = XDATA8(0x0A84);
    /* Returns high byte adjustment based on index value */
    if (idx > 0xF3) {
        return 0xFF;  /* Borrow occurred */
    }
    return 0x00;
}

/*
 * buf_read_offset_3e - Read from calculated address (param - 0x3E)
 * Address: 0x505d-0x5066 (10 bytes)
 *
 * From ghidra.c: return *(undefined1 *)CONCAT11(-(((0x3d < param_1) << 7) >> 7), param_1 - 0x3e)
 * Reads from address calculated as (high_byte, param - 0x3E)
 * High byte is 0xFF if param <= 0x3D (borrow), else 0x00
 */
uint8_t buf_read_offset_3e(uint8_t param)
{
    uint16_t addr;
    if (param <= 0x3D) {
        addr = 0xFF00 + (uint8_t)(param - 0x3E);  /* Borrow case */
    } else {
        addr = (uint8_t)(param - 0x3E);  /* Normal case */
    }
    return XDATA8(addr);
}

/*
 * nvme_queue_state_update - NVMe queue state management
 * Address: 0x5359-0x5372 (26 bytes)
 *
 * From ghidra.c:
 *   cVar1 = G_SYS_STATUS_PRIMARY;
 *   FUN_CODE_16e9(cVar1);
 *   DAT_INTMEM_51 = *pbVar3;
 *   bVar2 = DAT_INTMEM_51 + param_1 & 0x1f;
 *   FUN_CODE_16eb(cVar1 + 'V');
 *   *pbVar3 = bVar2;
 */
extern __xdata uint8_t * helper_16e9(uint8_t param);
extern __xdata uint8_t * helper_16eb(uint8_t param);

void nvme_queue_state_update(uint8_t param)
{
    uint8_t status;
    uint8_t new_val;

    status = G_SYS_STATUS_PRIMARY;
    helper_16e9(status);

    I_WORK_51 = G_SYS_STATUS_PRIMARY;
    new_val = (I_WORK_51 + param) & 0x1F;

    helper_16eb(status + 0x56);  /* 'V' = 0x56 */
    G_SYS_STATUS_PRIMARY = new_val;
}

/* Forward declarations for pcie_link_state_init helpers */
extern void uart_puthex(uint8_t val);
extern void uart_puts(const char __code *str);
extern uint8_t cmd_check_busy(void);
extern void cmd_start_trigger(void);
extern void cmd_config_e40b(void);
extern void cmd_engine_clear(void);

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
 * pcie_link_state_init - PCIe link status check with state machine
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
void pcie_link_state_init(void)
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

    /* Call cmd_engine_clear */
    cmd_engine_clear();

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
    while (REG_CMD_BUSY_STATUS & CMD_BUSY_STATUS_BUSY) {
        /* Spin */
    }

    /* Set PCIe complete flag */
    G_PCIE_COMPLETE_07DF = 1;
}

extern void cmd_config_e405_e421(uint8_t param);

/*
 * cmd_trigger_default - Command trigger entry point
 * Address: 0xdd0e-0xdd11 (4 bytes)
 *
 * Sets up parameters R5=1, R7=0x0F and falls through to dd12.
 */
void cmd_trigger_default(void)
{
    cmd_trigger_params(0x0F, 0x01);  /* R7=0x0F, R5=0x01 */
}

/*
 * cmd_trigger_params - Command trigger and mode setup
 * Address: 0xdd12-0xdd41 (48 bytes)
 *
 * Sets initial trigger value based on G_CMD_MODE, configures E405/E421,
 * then combines state and clears/sets trigger bits.
 *
 * Parameters:
 *   p1 (R7): Trigger bits to OR into final REG_CMD_TRIGGER value
 *   p2 (R5): Parameter passed to cmd_config_e405_e421 for E421 setup
 */
void cmd_trigger_params(uint8_t p1, uint8_t p2)
{
    uint8_t mode;
    uint8_t e421_val;
    uint8_t state_shifted;
    uint8_t trigger_val;

    /* Read command mode */
    mode = G_CMD_MODE;

    /* Set initial trigger value based on mode */
    if (mode == 0x02 || mode == 0x03) {
        REG_CMD_TRIGGER = 0x80;
    } else {
        REG_CMD_TRIGGER = 0x40;
    }

    /* Configure E405 and E421 (clears E405 bits 0-2, writes shifted p2 to E421) */
    cmd_config_e405_e421(p2);

    /* Read E421 value and compute state * 2 */
    e421_val = REG_CMD_MODE_E421;
    state_shifted = G_CMD_STATE << 1;

    /* Write combined value to E421 */
    REG_CMD_MODE_E421 = e421_val | state_shifted;

    /* Clear trigger bits 0-5, keep bits 6-7 */
    trigger_val = REG_CMD_TRIGGER;
    trigger_val &= 0xC0;
    REG_CMD_TRIGGER = trigger_val;

    /* OR in the p1 bits and write final value */
    trigger_val = REG_CMD_TRIGGER;
    trigger_val |= p1;
    REG_CMD_TRIGGER = trigger_val;
}

/*
 * protocol_state_dispatch - Protocol state dispatcher
 * Address: 0xdf79-0xdfaa (50 bytes)
 *
 * Reads G_STATE_0B1B -> G_LANE_STATE_0A9D, calls pcie_disable_and_trigger_e74e,
 * then dispatches based on state value:
 *   state 1: jump to dispatch_062e (Bank1:0xE374)
 *   state 2: if G_CMD_DEBUG_FF != 0x69, jump to dispatch_059d (Bank1:0xE545)
 *   state 3: call dispatch_055c (0xE968)
 *
 * Original disassembly:
 *   df79: mov dptr, #0x0b1b    ; G_STATE_0B1B
 *   df7c: movx a, @dptr
 *   df7d: mov dptr, #0x0a9d    ; G_LANE_STATE_0A9D
 *   df80: movx @dptr, a
 *   df81: lcall 0xe74e         ; pcie_disable_and_trigger_e74e()
 *   df84: mov dptr, #0x0a9d
 *   df87: movx a, @dptr
 *   df88: cjne a, #0x01, df8e  ; if state != 1
 *   df8b: ljmp 0xe374          ; dispatch_062e target
 *   df8e: cjne a, #0x02, dfa0  ; if state != 2
 *   df95: mov dptr, #0x07ff    ; G_CMD_DEBUG_FF
 *   df98: movx a, @dptr
 *   df99: xrl a, #0x69         ; if == 0x69
 *   df9b: jz dfaa              ; return
 *   df9d: ljmp 0xe545          ; dispatch_059d target
 *   dfa0: cjne a, #0x03, dfaa  ; if state != 3
 *   dfa7: lcall 0x055c         ; dispatch_055c()
 *   dfaa: ret
 */
extern void pcie_disable_and_trigger_e74e(void);
extern void dispatch_055c(void);
extern void dispatch_059d(void);
extern void dispatch_062e(void);

void protocol_state_dispatch(void)
{
    uint8_t state;

    /* Copy state from 0x0B1B to 0x0A9D */
    G_LANE_STATE_0A9D = G_STATE_0B1B;

    /* Call PCIe disable and trigger */
    pcie_disable_and_trigger_e74e();

    /* Read the state back and dispatch */
    state = G_LANE_STATE_0A9D;

    if (state == 0x01) {
        /* State 1: jump to Bank1 handler at 0xE374 */
        dispatch_062e();
        return;
    }

    if (state == 0x02) {
        /* State 2: check debug marker, possibly jump to Bank1 handler */
        if (G_CMD_DEBUG_FF != 0x69) {
            dispatch_059d();
        }
        return;
    }

    if (state == 0x03) {
        /* State 3: call handler */
        dispatch_055c();
    }
    /* Otherwise just return */
}

/*
 * cmd_param_setup - Command parameter setup
 * Address: 0xe120-0xe14a (43 bytes)
 *
 * Configures command registers E422-E425 based on parameters and mode.
 *
 * Parameters:
 *   p1 (r7): Bits to OR into REG_CMD_PARAM (bits 0-3)
 *   p2 (r5): Parameter bits (bits 0-1 go to bits 6-7 of REG_CMD_PARAM)
 *
 * Writes computed value to REG_CMD_PARAM (0xE422).
 * Sets REG_CMD_STATUS to 0x80 if mode==1, else 0xA8.
 * Initializes REG_CMD_ISSUE=0, REG_CMD_TAG=0xFF.
 */
void cmd_param_setup(uint8_t p1, uint8_t p2)
{
    uint8_t val;

    /* Compute parameter value:
     * - Bits 0-1 of p2 go to bits 6-7
     * - OR with p1 for bits 0-3
     * - Clear bits 4-5
     */
    val = ((p2 & 0x03) << 6) | (p1 & 0x0F);
    val &= 0xCF;  /* Clear bits 4-5 */
    REG_CMD_PARAM = val;

    /* Set status based on command mode */
    if (G_CMD_MODE == 0x01) {
        REG_CMD_STATUS = 0x80;
    } else {
        REG_CMD_STATUS = 0xA8;
    }

    /* Clear issue, set tag to 0xFF */
    REG_CMD_ISSUE = 0x00;
    REG_CMD_TAG = 0xFF;
}

/*
 * wait_status_loop - Wait loop with status check
 * Address: 0xe1c6-0xe1ed (40 bytes)
 *
 * This function is fully implemented as cmd_wait_completion() in cmd.c.
 * This stub provides the wait_status_loop name for callers (e.g., nvme.c).
 */
extern uint8_t cmd_wait_completion(void);
void wait_status_loop(void)
{
    cmd_wait_completion();
}

/*
 * cmd_engine_clear - Clear command engine registers 0xE420-0xE43F
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
void cmd_engine_clear(void)
{
    uint8_t i;
    volatile uint8_t __xdata *ptr = &REG_CMD_TRIGGER;

    /* Clear 32 bytes of command register block at 0xE420-0xE43F */
    for (i = 0; i < 0x20; i++) {
        ptr[i] = 0;
    }
}

/*
 * uart_wait_tx_ready - Wait for UART transmit buffer ready
 * Address: 0xe7ae-0xe7c0 (19 bytes)
 *
 * Two polling loops:
 * 1. Wait until (REG_UART_TFBF & 0x1F) == 0x10 (FIFO level)
 * 2. Wait until (REG_UART_STATUS & 0x07) == 0 (not busy)
 */
void uart_wait_tx_ready(void)
{
    /* Wait until UART FIFO level reaches 0x10 */
    while ((REG_UART_TFBF & 0x1F) != 0x10)
        ;

    /* Wait until UART not busy */
    while ((REG_UART_STATUS & 0x07) != 0)
        ;
}

/*
 * timer_event_init - Timer/event initialization handler
 * Address: 0xe883-0xe88d (11 bytes)
 *
 * Calls helper_e73a, then calls 0x95e1 with r7=0x10, r5=0,
 * then jumps to cmd_wait_completion.
 */
void timer_event_init(void)
{
    /* Clear command registers */
    cmd_engine_clear();

    /* Call config function with r7=0x10, r5=0 */
    helper_95e1(0x10, 0);

    /* Wait for completion */
    cmd_wait_completion();
}

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

/* pcie_txn_setup_e775 - moved to pcie.c */

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

/* pcie_handler_e890 - moved to pcie.c */

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
 * dispatch_handler_0557 - PCIe event dispatch handler
 * Address: 0x0557 -> targets 0xee94 (bank 1)
 *
 * Original disassembly at 0x16e94:
 *   ee94: acall 0xe97f   ; calls helper at 0x1697f
 *   ee96: rr a           ; rotate result right
 *   ee97: ljmp 0xed82    ; -> 0x16d82 -> ljmp 0x7a12 (NOP slide to 0x8000)
 *
 * The helper at 0x1697f:
 *   e97f: mov r1, #0xe6  ; setup parameter
 *   e981: ljmp 0x538d    ; call bank 0 dispatch function
 *
 * This function is part of the PCIe event handling chain. It checks event
 * state and returns non-zero (in R7) if dispatch/processing should continue.
 *
 * The caller in pcie.c uses the return value:
 *   if (result) { pcie_queue_handler_a62d(); ... }
 *
 * Returns: Non-zero if event processing should continue, 0 otherwise.
 */
uint8_t dispatch_handler_0557(void)
{
    /* Check event flags to determine if dispatch is needed.
     * The original function calls into bank 0/1 dispatch logic
     * that reads from event control registers.
     *
     * For now, return 0 (no dispatch) as a safe default.
     * A more complete implementation would check:
     * - G_EVENT_CTRL_09FA state
     * - PCIe link status
     * - Pending transfer state
     */
    return 0;  /* No dispatch needed - conservative default */
}

/*
 * pcie_write_reg_0633 - Power state check and conditional restart
 * Address: 0x0633 -> targets 0xc8c7
 *
 * Checks power state via power_check_state_dde2().
 * If state == 2, triggers a restart by jumping to main entry (0x8000).
 * Otherwise returns (with 0xFF, but return value not used).
 *
 * Original disassembly at 0xc8c7:
 *   c8c7: lcall 0xdde2    ; power_check_state_dde2()
 *   c8ca: mov a, r7       ; result in R7
 *   c8cb: xrl a, #0x02    ; test if result == 2
 *   c8cd: jz 0xc8d2       ; if result == 2, jump
 *   c8cf: mov r7, #0xff   ; return 0xff
 *   c8d1: ret
 *   c8d2: ljmp 0x8000     ; jump to main entry (restart)
 *
 * Note: The ljmp 0x8000 is a soft restart. In our C implementation,
 * we cannot easily replicate this behavior. The function just returns
 * and the caller continues. This may need revisiting if restart
 * behavior is critical.
 */
void pcie_write_reg_0633(void)
{
    extern uint8_t power_check_state_dde2(void);

    uint8_t state = power_check_state_dde2();
    if (state == 2) {
        /* Original firmware jumps to 0x8000 (restart).
         * In C, we cannot easily restart. For now, just return.
         * The caller in pcie_interrupt_handler doesn't check return value.
         */
        return;
    }
    /* state != 2: original returns 0xFF but value not checked */
}

/*
 * pcie_write_reg_0638 - PCIe register write and state clear
 * Address: 0x0638 -> targets 0xeadb (bank 1) -> ajmp 0x6cff (0x16cff)
 *
 * Original disassembly at 0x16cff:
 *   ecff: lcall 0x0be6    ; banked_store_byte (R1/A from caller)
 *   ed02: lcall 0x05c5    ; dispatch_05c5 -> handler at 0xe7fb
 *   ed05: clr a
 *   ed06: mov dptr, #0x023f
 *   ed09: movx @dptr, a   ; clear G_BANK1_STATE_023F
 *   ed0a: ret
 *
 * Called from pcie.c after writing 0x80 to source 0x8F via banked_store_byte.
 * This function continues processing by calling dispatch_05c5 and clearing
 * the bank 1 state flag.
 *
 * Note: The first lcall to banked_store_byte uses register context from caller.
 * In C, we skip that call as the caller already did the register write.
 */
void pcie_write_reg_0638(void)
{
    extern void dispatch_05c5(void);

    /* Call dispatch_05c5 -> 0xe7fb handler */
    dispatch_05c5();

    /* Clear bank 1 state flag */
    G_BANK1_STATE_023F = 0;
}

/*
 * pcie_cleanup_05f7 - PCIe status cleanup with computed index
 * Address: 0x05f7 -> targets 0xcbcc (bank 1)
 *
 * Original disassembly at 0x14bcc:
 *   cbcc: xch a, r0      ; exchange A with R0
 *   cbcd: mov r6, a      ; R6 = original R0
 *   cbce: add a, ACC     ; A = A * 2 (double)
 *   cbd0: anl a, #0x1e   ; mask to get valid index
 *   cbd2: add a, r7      ; add status value
 *   cbd3: mov dptr, #0x0442
 *   cbd6: movx @dptr, a  ; write computed index to 0x0442
 *   cbd7: ljmp 0xd7d9    ; continue to cleanup handler
 *
 * The continuation at 0xd7d9:
 *   d7d9: mov a, #0xff
 *   d7db: movx @dptr, a  ; write 0xFF to 0x0443
 *   d7dc: mov dptr, #0x0b2f
 *   d7df: movx a, @dptr  ; read status
 *   ... (additional status checks)
 *
 * This function is called when PCIe status bit 0 is set.
 * It computes an index from input parameters and writes to state buffer,
 * then continues with cleanup/status update logic.
 *
 * Since the original uses R0/R7 from caller context (which we don't have
 * in C), we implement a simplified version that does the essential cleanup.
 */
void pcie_cleanup_05f7(void)
{
    /* The original function computes an index and writes to 0x0442,
     * then continues to the cleanup handler at 0xd7d9.
     *
     * For C implementation, we simulate the essential behavior:
     * - Write to state buffer at 0x0442/0x0443
     * - The ljmp 0xd7d9 continues cleanup which we inline here
     */

    /* Write 0xFF to the second byte (as done at 0xd7d9) */
    /* XDATA_VAR8(0x0443) = 0xFF; - need to add this global if critical */

    /* The continuation checks XDATA[0x0B2F] and does conditional writes.
     * This is complex state machine logic that depends on runtime state.
     * Keeping as simplified stub for now.
     */
}

/*
 * pcie_cleanup_05fc - Return constant 0xF0
 * Address: 0x05fc -> targets 0xbe88
 *
 * Original disassembly at 0xbe88:
 *   be88: mov r7, #0xf0
 *   be8a: ret
 *
 * Simple function that just returns 0xF0.
 */
uint8_t pcie_cleanup_05fc(void)
{
    return 0xF0;
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
 * pcie_check_and_trigger_d5da - Check memory bit 7 and trigger PCIe handlers
 * Address: 0xd5da-0xd5e8 (15 bytes)
 *
 * Reads from banked memory using caller-set R1/R2/R3, checks if bit 7 is set,
 * and if so, triggers pcie_handler_e974 and pcie_handler_e06b(1).
 *
 * This function is called from bank 1 code after setting up:
 *   R1 = address low byte
 *   R2 = address high byte
 *   R3 = memory type (0x01=XDATA, 0x02=CODE, etc.)
 *
 * Original disassembly:
 *   d5da: lcall 0x0bc8       ; banked_load_byte(R1, R2, R3)
 *   d5dd: jnb acc.7, 0xd5e8  ; if bit 7 not set, skip to ret
 *   d5e0: lcall 0xe974       ; pcie_handler_e974()
 *   d5e3: mov r7, #0x01
 *   d5e5: lcall 0xe06b       ; pcie_handler_e06b(1)
 *   d5e8: ret
 *
 * Note: In C, caller must pass parameters explicitly since we can't
 * access caller's R1/R2/R3 register state.
 */
void pcie_check_and_trigger_d5da(uint8_t addrlo, uint8_t addrhi, uint8_t memtype)
{
    uint8_t val;

    /* Read from memory using banked_load_byte */
    val = banked_load_byte(addrlo, addrhi, memtype);

    /* Check if bit 7 is set */
    if (val & 0x80) {
        /* Trigger PCIe handlers */
        pcie_handler_e974();
        pcie_handler_e06b(0x01);
    }
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
 * transfer_handler_ce23 - PCIe lane configuration transfer handler
 * Address: 0xce23-0xce76 (84 bytes)
 *
 * Configures PCIe lane registers based on param:
 * - If param != 0: OR global status bytes with extended register values
 * - If param == 0: AND complement of global status bytes with ext reg values
 *
 * Then writes combined status back to register 0x35 and continues
 * PCIe initialization sequence.
 *
 * Disassembly:
 *   ce23: lcall 0xa334       ; status = pcie_read_status_a334()
 *   ce26: anl a, #0x3f       ; mask to bits 0-5
 *   ce28: mov r6, a          ; save in r6
 *   ce29: lcall 0xe890       ; pcie_handler_e890()
 *   ce2c: mov a, r7          ; check param (preserved in r7)
 *   ce2d-ce31: setup r3=0x02, r2=0x12, r1=0x40 for banked access
 *   ce33: jz 0xce4c          ; if param == 0, use AND-NOT path
 *   [ce35-ce4a: OR path - read ext regs, OR with globals, write to lane regs]
 *   [ce4c-ce64: AND-NOT path - read ext regs, AND with ~globals, write to lane regs]
 *   ce65: mov r1, #0x3f
 *   ce67: lcall 0x0be6       ; write accumulated value to ext reg 0x3f
 *   ce6a: lcall 0xe00c       ; get_pcie_status_flags_e00c()
 *   ce6d: lcall 0xa334       ; status = pcie_read_status_a334()
 *   ce70: anl a, #0xc0       ; keep bits 6-7 only
 *   ce72: orl a, r6          ; combine with saved bits 0-5
 *   ce73: lcall 0xe7f8       ; pcie_lane_init_e7f8() - writes combined to 0x35
 *   ce76: ljmp 0xe8cd        ; tail call clear_pcie_status_bytes_e8cd()
 *
 * PCIe extended registers (banked 0x02:0x12:xx -> XDATA 0xB2xx):
 *   0xB235: Link config status
 *   0xB23C-0xB23F: Lane config registers (write)
 *   0xB240-0xB243: Lane status registers (read)
 */
void transfer_handler_ce23(uint8_t param)
{
    uint8_t saved_status_lo;
    uint8_t reg_val;
    uint8_t combined;

    /* Save lower 6 bits of current status */
    saved_status_lo = pcie_read_status_a334() & 0x3F;

    /* Reset PCIe extended registers and clear lane config */
    pcie_handler_e890();

    if (param != 0) {
        /* Non-zero path: OR global values with extended register values */
        /* Read ext reg 0x40, OR with G_PCIE_WORK_0B34, write to 0x3C */
        reg_val = XDATA_REG8(0xB240);
        combined = G_PCIE_WORK_0B34 | reg_val;
        XDATA_REG8(0xB23C) = combined;

        /* Read ext reg 0x41, OR with G_PCIE_STATUS_0B35, write to 0x3D */
        reg_val = XDATA_REG8(0xB241);
        combined = G_PCIE_STATUS_0B35 | reg_val;
        XDATA_REG8(0xB23D) = combined;

        /* Read ext reg 0x42, OR with G_PCIE_STATUS_0B36, write to 0x3E */
        reg_val = XDATA_REG8(0xB242);
        combined = G_PCIE_STATUS_0B36 | reg_val;
        XDATA_REG8(0xB23E) = combined;

        /* Read ext reg 0x43, OR with G_PCIE_STATUS_0B37 */
        reg_val = XDATA_REG8(0xB243);
        combined = G_PCIE_STATUS_0B37 | reg_val;
    } else {
        /* Zero path: AND complement of globals with extended register values */
        /* Read ext reg 0x40, AND with ~G_PCIE_WORK_0B34, write to 0x3C */
        reg_val = XDATA_REG8(0xB240);
        combined = (~G_PCIE_WORK_0B34) & reg_val;
        XDATA_REG8(0xB23C) = combined;

        /* Read ext reg 0x41, AND with ~G_PCIE_STATUS_0B35, write to 0x3D */
        reg_val = XDATA_REG8(0xB241);
        combined = (~G_PCIE_STATUS_0B35) & reg_val;
        XDATA_REG8(0xB23D) = combined;

        /* Read ext reg 0x42, AND with ~G_PCIE_STATUS_0B36, write to 0x3E */
        reg_val = XDATA_REG8(0xB242);
        combined = (~G_PCIE_STATUS_0B36) & reg_val;
        XDATA_REG8(0xB23E) = combined;

        /* Read ext reg 0x43, AND with ~G_PCIE_STATUS_0B37 */
        reg_val = XDATA_REG8(0xB243);
        combined = (~G_PCIE_STATUS_0B37) & reg_val;
    }

    /* Write final combined value to lane config register 0x3F */
    XDATA_REG8(0xB23F) = combined;

    /* Build status flags from PCIe buffers */
    get_pcie_status_flags_e00c();

    /* Combine upper 2 bits of current status with saved lower 6 bits */
    combined = (pcie_read_status_a334() & 0xC0) | saved_status_lo;

    /* Write combined status to register 0x35 and continue init */
    XDATA_REG8(0xB235) = combined;

    /* Continue with pcie_lane_init_e7f8 logic:
     * - Read reg 0x37, clear bit 7, set bit 7, write back
     * - Trigger command via reg 0x38
     * - Poll for completion
     * - Clear lane config registers
     */
    reg_val = XDATA_REG8(0xB237);
    reg_val = (reg_val & 0x7F) | 0x80;
    XDATA_REG8(0xB237) = reg_val;

    /* Write 0x01 to command trigger register */
    XDATA_REG8(0xB238) = 0x01;

    /* Poll until bit 0 clears (command complete) */
    while (XDATA_REG8(0xB238) & 0x01) {
        /* Wait for hardware */
    }

    /* Read link config, keep only bits 6-7, write back */
    reg_val = XDATA_REG8(0xB235);
    reg_val &= 0xC0;
    XDATA_REG8(0xB235) = reg_val;

    /* Clear lane config registers 0x3C-0x3F */
    XDATA_REG8(0xB23C) = 0x00;
    XDATA_REG8(0xB23D) = 0x00;
    XDATA_REG8(0xB23E) = 0x00;
    XDATA_REG8(0xB23F) = 0x00;

    /* Clear PCIe status bytes (tail call) */
    clear_pcie_status_bytes_e8cd();
}

/*
 * transfer_continuation_d996 - PCIe transfer continuation after poll complete
 * Address: 0xd996-0xd9?? (large function)
 *
 * This is the continuation function called as a tail call from transfer_poll_handler_ceab.
 * It performs PCIe register configuration using banked memory access.
 *
 * Original disassembly (first part):
 *   d996: lcall 0xccac          ; Helper function
 *   d999: movx @dptr, a         ; Write result
 *   d99a: mov r7, #0x0f         ; r7 = 15
 *   d99c: lcall 0xe8a9          ; Another helper
 *   d99f: lcall 0xe57d          ; Timer/PHY setup
 *   d9a2: mov r7, #0x01
 *   d9a4: lcall 0xd630          ; Power helper
 *   d9a7: mov r7, #0x0f
 *   d9a9: lcall 0xd436          ; Config helper
 *   d9ac: lcall 0xe25e          ; State update
 *   d9af: banked_load_byte(0x02, 0x70, 0x41)  ; Extended memory read
 *   d9b8: anl a, #0xbf          ; Clear bit 6
 *   d9ba: banked_store_byte()   ; Write back
 *   ...continues with more register configuration...
 */
/* Forward declarations for functions defined later in this file or other files */
extern uint8_t banked_load_byte(uint8_t addrlo, uint8_t addrhi, uint8_t memtype);
extern void banked_store_byte(uint8_t addrlo, uint8_t addrhi, uint8_t memtype, uint8_t val);
extern void helper_e50d_full(uint8_t div_bits, uint8_t threshold_hi, uint8_t threshold_lo);
extern void pcie_trigger_cc11_e8ef(void);

/*
 * transfer_continuation_d996 - PCIe transfer continuation after poll complete
 * Address: 0xd996-0xd9?? (large function)
 *
 * This is the continuation function called as a tail call from transfer_poll_handler_ceab.
 * It performs extensive PCIe register configuration using banked memory access.
 *
 * Called functions:
 *   - 0xccac: helper_ccac
 *   - 0xe8a9: helper_e8a9
 *   - 0xe57d: timer_phy_setup_e57d
 *   - 0xd630: power_helper_d630
 *   - 0xd436: config_helper_d436
 *   - 0xe25e: state_update_e25e
 *   - 0x0bc8: banked_load_byte
 *   - 0x0be6: banked_store_byte
 *
 * TODO: Implement full logic once helper functions are available.
 * For now, this is a stub that performs minimal setup.
 */
void transfer_continuation_d996(void)
{
    uint8_t val;

    /* Extended memory: clear bit 6 of banked register memtype=0x02, addr=0x7041 */
    val = banked_load_byte(0x41, 0x70, 0x02);
    val &= 0xBF;  /* Clear bit 6 */
    banked_store_byte(0x41, 0x70, 0x02, val);

    /* Extended memory: clear bit 2 of banked register memtype=0x00, addr=0x1507 */
    val = banked_load_byte(0x07, 0x15, 0x00);
    val &= 0xFB;  /* Clear bit 2 */
    banked_store_byte(0x07, 0x15, 0x00, val);

    /* Note: Original has extensive register configuration with calls to:
     * helper_ccac, helper_e8a9(0x0F), timer_phy_setup_e57d,
     * power_helper_d630(0x01), config_helper_d436(0x0F), state_update_e25e
     * These will be added once the helper functions are implemented. */
}

/*
 * transfer_poll_handler_ceab - Timer poll and transfer handler
 * Address: 0xceab-0xcece (36 bytes)
 *
 * Sets up timer, polls status registers until ready, then calls
 * continuation handlers for PCIe transfer completion.
 *
 * Called from bank1 (5 calls from 0x14xxx addresses).
 *
 * Original disassembly:
 *   ceab: mov r7, #0x03        ; Set timer divisor bits to 3
 *   cead: lcall 0xe50d         ; Call timer setup helper
 *   ; Poll loop:
 *   ceb0: mov dptr, #0xe712    ; REG_LINK_STATUS_E712
 *   ceb3: movx a, @dptr        ; Read status
 *   ceb4: jb 0xe0.0, 0xcec6    ; If bit 0 set (done), exit loop
 *   ceb7: movx a, @dptr        ; Re-read status
 *   ceb8: anl a, #0x02         ; Isolate bit 1
 *   ceba: mov r7, a            ; Save in r7
 *   cebb: clr c
 *   cebc: rrc a                ; Shift right (bit 1 -> bit 0)
 *   cebd: jnz 0xcec6           ; If bit 1 was set, exit loop
 *   cebf: mov dptr, #0xcc11    ; REG_TIMER0_CSR
 *   cec2: movx a, @dptr        ; Read timer status
 *   cec3: jnb 0xe0.1, 0xceb0   ; If bit 1 NOT set, continue polling
 *   ; Exit path:
 *   cec6: lcall 0xe8ef         ; pcie_trigger_cc11_e8ef
 *   cec9: clr a
 *   ceca: mov r7, a            ; r7 = 0
 *   cecb: lcall 0xdd42         ; helper_dd42(0)
 *   cece: ljmp 0xd996          ; Tail call to continuation
 */
void transfer_poll_handler_ceab(void)
{
    uint8_t status;

    /* Set timer divisor bits to 3 and start timer */
    /* Note: e50d also uses r4/r5 for thresholds inherited from caller context,
     * but the key part is setting div_bits = 3 */
    helper_e50d_full(0x03, 0x00, 0x00);

    /* Poll loop: wait for link status or timer timeout */
    while (1) {
        /* Check link status register */
        status = REG_LINK_STATUS_E712;

        /* If bit 0 is set, transfer complete - exit */
        if (status & 0x01) {
            break;
        }

        /* Check bit 1 for error/alternate exit condition */
        if (status & 0x02) {
            break;
        }

        /* Check timer status - bit 1 indicates timeout */
        status = REG_TIMER0_CSR;
        if (status & 0x02) {
            /* Timeout - exit poll loop */
            break;
        }
    }

    /* Reset timer/trigger */
    pcie_trigger_cc11_e8ef();

    /* Call state handler with param 0 */
    helper_dd42(0);

    /* Continue with transfer completion (tail call) */
    transfer_continuation_d996();
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

/*
 * cmd_setup_aa37 - Command parameter setup (NVMe/SCSI)
 * Address: 0xaa37-0xab0d (~214 bytes) - Main body starts at 0xaa40
 *
 * This function sets up command parameters in the E420 register block
 * for NVMe or SCSI command processing. The original firmware has
 * overlapping code entry points at 0xaa36 and 0xaa37.
 *
 * Main body (0xaa40-0xab0d):
 *   1. Check G_CMD_MODE (0x07ca) for mode 2/3
 *   2. Call cmd_trigger_params and cmd_param_setup
 *   3. Set up command LBA registers (E426-E429)
 *   4. Clear command count/status registers (E42A-E42F)
 *   5. Copy control parameters from globals to registers
 *   6. For mode 2: set up extended parameters and flash error tracking
 *   7. Set G_CMD_STATUS to 0x16 (mode 2) or 0x12 (other modes)
 *
 * Original disassembly highlights:
 *   aa40: mov dptr, #0x07ca   ; G_CMD_MODE
 *   aa43: movx a, @dptr
 *   aa44: cjne a, #0x02, aa4b ; if mode != 2, r5=4
 *   aa47: mov r5, #0x05       ; else r5=5
 *   aa4f: lcall 0xdd12        ; cmd_trigger_params(r5, 0x0f)
 *   aa56: lcall 0xe120        ; cmd_param_setup(1, 1)
 *   aa59: mov dptr, #0xe426   ; REG_CMD_LBA_0
 *   aa5c: mov a, #0x4c        ; 'L' - NVMe command byte
 *   ... (sets up remaining registers)
 *   ab08: mov dptr, #0x07c4   ; G_CMD_STATUS
 *   ab0b: mov a, #0x16 or 0x12
 *   ab0d: movx @dptr, a; ret
 */
void cmd_setup_aa37(void)
{
    uint8_t cmd_mode;
    uint8_t flash_cmd_type;
    uint8_t event_flags;
    uint8_t r5_param;
    uint8_t error_val;

    /* Read command mode */
    cmd_mode = G_CMD_MODE;

    /* Set helper parameter based on mode */
    if (cmd_mode == 0x02) {
        r5_param = 0x05;
    } else {
        r5_param = 0x04;
    }

    /* Call setup helpers */
    cmd_trigger_params(0x0F, r5_param);
    cmd_param_setup(0x01, 0x01);

    /* Set up LBA registers */
    REG_CMD_LBA_0 = 0x4C;  /* 'L' - LBA marker */
    REG_CMD_LBA_1 = 0x17;

    /* LBA_2 depends on mode */
    cmd_mode = G_CMD_MODE;  /* Re-read mode */
    if (cmd_mode == 0x02) {
        REG_CMD_LBA_2 = 0x40;
    } else {
        REG_CMD_LBA_2 = 0x00;
    }

    /* LBA_3 depends on flash type and event flags */
    flash_cmd_type = G_FLASH_CMD_TYPE;
    event_flags = G_EVENT_FLAGS;
    if ((flash_cmd_type == 0x00) && (event_flags & 0x80)) {
        REG_CMD_LBA_3 = 0x54;  /* 'T' - Transfer mode */
    } else {
        REG_CMD_LBA_3 = 0x50;  /* 'P' - Standard mode */
    }

    /* Clear command count area (E42A-E42F) - 6 bytes */
    REG_CMD_COUNT_LOW = 0x00;
    REG_CMD_COUNT_HIGH = 0x00;
    XDATA_REG8(0xE42C) = 0x00;
    XDATA_REG8(0xE42D) = 0x00;
    XDATA_REG8(0xE42E) = 0x00;
    REG_CMD_RESP_STATUS = 0x00;

    /* Copy control parameters from globals */
    REG_CMD_CTRL = G_CMD_CTRL_PARAM;
    REG_CMD_TIMEOUT = G_CMD_TIMEOUT_PARAM;

    /* Mode 2 specific setup */
    if (cmd_mode == 0x02) {
        /* Calculate error value based on event flags */
        event_flags = G_EVENT_FLAGS;
        if (event_flags & 0x03) {
            error_val = 0x03;
        } else {
            error_val = 0x02;
        }
        G_FLASH_ERROR_0 = error_val;

        /* Set bit 3 if event flag bit 7 is set */
        if (event_flags & 0x80) {
            G_FLASH_ERROR_0 |= 0x08;
        }

        /* Set command parameter based on flash type */
        if (flash_cmd_type == 0x00) {
            REG_CMD_PARAM_L = G_FLASH_ERROR_0;
        } else {
            REG_CMD_PARAM_L = 0x02;
        }

        REG_CMD_PARAM_H = 0x00;
        REG_CMD_EXT_PARAM_0 = 0x80;

        /* Check if early exit via 0xaafb path */
        flash_cmd_type = G_FLASH_CMD_TYPE;
        if ((flash_cmd_type == 0x00) && (event_flags & 0x03)) {
            REG_CMD_EXT_PARAM_1 = 0x6D;  /* 'm' - early exit marker */
            /* Original calls FUN_CODE_aafb here and returns */
            /* For now, we continue to set final status */
        } else {
            REG_CMD_EXT_PARAM_1 = 0x65;  /* 'e' - normal marker */
        }
    }

    /* Set final command status based on mode */
    cmd_mode = G_CMD_MODE;
    if (cmd_mode == 0x02) {
        G_CMD_STATUS = 0x16;
    } else {
        G_CMD_STATUS = 0x12;
    }
}

/* pcie_setup_a38b - moved to queue_handlers.c */

/* usb_ep_loop_180d - moved to usb.c */

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

/* helper_3219, helper_3267 - Implemented in protocol.c as:
 *   nvme_call_and_signal_3219
 *   nvme_ep_config_init_3267
 */

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

/*
 * helper_1677 - Calculate address 0x0477 + (IDATA 0x53 * 4)
 * Address: 0x1677-0x1686 (16 bytes)
 *
 * Reads IDATA 0x53, multiplies by 4, adds 0x77, forms DPTR = 0x04XX.
 * The actual implementation is in dma.c as transfer_calc_work53_offset.
 *
 * Original disassembly:
 *   1677: mov a, 0x53          ; A = IDATA[0x53]
 *   1679: add a, acc           ; A *= 2
 *   167b: add a, acc           ; A *= 2 (total x4)
 *   167d: add a, #0x77
 *   167f: mov DPL, a
 *   1681: clr a
 *   1682: addc a, #0x04
 *   1684: mov DPH, a
 *   1686: ret
 */
void helper_1677(void)
{
    /* This sets up DPTR for subsequent operations.
     * In C context, this is typically used before a read/write.
     * The real implementation is transfer_calc_work53_offset in dma.c */
    __idata uint8_t *work53 = (__idata uint8_t *)0x53;
    uint16_t addr = 0x0477 + ((*work53) * 4);
    /* DPTR would be set to addr in assembly - in C this is a no-op
     * unless we read/write to the address */
    (void)addr;
}

/*
 * helper_1659 - Write param and calculate endpoint address
 * Address: 0x1659-0x1667 (15 bytes)
 *
 * Writes param to current DPTR position, then reads from 0x0464
 * and calculates address 0x044E + that value.
 *
 * Original disassembly:
 *   1659: movx @dptr, a        ; Write A to @DPTR (caller set DPTR)
 *   165a: mov dptr, #0x0464
 *   165d: movx a, @dptr        ; A = [0x0464]
 *   165e: add a, #0x4e
 *   1660: mov DPL, a
 *   ...
 *
 * In C, we can't replicate the DPTR-based chaining. This function
 * writes param to a working location and sets up for next operation.
 */
void helper_1659(uint8_t param)
{
    /* The param would be written to whatever DPTR the caller set up.
     * Based on context, this is likely writing to an endpoint config area.
     * Then it calculates address 0x044E + G_SYS_STATUS_PRIMARY */
    uint8_t status = *(__xdata uint8_t *)0x0464;  /* G_SYS_STATUS_PRIMARY */
    uint16_t addr = 0x044E + status;

    /* Write param to the computed address (approximation of the behavior) */
    *(__xdata uint8_t *)addr = param;
}

/*
 * helper_1ce4 - Calculate address 0x04B7 + IDATA[0x23]
 * Address: 0x1ce4-0x1cef (12 bytes)
 *
 * Adds 0xB7 to IDATA[0x23], returns DPTR = 0x04XX.
 * This is used to access endpoint descriptor/config table entries.
 * The real implementation is in usb.c as usb_calc_addr_04b7_plus.
 *
 * Original disassembly:
 *   1ce4: mov a, #0xb7
 *   1ce6: add a, 0x23         ; A = 0xB7 + IDATA[0x23]
 *   1ce8: mov 0x82, a         ; DPL
 *   1cea: clr a
 *   1ceb: addc a, #0x04       ; DPH = 0x04 + carry
 *   1ced: mov 0x83, a
 *   1cef: ret
 */
void helper_1ce4(void)
{
    /* This sets up DPTR for subsequent read/write in the caller.
     * In C, subsequent operations need to explicitly use the address. */
    __idata uint8_t *work23 = (__idata uint8_t *)0x23;
    uint16_t addr = 0x04B7 + (*work23);
    /* The caller typically reads from this address next */
    (void)addr;
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

/*
 * helper_165e - Get pointer to 0x044E + offset
 * Address: 0x165e-0x1667 (10 bytes)
 *
 * Adds 0x4E to offset and returns address in 0x04XX range.
 * In original assembly this sets DPTR; in C we return a pointer.
 * The return value from the pointer dereference is what callers expect.
 */
uint8_t helper_165e(uint8_t param)
{
    uint8_t low = param + 0x4E;
    uint16_t addr = 0x0400 + low;
    if (low < 0x4E) {
        addr += 0x0100;  /* Handle overflow carry */
    }
    return *(__xdata uint8_t *)addr;
}

/*
 * helper_1660 - Set up address and write to it
 * Address: 0x1660-0x1667 (8 bytes)
 *
 * This is entry 0x1660 which expects A already contains the computed offset.
 * First param is the offset (after add 0x4E), second is written to @DPTR.
 * Actually based on call site: helper_1660(status + 0x4E, r6_val)
 * - param1 = computed offset
 * - param2 = value to write to that address
 */
void helper_1660(uint8_t offset, uint8_t value)
{
    uint16_t addr = 0x0400 + offset;
    if (offset < 0x4E) {
        addr += 0x0100;  /* Handle potential carry */
    }
    *(__xdata uint8_t *)addr = value;
}

/* helper_0412 - moved to pcie.c */

/* helper_3291 - Implemented in protocol.c as queue_idx_get_3291 */

/* process_log_entries - Log processing function (0xc2e6) */
void process_log_entries(uint8_t param)
{
    (void)param;
    /* Stub */
}

/*
 * helper_96ae - Clear command engine and return status
 * Address: 0x96ae-0x96b6 (9 bytes)
 *
 * Saves R7/R6 to R3/R2, calls helper_e73a to clear command registers,
 * then returns the original R7 value.
 *
 * Original disassembly:
 *   96ae: mov r3, 0x07     ; R3 = R7
 *   96b0: mov r2, 0x06     ; R2 = R6
 *   96b2: lcall 0xe73a     ; Call helper_e73a (clear 0xE420-0xE43F)
 *   96b5: mov a, r3        ; A = R3 (original R7)
 *   96b6: ret
 *
 * Note: R7/R6 parameters and return value currently ignored in callers.
 */
void helper_96ae(void)
{
    /* Clear command engine registers */
    cmd_engine_clear();
}

/* helper_dd0e - Address: 0xdd0e
 * Sets R5=1, R7=0x0f and falls through to cmd_trigger_params
 */
void helper_dd0e(void)
{
    cmd_trigger_params(0x0F, 0x01);
}

/* helper_95a0 - Address: 0x95a0
 * Command error recovery helper
 * Sets R5=2, calls cmd_param_setup, writes to E424/E425/07C4
 */
void helper_95a0(uint8_t r7)
{
    (void)r7;
    /* Stub - should call cmd_param_setup(r7, 0x02) and write to cmd regs */
}

/*
 * helper_545c - Clear transfer flag
 * Address: 0x545c-0x5461 (6 bytes)
 *
 * Clears the transfer flag at 0x0AF8.
 *
 * Original disassembly:
 *   545c: clr a              ; A = 0
 *   545d: mov dptr, #0x0af8  ; G_TRANSFER_FLAG_0AF8
 *   5460: movx @dptr, a      ; Write 0
 *   5461: ret
 */
void helper_545c(void)
{
    G_TRANSFER_FLAG_0AF8 = 0;
}

/*
 * helper_cb05 - Set PHY config enable bit
 * Address: 0xcb05-0xcb0e (10 bytes)
 *
 * Sets bit 0 in REG_PHY_CFG_C6A8.
 *
 * Original disassembly:
 *   cb05: mov dptr, #0xc6a8  ; REG_PHY_CFG_C6A8
 *   cb08: movx a, @dptr      ; Read
 *   cb09: anl a, #0xfe       ; Clear bit 0
 *   cb0b: orl a, #0x01       ; Set bit 0
 *   cb0d: movx @dptr, a      ; Write back
 *   cb0e: ret
 */
void helper_cb05(void)
{
    uint8_t val;
    val = REG_PHY_CFG_C6A8;
    val &= 0xFE;  /* Clear bit 0 */
    val |= 0x01;  /* Set bit 0 */
    REG_PHY_CFG_C6A8 = val;
}

/*
 * scsi_dma_mode_setup - SCSI DMA mode setup
 * Address: Likely part of 0x5462 context or nearby
 *
 * Called when G_EP_STATUS_CTRL != 0 to configure DMA for SCSI transfers.
 * Sets up DMA registers for the pending transfer mode.
 */
void scsi_dma_mode_setup(void)
{
    /* Configure DMA for SCSI mode transfer */
    /* This is called from scsi_mode_setup_5462 when EP status is active */

    /* Set DMA configuration for SCSI mode */
    REG_DMA_CONFIG = 0xA0;  /* Enable DMA with mode setting */
}

/*
 * scsi_handle_init_4d92 - SCSI handle initialization
 * Address: 0x4d92-0x4e6c (219 bytes)
 *
 * This is an alias for the SCSI command processing initialization.
 * Called by usb_helper_5112 after extracting transfer parameters.
 * The actual implementation is in scsi.c as scsi_cmd_process.
 */
void scsi_handle_init_4d92(void)
{
    /* The real implementation initializes transfer state and
     * starts the SCSI command state machine. For now, stub. */
}

/*===========================================================================
 * Missing Helper Stubs for scsi.c
 *===========================================================================*/

/*
 * helper_1cc1 - Set endpoint queue control to 0x84
 * Address: 0x1cc1-0x1cc7 (7 bytes)
 */
void helper_1cc1(void)
{
    G_EP_QUEUE_CTRL = 0x84;
}

/*
 * helper_1ba5 - Read 16-bit buffer address
 * Address: 0x1ba5-0x1bad (9 bytes)
 *
 * Returns 16-bit buffer address (G_BUF_ADDR_HI:G_BUF_ADDR_LO).
 */
uint16_t helper_1ba5(void)
{
    return ((uint16_t)G_BUF_ADDR_HI << 8) | G_BUF_ADDR_LO;
}

/* mul_add_index - Multiply and add index helper (2-param version) */
void mul_add_index(uint8_t param1, uint8_t param2)
{
    (void)param1;
    (void)param2;
}

/*
 * helper_1bd7 - Part of calculation chain
 * Address: 0x1bd7-0x1bdb (5 bytes)
 *
 * Mid-function entry for arithmetic chain.
 */
void helper_1bd7(void)
{
    /* Part of calculation chain - context-dependent */
}

/*
 * helper_1c6d - Subtract 16-bit value from core state
 * Address: 0x1c6d-0x1c76 (10 bytes)
 *
 * Subtracts r6:r7 from IDATA[0x16:0x17].
 * Note: IDATA stores in lo:hi order, params in hi:lo.
 */
void helper_1c6d(uint8_t hi, uint8_t lo)
{
    uint8_t temp = I_CORE_STATE_H;
    uint8_t borrow = (lo > temp) ? 1 : 0;
    I_CORE_STATE_H = temp - lo;
    I_CORE_STATE_L = I_CORE_STATE_L - hi - borrow;
}

/* helper_1b9d - Address: 0x1b9d (2-param version returning uint8_t) */
uint8_t helper_1b9d(uint8_t p1, uint16_t p2)
{
    (void)p1;
    (void)p2;
    return 0;
}

/*
 * helper_15a0 - Calculate state pointer from I_WORK_43
 * Address: 0x15a0-0x15ab (12 bytes)
 *
 * Disassembly:
 *   15a0: mov a, #0x4e       ; A = 0x4e
 *   15a2: add a, 0x43        ; A = 0x4e + IDATA[0x43]
 *   15a4: mov DPL, a         ; DPL = A
 *   15a6: clr a              ; A = 0
 *   15a7: addc a, #0x01      ; A = 0x01 + carry
 *   15a9: mov DPH, a         ; DPH = A
 *   15ab: ret
 *
 * Returns pointer to XDATA at (0x014e + I_WORK_43).
 */
__xdata uint8_t * helper_15a0(void)
{
    return (__xdata uint8_t *)(0x014e + I_WORK_43);
}

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

/*
 * helper_03a4 - Bank-switching trampoline to power_config_init
 * Address: 0x03a4-0x03a8 (5 bytes)
 *
 * This is a dispatch entry that loads DPTR with 0xcb37 and jumps
 * to the bank 0 handler at 0x0300, effectively calling power_config_init.
 *
 * Original disassembly:
 *   03a4: mov dptr, #0xcb37
 *   03a7: ajmp 0x0300
 */
extern void power_config_init(void);

void helper_03a4(void)
{
    power_config_init();
}

/*
 * helper_041c - Bank-switching trampoline to power_check_status_e647
 * Address: 0x041c-0x0420 (5 bytes)
 *
 * This is a dispatch entry that loads DPTR with 0xe647 and jumps
 * to the bank 0 handler at 0x0300, effectively calling power_check_status_e647.
 *
 * Original disassembly:
 *   041c: mov dptr, #0xe647
 *   041f: ajmp 0x0300
 */
extern void power_check_status_e647(void);

void helper_041c(void)
{
    power_check_status_e647();
}

/*
 * helper_1d43 - Clear TLP status and setup transaction table
 * Address: 0x1d43-0x1d61+ (complex)
 *
 * Clears G_TLP_STATUS and performs table address calculation.
 */
void helper_1d43(void)
{
    G_TLP_STATUS = 0;
    /* Additional setup performed via helper calls */
}

/*
 * helper_1b47 - Update NVMe device and control status
 * Address: 0x1b47-0x1b5f (25 bytes)
 *
 * Reads G_STATE_HELPER_42, ORs with high bits of REG_NVME_DEV_STATUS,
 * writes result. Then sets bit 1 of REG_NVME_CTRL_STATUS.
 */
void helper_1b47(void)
{
    uint8_t state_val = G_STATE_HELPER_42;
    uint8_t dev_status_hi = REG_NVME_DEV_STATUS & 0xC0;
    REG_NVME_DEV_STATUS = state_val | dev_status_hi;
    REG_NVME_CTRL_STATUS = (REG_NVME_CTRL_STATUS & 0xFD) | 0x02;
}

/*
 * helper_1c56 - Read NVMe device status high bits
 * Address: 0x1c56-0x1c5c (7 bytes)
 *
 * Returns REG_NVME_DEV_STATUS & 0xC0 (top 2 bits).
 */
uint8_t helper_1c56(void)
{
    return REG_NVME_DEV_STATUS & 0xC0;
}

/*
 * helper_1d39 - Add to USB index counter with 5-bit wrap
 * Address: 0x1d39-0x1d42 (10 bytes)
 *
 * Adds param to G_USB_INDEX_COUNTER, masks to 5 bits.
 */
void helper_1d39(uint8_t param)
{
    G_USB_INDEX_COUNTER = (G_USB_INDEX_COUNTER + param) & 0x1F;
}

/*
 * helper_1b77 - Read 16-bit core state
 * Address: 0x1b77-0x1b7d (7 bytes)
 *
 * Returns I_CORE_STATE_L:I_CORE_STATE_H as 16-bit value.
 */
uint16_t helper_1b77(void)
{
    return ((uint16_t)I_CORE_STATE_L << 8) | I_CORE_STATE_H;
}

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

/*
 * helper_048a - State checksum/validation helper
 * Address: 0x048a -> targets 0xece1 (bank 1)
 *
 * IMPORTANT: This uses overlapping code entry - 0xece1 enters in the middle
 * of another function's lcall instruction, creating alternate execution path.
 *
 * Original disassembly when entering at 0x16ce1:
 *   ece1: jc 0x6cf0       ; if carry set, goto alternate path
 *   ece3: mov a, r5       ; get loop counter
 *   ece4: cjne a, #0x08, 0x6cd2  ; loop back if counter < 8
 *   ece7: mov dptr, #0x0240
 *   ecea: mov a, r7       ; get computed checksum
 *   eceb: movx @dptr, a   ; write result to 0x0240
 *   ecec: ret
 *
 * When carry set (path at 0x6cf0 / 0x16cf0):
 *   ecf0: anl a, #0xf0    ; mask high nibble
 *   ecf2: orl a, #0x08    ; set bit 3
 *   ecf4: lcall 0xbfa7    ; call helper
 *   ecf7: mov a, #0x1a
 *   ecf9: lcall 0xbfb7    ; call another helper
 *   ecfc: mov r1, #0x3f
 *   ecfe: mov a, r7
 *   ecff: lcall 0x0be6    ; banked_store_byte
 *   ed02: lcall 0x05c5    ; dispatch_05c5
 *   ed05: clr a
 *   ed06: mov dptr, #0x023f
 *   ed09: movx @dptr, a   ; clear G_BANK1_STATE_023F
 *   ed0a: ret
 *
 * The loop at 0x6cd2 (0x16cd2) XORs bytes from 0x0241-0x0248 together.
 * This appears to compute a checksum of state bytes.
 *
 * Called when G_STATE_FLAG_0AF1 bit 2 (0x04) is set.
 */
void helper_048a(void)
{
    /* This function computes an XOR checksum of bytes at 0x0241-0x0248
     * and stores the result at 0x0240.
     *
     * The overlapping code entry means the carry flag state at entry
     * determines which path is taken. Without knowing the carry state
     * from the C caller, we implement the common (no-carry) path.
     */
    uint8_t checksum = 0xFF;  /* Initial value as in original */
    uint8_t i;

    /* XOR together bytes at 0x0241-0x0248 */
    for (i = 0; i < 8; i++) {
        checksum ^= XDATA_VAR8(0x0241 + i);
    }

    /* Store result at 0x0240 */
    XDATA_VAR8(0x0240) = checksum;
}

/*===========================================================================
 * Additional Missing Helper Stubs for scsi.c
 *===========================================================================*/

/*
 * helper_048f - Bank-switching trampoline to NOP space (empty)
 * Address: 0x048f-0x0493 (5 bytes)
 *
 * This dispatch entry loads DPTR with 0xef1e and jumps to the bank 1
 * handler at 0x0311. The target 0xef1e contains NOPs (0x00), indicating
 * this is reserved/unused space or a placeholder for future functionality.
 *
 * Original disassembly:
 *   048f: mov dptr, #0xef1e
 *   0492: ajmp 0x0311
 */
void helper_048f(void)
{
    /* Target is NOP space - intentionally empty */
}

/*
 * helper_1c77 - Read NVMe command param high bits
 * Address: 0x1c77-0x1c7d (7 bytes)
 *
 * Returns REG_NVME_CMD_PARAM & 0xE0 (top 3 bits).
 */
uint8_t helper_1c77(void)
{
    return REG_NVME_CMD_PARAM & 0xE0;
}

/*
 * ep_config_read - Read endpoint configuration
 * Address: Various
 *
 * Reads endpoint config from table based on param.
 */
uint8_t ep_config_read(uint8_t param)
{
    return *(__xdata uint8_t *)(0x05A8 + param);
}

/* helper_2608 - Address: 0x2608 */
/* helper_2608 - moved to dma.c as handler_2608 */

/* helper_3adb - implemented in protocol.c as handler_3adb */

/* helper_488f - Queue processor
 * Address: 0x488f
 */
void helper_488f(void)
{
    uint8_t status;

    G_STATE_FLAG_06E6 = 1;
    I_WORK_39 = 0;

    status = REG_NVME_LINK_STATUS;
    if ((status >> 1) & 1) {
        G_SYS_STATUS_PRIMARY = 0;
        G_SYS_STATUS_SECONDARY = 0;
        if (XDATA8(0x0AF8) != 0) {
            REG_NVME_QUEUE_TRIGGER = XDATA8(0xC51A);
        }
    }
}

/* helper_3e81 - USB status handler
 * Address: 0x3e81
 */
void helper_3e81(void)
{
    uint8_t pending;
    uint8_t counter;

    pending = REG_NVME_QUEUE_PENDING & 0x3F;
    I_WORK_38 = pending;

    counter = XDATA8(0x0500 + pending + 0x17);
    counter++;
    XDATA8(0x0500 + pending + 0x17) = counter;
    I_WORK_39 = counter;
}

/* helper_4784 - Link status handler
 * Address: 0x4784
 */
void helper_4784(void)
{
    uint8_t state = I_STATE_6A;

    if (state == 3) {
        REG_NVME_QUEUE_CFG = (REG_NVME_QUEUE_CFG & 0xF7) | 0x08;
        REG_NVME_QUEUE_CFG &= 0xFE;
    } else if (state == 4) {
        REG_NVME_QUEUE_CFG = (REG_NVME_QUEUE_CFG & 0xF7) | 0x08;
    } else if (state == 5) {
        if (XDATA8(0x0001) == 5) {
            REG_NVME_QUEUE_CFG = (REG_NVME_QUEUE_CFG & 0xF7) | 0x08;
        }
    }
}

/* helper_49e9 - USB control handler
 * Address: 0x49e9
 */
void helper_49e9(void)
{
    uint8_t queue_idx;
    uint8_t expected;
    uint8_t counter;

    queue_idx = REG_NVME_QUEUE_INDEX;
    I_WORK_38 = queue_idx;
    REG_NVME_QUEUE_INDEX = 0xFF;

    expected = XDATA8(0x009F);
    counter = XDATA8(0x0517);
    counter++;
    XDATA8(0x0517) = counter;

    if (counter != expected) {
        uint8_t alt_val = XDATA8(0x00C2);
        if (XDATA8(0x0517) == alt_val) {
            /* Match */
        }
    }
}

/*
 * helper_0584 - Bank-switching trampoline to NOP space (empty)
 * Address: 0x0584-0x0588 (5 bytes)
 *
 * This dispatch entry loads DPTR with 0xef24 and jumps to the bank 1
 * handler at 0x0311. The target 0xef24 contains NOPs (0x00), indicating
 * this is reserved/unused space or a placeholder for future functionality.
 *
 * Original disassembly:
 *   0584: mov dptr, #0xef24
 *   0587: ajmp 0x0311
 */
void helper_0584(void)
{
    /* Target is NOP space - intentionally empty */
}

/*
 * helper_1aad - Setup queue parameter and calculate buffer offset
 * Address: 0x1aad-0x1acd (33 bytes)
 *
 * Stores param to G_EP_QUEUE_PARAM, then calculates:
 *   offset = G_BUF_BASE + (G_USB_PARAM_0B00 * 0x40)
 * And stores the result to G_BUF_OFFSET_HI:G_BUF_OFFSET_LO.
 */
void helper_1aad(uint8_t param)
{
    uint16_t base = ((uint16_t)G_BUF_BASE_HI << 8) | G_BUF_BASE_LO;
    uint16_t offset_val = (uint16_t)G_USB_PARAM_0B00 * 0x40;
    uint16_t result = base + offset_val;

    G_EP_QUEUE_PARAM = param;
    G_BUF_OFFSET_HI = (uint8_t)(result >> 8);
    G_BUF_OFFSET_LO = (uint8_t)result;
}

/*
 * helper_1cae - Increment queue index with 5-bit wrap
 * Address: 0x1cae-0x1cb6 (9 bytes)
 *
 * Disassembly:
 *   1cae: mov dptr, #0x0b00  ; G_USB_PARAM_0B00
 *   1cb1: movx a, @dptr      ; Read current value
 *   1cb2: inc a              ; Increment
 *   1cb3: anl a, #0x1f       ; Mask to 5 bits (0-31)
 *   1cb5: movx @dptr, a      ; Write back
 *   1cb6: ret
 */
void helper_1cae(void)
{
    G_USB_PARAM_0B00 = (G_USB_PARAM_0B00 + 1) & 0x1F;
}

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
/*
 * TODO: dma_buffer_store_result_e68f is complex as it uses
 * DPTR-returning helpers (0x9983, 0x99bc, 0x9980) that modify DPTR
 * for table indexing. These helpers compute DPTR = base + R7*34.
 *
 * The function:
 * 1. Sets DPTR=0x05b3, calls 0x9983 to offset, writes R5 to result
 * 2. Calls 0x99bc to offset further, writes R3 to result
 * 3. Sets DPTR=0x0a5f, saves value, calls 0x9980, restores value
 *
 * This requires careful assembly-level handling as C can't directly
 * express DPTR manipulation across function boundaries.
 */
void dma_buffer_store_result_e68f(void)
{
    /* TODO: Implement - requires inline assembly or restructuring */
    (void)0;
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

/* pcie_channel_setup_e19e - moved to pcie.c */

/*===========================================================================
 * Helper Stubs for Bank1 High Functions
 *===========================================================================*/

/* helper_9983 - Address: 0x9983 - Entry point into pcie_store_to_05b8 (0x9980)
 * This is an overlapping code entry where DPTR is assumed to already be set.
 * Implemented as part of pcie_store_to_05b8 in pcie.c:1459 */

/*
 * helper_e762 - Store address calculation to G_WORK_05AF
 * Address: 0xe762-0xe774 (19 bytes)
 *
 * Computes a 32-bit address (0x00D0_00_xx) where xx = param,
 * then stores it at G_WORK_05AF (0x05AF).
 */
void helper_e762(uint8_t param)
{
    /* Store 32-bit address value at 0x05AF */
    volatile uint8_t __xdata *ptr = (__xdata uint8_t *)0x05AF;

    /* r4:r5:r6:r7 = (0, 0xD0, 0, param) */
    ptr[0] = 0;     /* r4 - high byte */
    ptr[1] = 0xD0;  /* r5 */
    ptr[2] = 0;     /* r6 */
    ptr[3] = param; /* r7 - low byte */
}

/*
 * helper_e8f9 - Clear flag and check PCIe status
 * Address: 0xe8f9-0xe901 (9 bytes)
 *
 * Clears 0x05AE and calls helper_c1f9.
 */
extern uint8_t helper_c1f9(void);

uint8_t helper_e8f9(void)
{
    XDATA8(0x05AE) = 0;
    return helper_c1f9();
}

/*
 * helper_0be6 - Conditional XDATA write helper (register-based)
 * Address: 0x0be6-0x0bef (10 bytes)
 *
 * This is a low-level helper that writes the A register to XDATA at
 * address r2:r1 if r3 == 1. It's used for conditional memory writes
 * where the calling function has set up the register context.
 *
 * Original disassembly:
 *   0be6: cjne r3, #0x01, 0x0bef  ; if r3 != 1, skip to return
 *   0be9: mov 0x82, r1            ; DPL = r1 (low byte of address)
 *   0beb: mov 0x83, r2            ; DPH = r2 (high byte of address)
 *   0bed: movx @dptr, a           ; write A to xdata[r2:r1]
 *   0bee: ret
 *   0bef: ret                     ; skip case - just return
 *
 * Since this function operates on raw register state from the caller,
 * and C cannot easily replicate this register-passing behavior,
 * the stub remains empty. Callers that use this function should be
 * aware that the write operation is not performed in the C version.
 *
 * The function is primarily called from:
 * - pcie_lane_init_e7f8 (after setting up r1, r2, r3, A via prior calls)
 * - helper_048a (checksum computation path)
 */
void helper_0be6(void)
{
    /* Register-based function - cannot be directly replicated in C.
     * The calling context would need to set up r1, r2, r3, A registers
     * which is not possible with standard C calling conventions. */
}

/*
 * helper_a33d - Read PCIe extended register by offset
 * Address: 0xa33d-0xa343 (7 bytes)
 *
 * Disassembly:
 *   a33d: mov r3, #0x02      ; R3 = 0x02 (bank)
 *   a33f: mov r2, #0x12      ; R2 = 0x12 (high byte)
 *   a341: ljmp 0x0bc8        ; banked_load_byte
 *
 * Takes R1 from caller as the register offset within the PCIe extended
 * register space. Reads from bank 0x02:0x12xx which maps to XDATA 0xB2xx.
 *
 * Parameters:
 *   reg_offset - The low byte offset (0x00-0xFF) of the register to read
 *
 * Returns: Value read from PCIe extended register 0xB200 + reg_offset
 */
uint8_t helper_a33d(uint8_t reg_offset)
{
    /* Read from PCIe extended register at 0xB200 + offset */
    return XDATA_REG8(0xB200 + reg_offset);
}

/* ext_mem_access_0bc8 - Address: 0x0bc8 - Extended memory access */
void ext_mem_access_0bc8(uint8_t bank, uint8_t addr_hi, uint8_t addr_lo)
{
    (void)bank; (void)addr_hi; (void)addr_lo;
    /* Stub */
}

/* helper_e677 - Address: 0xe677 - PCIe channel init helper */
void helper_e677(void)
{
    /* Clear buffer flag at 0x044C */
    XDATA8(0x044C) = 0;

    /* Initialize primary channel 0xCC1D with trigger sequence */
    XDATA_REG8(0xCC1D) = 0x04;
    XDATA_REG8(0xCC1D) = 0x02;

    /* Initialize secondary channel 0xCC5D with trigger sequence */
    XDATA_REG8(0xCC5D) = 0x04;
    XDATA_REG8(0xCC5D) = 0x02;
}

/*===========================================================================
 * Bank1 High 1-Call Functions (0xE000-0xFFFF)
 *===========================================================================*/

/* get_pcie_status_flags_e00c - moved to pcie.c */

/*
 * check_nvme_ready_e03c - Check if NVMe subsystem is ready
 * Address: 0xe03c-0xe06a (47 bytes)
 *
 * Checks multiple status conditions:
 *   - 0x0ACF must equal 0xA1 (XOR check)
 *   - 0x0AD3 must be 0
 *   - 0x0AD5 must equal 1
 *   - 0x0AD1 must be 0
 * If all pass: writes init sequence to 0x9003-0x9004, 0x9E00, returns 3
 * If any fail: returns 5
 */
uint8_t check_nvme_ready_e03c(void)
{
    /* Check 0x0ACF == 0xA1 */
    if (XDATA8(0x0ACF) != 0xA1) {
        return 5;  /* Not ready */
    }

    /* Check 0x0AD3 == 0 */
    if (XDATA8(0x0AD3) != 0) {
        return 5;
    }

    /* Check 0x0AD5 == 1 */
    if (XDATA8(0x0AD5) != 1) {
        return 5;
    }

    /* Check 0x0AD1 == 0 */
    if (XDATA8(0x0AD1) != 0) {
        return 5;
    }

    /* All checks passed - initialize NVMe registers */
    XDATA_REG8(0x9003) = 0x00;
    XDATA_REG8(0x9004) = 0x01;
    XDATA_REG8(0x9E00) = 0x00;

    return 3;  /* Ready */
}

/*
 * pcie_dma_init_e0e4 - Initialize PCIe DMA transfer
 * Address: 0xe0e4-0xe0f3 (16 bytes)
 *
 * Calls initialization helper 0x053e, sets up extended memory
 * parameters (r3=0xFF, r2=0x52, r1=0xE6), calls 0x538d, then 0xe7ae
 */
extern void helper_053e(void);
extern void helper_538d(uint8_t r3, uint8_t r2, uint8_t r1);
extern void uart_wait_tx_ready(void);

void pcie_dma_init_e0e4(void)
{
    /* Initial setup */
    helper_053e();

    /* Set up extended memory parameters and call */
    helper_538d(0xFF, 0x52, 0xE6);

    /* Final PCIe/DMA handler */
    uart_wait_tx_ready();
}

/*
 * helper_a2ff - Read PCIe extended register 0x34
 * Address: 0xa2ff-0xa307 (9 bytes)
 *
 * Disassembly:
 *   a2ff: mov r1, #0x34      ; R1 = 0x34 (register offset)
 *   a301: mov r3, #0x02      ; R3 = 0x02 (bank)
 *   a303: mov r2, #0x12      ; R2 = 0x12 (high byte)
 *   a305: ljmp 0x0bc8        ; banked_load_byte
 *
 * Reads PCIe extended register 0x34 (link state) from banked memory.
 * Bank 0x02:0x12xx maps to XDATA 0xB2xx.
 */
uint8_t helper_a2ff(void)
{
    /* Read from PCIe extended register 0x34 (0xB234) */
    return XDATA_REG8(0xB234);
}

/*
 * helper_053e - Bank-switching trampoline to NOP space (empty)
 * Address: 0x053e-0x0542 (5 bytes)
 *
 * This dispatch entry loads DPTR with 0xef03 and jumps to the bank 1
 * handler at 0x0311. The target 0xef03 contains NOPs (0x00), indicating
 * this is reserved/unused space or a placeholder for future functionality.
 *
 * Original disassembly:
 *   053e: mov dptr, #0xef03
 *   0541: ajmp 0x0311
 */
void helper_053e(void)
{
    /* Target is NOP space - intentionally empty */
}

/* helper_538d - Address: 0x538d - Extended memory setup */
void helper_538d(uint8_t r3, uint8_t r2, uint8_t r1)
{
    (void)r3; (void)r2; (void)r1;
    /* Stub */
}

/*
 * check_pcie_status_e239 - Check PCIe status and initialize NVMe
 * Address: 0xe239-0xe25d (37 bytes)
 *
 * Checks 0x9090 bit 7 and 0x0AD3, then increments counter and
 * copies bit 0 of 0x9000 to 0x9E00.
 */
extern uint8_t helper_a71b(void);

uint8_t check_pcie_status_e239(void)
{
    uint8_t val;

    /* Check 0x9090 bit 7 */
    val = XDATA_REG8(0x9090);
    if (!(val & 0x80)) {
        return 5;  /* Not ready */
    }

    /* Check 0x0AD3 == 0 */
    if (XDATA8(0x0AD3) != 0) {
        return 5;  /* Busy */
    }

    /* Call helper and increment result */
    val = helper_a71b();
    XDATA8(0x0AD3) = val + 1;

    /* Copy bit 0 of 0x9000 to 0x9E00 */
    val = XDATA_REG8(0x9000) & 0x01;
    XDATA_REG8(0x9E00) = val;

    return 3;  /* Success */
}

/* pcie_dma_config_e330 - moved to pcie.c */

/* pcie_channel_disable_e5fe - moved to pcie.c */

/*
 * helper_a71b - Clear NVME status register
 * Address: 0xa71b-0xa721 (7 bytes)
 *
 * Original disassembly:
 *   a71b: mov dptr, #0x9003  ; REG_NVME_STATUS_9003
 *   a71e: clr a
 *   a71f: movx @dptr, a      ; Write 0 to 0x9003
 *   a720: inc dptr
 *   a721: ret
 *
 * Returns 0.
 */
uint8_t helper_a71b(void)
{
    XDATA_REG8(0x9003) = 0;
    return 0;
}

/*
 * helper_9617 - Set interrupt enable bit 4
 * Address: 0x9617-0x9620 (10 bytes)
 *
 * Sets bit 4 of REG_INT_ENABLE (0xC801).
 *
 * Original disassembly:
 *   9617: mov dptr, #0xc801  ; REG_INT_ENABLE
 *   961a: movx a, @dptr      ; Read
 *   961b: anl a, #0xef       ; Clear bit 4
 *   961d: orl a, #0x10       ; Set bit 4
 *   961f: movx @dptr, a      ; Write back
 *   9620: ret
 */
void helper_9617(void)
{
    uint8_t val = REG_INT_ENABLE;
    val = (val & ~0x10) | 0x10;  /* Set bit 4 */
    REG_INT_ENABLE = val;
}

/*
 * helper_95bf - DMA config write sequence
 * Address: 0x95bf-0x95c8 (10 bytes)
 *
 * Writes 0x04 then 0x02 to REG_XFER_DMA_CFG (0xCC99).
 * This is the tail of cmd_clear_cc9a_setup but can be called directly.
 *
 * Original disassembly:
 *   95bf: mov dptr, #0xcc99  ; REG_XFER_DMA_CFG
 *   95c2: mov a, #0x04
 *   95c4: movx @dptr, a      ; Write 0x04
 *   95c5: mov a, #0x02
 *   95c7: movx @dptr, a      ; Write 0x02
 *   95c8: ret
 */
void helper_95bf(void)
{
    REG_XFER_DMA_CFG = 0x04;
    REG_XFER_DMA_CFG = 0x02;
}

/*
 * helper_95e1 - Config helper with two params
 * Address: 0x95e1
 */
void helper_95e1(uint8_t r7, uint8_t r5)
{
    (void)r7;
    (void)r5;
}

/*
 * helper_bd23 - Set bit 5 in register at DPTR
 * Address: 0xbd23-0xbd29 (7 bytes)
 *
 * Original disassembly:
 *   bd23: movx a, @dptr      ; Read
 *   bd24: anl a, #0xdf       ; Clear bit 5
 *   bd26: orl a, #0x20       ; Set bit 5
 *   bd28: movx @dptr, a      ; Write back
 *   bd29: ret
 *
 * Note: Called with DPTR pre-set to target register.
 * In C, we pass the register address as a parameter.
 */
void helper_bd23(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xDF) | 0x20;  /* Set bit 5 */
    *reg = val;
}

/* pcie_disable_and_trigger_e74e - moved to pcie.c */

/* pcie_wait_and_ack_e80a - moved to pcie.c */

/* clear_status_bytes_e8cd - moved to pcie.c (renamed to clear_pcie_status_bytes_e8cd) */

/*
 * helper_e50d - Timer0 configuration setup
 * Address: 0xe50d-0xe528 (28 bytes)
 *
 * Configures Timer0 with divisor bits, threshold value, and starts it.
 *
 * Parameters (passed in registers):
 *   R7: Bits 0-2 to set in REG_TIMER0_DIV (0xCC10)
 *   R4: High byte for timer threshold (0xCC12)
 *   R5: Low byte for timer threshold (0xCC13)
 *
 * Original disassembly:
 *   e50d: lcall 0xe8ef       ; Reset timer (pcie_trigger_cc11_e8ef)
 *   e510: mov dptr, #0xcc10  ; REG_TIMER0_DIV
 *   e513: movx a, @dptr      ; Read
 *   e514: anl a, #0xf8       ; Clear bits 0-2
 *   e516: orl a, r7          ; OR in div_bits
 *   e517: movx @dptr, a      ; Write back
 *   e518: mov r7, 0x05       ; R7 = R5 (threshold_lo)
 *   e51a: mov dptr, #0xcc12
 *   e51d: mov a, r4          ; threshold_hi
 *   e51e: movx @dptr, a      ; Write to CC12
 *   e51f: inc dptr
 *   e520: mov a, r7
 *   e521: movx @dptr, a      ; Write threshold_lo to CC13
 *   e522: mov dptr, #0xcc11  ; REG_TIMER0_CSR
 *   e525: mov a, #0x01
 *   e527: movx @dptr, a      ; Start timer
 *   e528: ret
 */
extern void pcie_trigger_cc11_e8ef(void);

void helper_e50d_full(uint8_t div_bits, uint8_t threshold_hi, uint8_t threshold_lo)
{
    uint8_t val;

    /* Reset timer */
    pcie_trigger_cc11_e8ef();

    /* Configure timer divisor - clear bits 0-2, set new value */
    val = REG_TIMER0_DIV;
    val = (val & 0xF8) | (div_bits & 0x07);
    REG_TIMER0_DIV = val;

    /* Set threshold value (16-bit across CC12:CC13) */
    XDATA_REG8(0xCC12) = threshold_hi;
    XDATA_REG8(0xCC13) = threshold_lo;

    /* Start timer */
    REG_TIMER0_CSR = 0x01;
}

/* Simple stub wrapper for backward compatibility */
void helper_e50d(void)
{
    helper_e50d_full(0, 0, 0);
}

/* pcie_trigger_cc11_e8ef - moved to pcie.c */

/* clear_flag_and_call_e8f9 - Same as helper_e8f9, implemented above at line 4013 */

/*
 * set_flag_and_call_e902 - Set flag and call helper
 * Address: 0xe902-0xe908 (7 bytes)
 *
 * Sets 0x05AE to 1 and calls helper_c1f9 (opposite of helper_e8f9).
 */
void set_flag_and_call_e902(void)
{
    XDATA8(0x05AE) = 1;
    (void)helper_c1f9();  /* helper_c1f9 is declared earlier */
}

/*
 * pcie_trigger_and_call_e90b - Trigger PCIe and call helper
 * Address: 0xe90b-0xe911 (7 bytes)
 */
extern void helper_be8b(void);

void pcie_trigger_and_call_e90b(void)
{
    XDATA_REG8(0xCC81) = 0x04;
    helper_be8b();
}

/*
 * ext_mem_access_wrapper_e914 - Extended memory access wrapper
 * Address: 0xe914-0xe91a (7 bytes)
 */
void ext_mem_access_wrapper_e914(void)
{
    ext_mem_access_0bc8(0x02, 0x28, 0x05);
}

/*
 * call_bd05_wrapper_e95f - Wrapper for 0xbd05 helper
 * Address: 0xe95f-0xe962 (4 bytes)
 */
extern void helper_bd05(void);

void call_bd05_wrapper_e95f(void)
{
    helper_bd05();
}

/*
 * helper_c1f9 - PCIe channel initialization and status check
 * Address: 0xc1f9-0xc26f (119 bytes)
 *
 * Initializes 12 PCIe channels, sets up transfer parameters,
 * triggers transaction, and waits for completion.
 *
 * Returns:
 *   0x00: Success (flag set or transaction complete)
 *   0xFE: Timeout waiting for status
 *   0xFF: Transaction error
 */
extern void pcie_clear_reg_at_offset(uint8_t offset);
extern void pcie_set_byte_enables(uint8_t byte_en);
extern void pcie_clear_and_trigger(void);
extern uint8_t pcie_get_completion_status(void);
extern void pcie_write_status_complete(void);
extern uint8_t pcie_read_completion_data(void);
extern uint8_t pcie_get_link_speed(void);

uint8_t helper_c1f9(void)
{
    uint8_t i;
    uint8_t val;

    /* Loop 12 times - clear registers 0xB210-0xB21B */
    for (i = 0; i < 12; i++) {
        pcie_clear_reg_at_offset(i);
    }

    /* Check 0x05AE bit 0 and configure 0xB210 */
    val = XDATA8(0x05AE);
    if (val & 0x01) {
        XDATA_REG8(0xB210) = 0x40;
    } else {
        XDATA_REG8(0xB210) = 0;
    }

    /* Write 1 to 0xB213 */
    XDATA_REG8(0xB213) = 0x01;

    /* Set byte enables to 0x0F */
    pcie_set_byte_enables(0x0F);

    /* Copy 32-bit value from 0x05AF to 0xB218 */
    XDATA_REG8(0xB218) = XDATA8(0x05AF);
    XDATA_REG8(0xB219) = XDATA8(0x05B0);
    XDATA_REG8(0xB21A) = XDATA8(0x05B1);
    XDATA_REG8(0xB21B) = XDATA8(0x05B2);

    /* Clear and trigger PCIe transaction */
    pcie_clear_and_trigger();

    /* Wait for completion (poll until non-zero) */
    while (pcie_get_completion_status() == 0) {
        /* Busy-wait */
    }

    /* Write completion status */
    pcie_write_status_complete();

    /* Check 0x05AE bit 0 - if set, return success */
    val = XDATA8(0x05AE);
    if (val & 0x01) {
        return 0;
    }

    /* Poll 0xB296 for status bits */
    while (1) {
        val = XDATA_REG8(0xB296);
        /* Check bit 1 */
        if (val & 0x02) {
            break;  /* Bit 1 set - exit poll loop */
        }
        /* Check bit 0 */
        if (!(val & 0x01)) {
            /* Bit 0 not set - timeout, write 1 and return 0xFE */
            XDATA_REG8(0xB296) = 0x01;
            return 0xFE;
        }
    }

    /* Read completion data and check for errors */
    val = pcie_read_completion_data();
    if (val != 0) {
        return 0xFF;
    }

    /* Check 0xB22D (next byte after completion data) */
    val = XDATA_REG8(0xB22D);
    if (val != 0) {
        return 0xFF;
    }

    /* Check if link status byte equals 4 */
    val = XDATA_REG8(0xB22B);
    if (val != 0x04) {
        return 0xFF;
    }

    /* Get link speed on success */
    (void)pcie_get_link_speed();

    return 0;  /* Success */
}

/*
 * helper_be8b - PCIe/USB mode processing handler
 * Address: 0xbe8b-0xbefa (112 bytes)
 *
 * Checks E302 bits 4-5 to determine mode:
 *   - If both bits set (0x30): Simple path - call helpers and return
 *   - Otherwise: Full processing path with polling loops
 *
 * Disassembly:
 *   be8b: mov dptr, #0xe302
 *   be8e: movx a, @dptr
 *   be8f: anl a, #0x30       ; Mask bits 4-5
 *   be91: mov r7, a          ; Save
 *   be92: swap a
 *   be93: anl a, #0x0f       ; Get bits 4-5 in low nibble
 *   be95: xrl a, #0x03       ; XOR with 3
 *   be97: jz 0xbeeb          ; If bits 4-5 both set, go to alt path
 *   ... (main path with polling loops)
 *   beeb: ... (alt path)
 */
extern void uart_puthex(uint8_t val);
extern void uart_puts(__code const char *str);
extern void cmd_engine_clear(void);
extern uint8_t cmd_check_busy(void);
extern void cmd_start_trigger(void);
extern void cmd_config_e40b(void);

/* Internal helpers already defined earlier in stubs.c */
static void helper_9536(void);
static void helper_b8c3(void);
static void helper_befb(void);

void helper_be8b(void)
{
    uint8_t mode_bits;
    uint8_t val;

    /* Read E302 and check bits 4-5 */
    mode_bits = XDATA_REG8(0xE302) & 0x30;

    /* Extract and check if both bits 4-5 are set (mode_bits >> 4 == 3) */
    if (((mode_bits >> 4) & 0x0F) == 0x03) {
        /* Alternate path - both bits set */
        helper_befb();
        uart_puthex(0);  /* lcall 0x51c7 */
        /* uart_puts - different address than main path */
        return;
    }

    /* Main processing path */
    helper_befb();
    uart_puthex(0);

    /* Additional helper calls */
    cmd_engine_clear();
    helper_b8c3();
    helper_9536();

    /* Poll loop 1: Wait for CC89 bit 1 */
    while (!(XDATA_REG8(0xCC89) & 0x02)) {
        /* Wait */
    }

    /* Configure registers */
    cmd_config_e40b();

    /* E403 = 0 */
    XDATA_REG8(0xE403) = 0x00;
    /* E404 = 0x40 */
    XDATA_REG8(0xE404) = 0x40;
    /* E405: clear bits 0-2, set bits 0 and 2 (value 5) */
    val = XDATA_REG8(0xE405);
    val = (val & 0xF8) | 0x05;
    XDATA_REG8(0xE405) = val;
    /* E402: clear bits 5-7, set bit 5 */
    val = XDATA_REG8(0xE402);
    val = (val & 0x1F) | 0x20;
    XDATA_REG8(0xE402) = val;

    /* Poll loop 2: Wait for cmd_check_busy to return 0 */
    while (cmd_check_busy()) {
        /* Wait */
    }

    /* Start trigger */
    cmd_start_trigger();

    /* Poll loop 3: Wait for E41C bit 0 to clear */
    while (XDATA_REG8(0xE41C) & 0x01) {
        /* Wait */
    }

    /* Set completion flag */
    XDATA_VAR8(0x07DF) = 1;
}

/*
 * helper_bd05 - Timer init and start
 * Address: 0xbd05-0xbd13 (15 bytes)
 *
 * Clears timer init flag and starts timer by writing 4 then 2 to CSR.
 * Implemented as reg_timer_init_and_start() in utils.c.
 */
extern void reg_timer_init_and_start(void);

void helper_bd05(void)
{
    reg_timer_init_and_start();
}

/*
 * cmd_init_and_wait_e459 - Initialize command and wait for completion
 * Address: 0xe459-0xe475 (29 bytes)
 *
 * Original disassembly:
 *   e459: lcall 0xe73a        ; cmd_engine_clear - clear command state
 *   e45c: mov r5, #0x01
 *   e45e: mov r7, #0x0c
 *   e460: lcall 0xdd12        ; cmd_trigger_params(0x0c, 0x01)
 *   e463: lcall 0x95af        ; helper_95af
 *   e466: mov dptr, #0xe422   ; REG_CMD_PARAM
 *   e469: clr a
 *   e46a: movx @dptr, a       ; E422 = 0
 *   e46b: inc dptr
 *   e46c: movx @dptr, a       ; E423 = 0
 *   e46d: inc dptr
 *   e46e: mov a, #0x16
 *   e470: movx @dptr, a       ; E424 = 0x16
 *   e471: inc dptr
 *   e472: mov a, #0x31
 *   e474: movx @dptr, a       ; E425 = 0x31
 *   e475: ljmp 0xe1c6         ; cmd_wait_completion
 */
extern void cmd_engine_clear(void);
extern void helper_95af(void);

void cmd_init_and_wait_e459(void)
{
    /* Clear command state */
    cmd_engine_clear();

    /* Configure with cmd_trigger_params(0x0c, 0x01) */
    cmd_trigger_params(0x0C, 0x01);

    /* Additional setup */
    helper_95af();

    /* Write command parameters to E422-E425 */
    REG_CMD_PARAM = 0x00;      /* E422 */
    REG_CMD_STATUS = 0x00;     /* E423 */
    REG_CMD_ISSUE = 0x16;      /* E424 */
    REG_CMD_TAG = 0x31;        /* E425 */

    /* Wait for command completion */
    cmd_wait_completion();
}

/*
 * helper_95af - Set command status to 0x06
 * Address: 0x95af-0x95b5 (7 bytes)
 *
 * Original disassembly:
 *   95af: mov dptr, #0x07c4  ; G_CMD_STATUS
 *   95b2: mov a, #0x06
 *   95b4: movx @dptr, a      ; G_CMD_STATUS = 0x06
 *   95b5: ret
 */
void helper_95af(void)
{
    G_CMD_STATUS = 0x06;
}

/*
 * pcie_tunnel_init_c00d - PCIe tunnel initialization
 * Address: 0xc00d-0xc088 (~124 bytes)
 *
 * Initializes PCIe tunnel state and clears various status variables.
 * This is a complex function that's called during NVMe initialization.
 */
void pcie_tunnel_init_c00d(void)
{
    /* Check if tunnel already active - skip if G_TUNNEL_ACTIVE is 0 */
    if (XDATA_VAR8(0x06E6) == 0) {
        return;
    }

    /* Clear tunnel active flag */
    XDATA_VAR8(0x06E6) = 0;
    /* Set sequence numbers */
    XDATA_VAR8(0x06E7) = 1;  /* inc dptr, inc a */
    XDATA_VAR8(0x06E8) = 1;  /* inc dptr */
    /* Clear state variables */
    XDATA_VAR8(0x05A7) = 0;
    XDATA_VAR8(0x06EB) = 0;
    XDATA_VAR8(0x05AC) = 0;
    XDATA_VAR8(0x05AD) = 0;
    /* Additional tunnel setup would go here */
}

/*
 * queue_calc_dptr_c44f - Calculate queue data pointer
 * Address: 0xc44f-0xc45e (16 bytes)
 *
 * Calculates: DPTR = 0x057E + (I_WORK_21 * 10)
 * Returns pointer to queue entry data.
 *
 * Disassembly:
 *   c44f: mov a, 0x21       ; A = I_WORK_21 (queue index)
 *   c451: mov 0xf0, #0x0a   ; B = 10 (entry size)
 *   c454: mul ab            ; A = A * 10
 *   c455: add a, #0x7e      ; A += 0x7E (base offset)
 *   c457: mov 0x82, a       ; DPL = A
 *   c459: clr a
 *   c45a: addc a, #0x05     ; DPH = 0x05 + carry
 *   c45c: mov 0x83, a
 *   c45e: ret
 */
#define I_WORK_21 (*(__idata uint8_t *)0x21)

__xdata uint8_t * queue_calc_dptr_c44f(void)
{
    uint16_t addr;
    uint8_t idx = I_WORK_21;

    /* Calculate: 0x057E + (idx * 10) */
    addr = 0x057E + (idx * 10);

    return (__xdata uint8_t *)addr;
}

/*
 * queue_check_status_c4a9 - Check if queue index is within count
 * Address: 0xc4a9-0xc4b2 (10 bytes)
 *
 * Returns 1 if I_WORK_21 < G_QUEUE_COUNT_06E5, else 0.
 * Used for iterating through queue entries.
 *
 * Disassembly:
 *   c4a9: mov a, 0x21       ; A = I_WORK_21 (queue index)
 *   c4ab: mov dptr, #0x06e5 ; G_QUEUE_COUNT_06E5
 *   c4ae: movx a, @dptr     ; A = count
 *   c4af: cjne a, 0x21, +4  ; compare count with index
 *   c4b1: ret               ; if equal, fall through
 *   c4b2: ... (returns 0 or 1 based on carry)
 */
#ifndef G_QUEUE_COUNT_06E5
#define G_QUEUE_COUNT_06E5 XDATA_VAR8(0x06E5)
#endif

uint8_t queue_check_status_c4a9(void)
{
    uint8_t idx = I_WORK_21;
    uint8_t count = G_QUEUE_COUNT_06E5;
    return (idx < count) ? 1 : 0;
}

/*
 * pcie_read_ctrl_b402 - Read PCIe control and clear bit 1
 * Address: 0xccac-0xccb2
 */
#define REG_PCIE_CTRL2_B402 XDATA_REG8(0xB402)

uint8_t pcie_read_ctrl_b402(void)
{
    return REG_PCIE_CTRL2_B402 & 0xFD;
}

/*
 * pcie_lane_disable_e8a9 - Disable lane on param bit 0
 * Address: 0xe8a9-0xe8b4
 */
#define REG_PCIE_LANE_CTRL_C659 XDATA_REG8(0xC659)

void pcie_lane_disable_e8a9(uint8_t param)
{
    if (param & 0x01) {
        REG_PCIE_LANE_CTRL_C659 &= 0xFE;
    }
}

/*
 * timer_phy_config_e57d - Configure PHY timer
 * Address: 0xe57d-0xe596
 */
#define REG_PHY_TIMER_CTRL_E764 XDATA_REG8(0xE764)

void timer_phy_config_e57d(uint8_t param)
{
    uint8_t val;
    if (!(param & 0x01)) return;
    val = REG_PHY_TIMER_CTRL_E764; val &= 0xFD; REG_PHY_TIMER_CTRL_E764 = val;
    val = REG_PHY_TIMER_CTRL_E764; val &= 0xFE; REG_PHY_TIMER_CTRL_E764 = val;
    val = REG_PHY_TIMER_CTRL_E764; val &= 0xF7; REG_PHY_TIMER_CTRL_E764 = val;
    val = REG_PHY_TIMER_CTRL_E764; val = (val & 0xFB) | 0x04; REG_PHY_TIMER_CTRL_E764 = val;
}

/*
 * power_config_d630 - Configure power lanes
 * Address: 0xd630-0xd674
 */
#define REG_POWER_CTRL_B432 XDATA_REG8(0xB432)
#define REG_POWER_LANE_B404 XDATA_REG8(0xB404)

void power_config_d630(uint8_t param)
{
    uint8_t val;
    val = REG_POWER_CTRL_B432; val = (val & 0xF8) | 0x07; REG_POWER_CTRL_B432 = val;
    val = REG_POWER_LANE_B404; val = (val & 0xF0) | (param & 0x0F); REG_POWER_LANE_B404 = val;
}

/*
 * state_update_e25e - Update state registers
 * Address: 0xe25e-0xe281
 */
void state_update_e25e(void)
{
    uint8_t val;
    val = XDATA_REG8(0x78AF); val = (val & 0xBF) | 0x40; XDATA_REG8(0x78AF) = val;
    val = XDATA_REG8(0x78B0); val = (val & 0xBF) | 0x40; XDATA_REG8(0x78B0) = val;
    val = XDATA_REG8(0x78B1); val = (val & 0xBF) | 0x40; XDATA_REG8(0x78B1) = val;
    val = XDATA_REG8(0x78B2); val = (val & 0xBF) | 0x40; XDATA_REG8(0x78B2) = val;
}

extern void pcie_lane_config(uint8_t lane_mask);

/*
 * state_handler_d996 - PCIe state machine handler
 * Address: 0xd996-0xd9d4 (63 bytes)
 *
 * Complex state machine handler that configures PCIe registers
 * and calls multiple helper functions. Called as tail call from
 * transfer_handler_ceab.
 *
 * TODO: Full implementation requires:
 *   - helper_ccac, helper_e8a9(0x0F), helper_e57d
 *   - helper_d630(0x01), helper_d436(0x0F), helper_e25e
 *   - ext_mem_access_0bc8 calls for register configuration
 */
void state_handler_d996(void)
{
    uint8_t val;
    val = pcie_read_ctrl_b402();
    (void)val;
    pcie_lane_disable_e8a9(0x0F);
    timer_phy_config_e57d(0x0F);
    power_config_d630(0x01);
    pcie_lane_config(0x0F);
    state_update_e25e();
    val = XDATA_REG8(0x7041); val &= 0xBF; XDATA_REG8(0x7041) = val;
    val = XDATA_REG8(0x1507); val = (val & 0xFB) | 0x04; XDATA_REG8(0x1507) = val;
    val = XDATA_REG8(0x1507); val = (val & 0xFD) | 0x02; XDATA_REG8(0x1507) = val;
}

/*
 * transfer_handler_ceab - Transfer poll and state handler
 * Address: 0xceab-0xcece (36 bytes)
 *
 * Configures Timer0 with divisor 3, polls E712 for ready status
 * with Timer0 timeout, then triggers state machine handler.
 *
 * Flow:
 *   1. Configure Timer0 with divisor=3 via helper_e50d_full
 *   2. Poll loop:
 *      - Check E712 bit 0 (ready flag)
 *      - Check E712 bit 1 (alternate ready)
 *      - Check CC11 bit 1 (timer expired)
 *   3. Call pcie_trigger_cc11_e8ef to reset timer
 *   4. Call helper_dd42(0) to update state
 *   5. Tail call to state_handler_d996
 *
 * Disassembly:
 *   ceab: mov r7, #0x03
 *   cead: lcall 0xe50d       ; helper_e50d_full(3, 0, 0)
 *   ceb0: mov dptr, #0xe712  ; Poll loop start
 *   ceb3: movx a, @dptr
 *   ceb4: jb 0xe0.0, 0xcec6  ; Exit if bit 0 set
 *   ceb7: movx a, @dptr
 *   ceb8: anl a, #0x02
 *   ceba: mov r7, a
 *   cebb: clr c
 *   cebc: rrc a
 *   cebd: jnz 0xcec6         ; Exit if bit 1 set
 *   cebf: mov dptr, #0xcc11
 *   cec2: movx a, @dptr
 *   cec3: jnb 0xe0.1, 0xceb0 ; Loop if timer not expired
 *   cec6: lcall 0xe8ef       ; pcie_trigger_cc11_e8ef
 *   cec9: clr a
 *   ceca: mov r7, a
 *   cecb: lcall 0xdd42       ; helper_dd42(0)
 *   cece: ljmp 0xd996        ; tail call
 */
#define REG_POLL_STATUS_E712 XDATA_REG8(0xE712)

void transfer_handler_ceab(void)
{
    uint8_t status;

    /* Configure Timer0 with divisor bits = 3 */
    helper_e50d_full(3, 0, 0);

    /* Poll loop: wait for E712 ready or timer timeout */
    while (1) {
        /* Read poll status register */
        status = REG_POLL_STATUS_E712;

        /* Check if bit 0 is set (ready) */
        if (status & 0x01) {
            break;
        }

        /* Check if bit 1 is set (alternate ready) */
        if (status & 0x02) {
            break;
        }

        /* Check timer status - if bit 1 of CC11 is set, timer expired */
        if (REG_TIMER0_CSR & 0x02) {
            break;
        }
    }

    /* Reset timer */
    pcie_trigger_cc11_e8ef();

    /* Update state to 0 */
    helper_dd42(0);

    /* Tail call to state handler */
    state_handler_d996();
}
