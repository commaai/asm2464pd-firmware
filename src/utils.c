/*
 * ASM2464PD Firmware - Core Utility Functions
 *
 * Low-level utility functions for memory access and data manipulation.
 * These are called throughout the firmware for loading parameters from
 * internal RAM (IDATA) and external RAM (XDATA).
 */

#include "utils.h"
#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"
#include "drivers/power.h"
#include "drivers/uart.h"
#include "drivers/cmd.h"
#include "drivers/timer.h"

/* External functions from app layer */
extern void protocol_setup_params(uint8_t r3, uint8_t r5, uint8_t r7);  /* app/protocol.c */

/* Forward declarations for functions defined later in this file */
void ext_mem_bank_access(uint8_t bank, uint8_t addr_hi, uint8_t addr_lo);

/*
 * pcie_short_delay - PCIe short delay with status read
 * Address: 0xbefb-0xbf0e (20 bytes)
 *
 * Performs a delay loop (0x2269FF iterations) then reads PHY mode status.
 * Returns bits 4-5 of 0xE302 in low nibble.
 */
uint8_t pcie_short_delay(void) {
    uint32_t delay;
    uint8_t mode_bits;

    /* Delay loop: 0x2269FF iterations (~2.3M loops) */
    for (delay = 0x2269FF; delay > 0; delay--) {
        /* Empty loop body - just wastes cycles */
    }

    /* Read PHY mode register and extract bits 4-5 */
    mode_bits = REG_PHY_MODE_E302;
    mode_bits = (mode_bits & 0x30) >> 4;  /* Extract bits 4-5, shift to low nibble */

    return mode_bits;
}

/*
 * cmd_engine_wait_idle - Clear command engine state
 * Address: 0xb8c3-0xb8f8 (54 bytes)
 *
 * Performs a delay loop then clears multiple command state variables.
 */
void cmd_engine_wait_idle(void) {
    uint32_t delay;

    /* Delay loop: 0x21E4FF iterations (~2.2M loops) */
    for (delay = 0x21E4FF; delay > 0; delay--) {
        /* Empty loop body - just wastes cycles */
    }

    /* Clear command slot index pair */
    G_CMD_SLOT_INDEX = 0;              /* 0x07B7 */
    *((__xdata uint8_t *)0x07B8) = 0;  /* 0x07B8 */

    /* Clear command state pair */
    G_CMD_STATE = 0;                   /* 0x07C3 */
    *((__xdata uint8_t *)0x07C4) = 0;  /* 0x07C4 */

    /* Clear additional command work variables */
    G_CMD_WORK_C7 = 0;                 /* 0x07C7 */
    G_CMD_WORK_C5 = 0;                 /* 0x07C5 */
    G_CMD_WORK_C2 = 0;                 /* 0x07C2 */
    G_CMD_SLOT_C1 = 0;                 /* 0x07C1 */
    G_CMD_WORK_E3 = 0;                 /* 0x07E3 */

    /* Set flash operation counter to 1 */
    G_FLASH_OP_COUNTER = 1;            /* 0x07BD */
}

/*
 * link_state_init_stub - Initialize link state registers
 * Address: 0x9536-0x9565 (48 bytes)
 *
 * Initializes command engine and link state registers.
 */
void link_state_init_stub(void) {
    /* Write 0xFF to command control registers 0xE40F and 0xE410 */
    REG_CMD_CTRL_E40F = 0xFF;
    REG_CMD_CTRL_E410 = 0xFF;

    /* Clear bits 1, 2, 3 of command config register 0xE40B */
    REG_CMD_CONFIG = REG_CMD_CONFIG & 0xFD;  /* Clear bit 1 */
    REG_CMD_CONFIG = REG_CMD_CONFIG & 0xFB;  /* Clear bit 2 */
    REG_CMD_CONFIG = REG_CMD_CONFIG & 0xF7;  /* Clear bit 3 */

    /* Update transfer DMA control: clear bits 0-2, set bit 1 */
    REG_XFER_DMA_CTRL = (REG_XFER_DMA_CTRL & 0xF8) | 0x02;  /* CC88 */

    /* Clear transfer DMA address low, set to 0xC7 at next byte */
    REG_XFER_DMA_ADDR_LO = 0;          /* CC8A */
    REG_XFER_DMA_ADDR_HI = 0xC7;       /* CC8B */

    /* Set transfer DMA command to 0x01 */
    REG_XFER_DMA_CMD = 0x01;           /* CC89 */
}

/*
 * idata_load_dword - Load 32-bit value from IDATA
 * Address: 0x0d78-0x0d83 (12 bytes)
 *
 * Original function loads 4 bytes from IDATA[@R0] into R4-R7.
 * In C, we return a 32-bit value which SDCC places in R4-R7.
 *
 * Original disassembly:
 *   0d78: mov a, @r0        ; read byte 0
 *   0d79: mov r4, a
 *   0d7a: inc r0
 *   0d7b: mov a, @r0        ; read byte 1
 *   0d7c: mov r5, a
 *   0d7d: inc r0
 *   0d7e: mov a, @r0        ; read byte 2
 *   0d7f: mov r6, a
 *   0d80: inc r0
 *   0d81: mov a, @r0        ; read byte 3
 *   0d82: mov r7, a
 *   0d83: ret
 */
uint32_t idata_load_dword(__idata uint8_t *ptr)
{
    uint32_t val;
    val = ptr[0];
    val |= ((uint32_t)ptr[1]) << 8;
    val |= ((uint32_t)ptr[2]) << 16;
    val |= ((uint32_t)ptr[3]) << 24;
    return val;
}

/*
 * xdata_load_dword - Load 32-bit value from XDATA
 * Address: 0x0d84-0x0d8f (12 bytes)
 *
 * Original function loads 4 bytes from XDATA[DPTR] into R4-R7.
 *
 * Original disassembly:
 *   0d84: movx a, @dptr     ; read byte 0
 *   0d85: mov r4, a
 *   0d86: inc dptr
 *   0d87: movx a, @dptr     ; read byte 1
 *   0d88: mov r5, a
 *   0d89: inc dptr
 *   0d8a: movx a, @dptr     ; read byte 2
 *   0d8b: mov r6, a
 *   0d8c: inc dptr
 *   0d8d: movx a, @dptr     ; read byte 3
 *   0d8e: mov r7, a
 *   0d8f: ret
 */
uint32_t xdata_load_dword(__xdata uint8_t *ptr)
{
    uint32_t val;
    val = ptr[0];
    val |= ((uint32_t)ptr[1]) << 8;
    val |= ((uint32_t)ptr[2]) << 16;
    val |= ((uint32_t)ptr[3]) << 24;
    return val;
}

/*
 * idata_load_dword_alt - Load 32-bit value from IDATA (alternate register allocation)
 * Address: 0x0d90-0x0d9c (13 bytes)
 *
 * Original function loads 4 bytes from IDATA[@R0] into R0-R3.
 * Used for loading secondary parameters.
 *
 * Original disassembly:
 *   0d90: mov a, @r0        ; read byte 0
 *   0d91: mov r3, a
 *   0d92: inc r0
 *   0d93: mov a, @r0        ; read byte 1
 *   0d94: mov r1, a
 *   0d95: inc r0
 *   0d96: mov a, @r0        ; read byte 2
 *   0d97: mov r2, a
 *   0d98: inc r0
 *   0d99: mov a, @r0        ; read byte 3
 *   0d9a: xch a, r3
 *   0d9b: mov r0, a
 *   0d9c: ret
 */
uint32_t idata_load_dword_alt(__idata uint8_t *ptr)
{
    uint32_t val;
    val = ptr[0];
    val |= ((uint32_t)ptr[1]) << 8;
    val |= ((uint32_t)ptr[2]) << 16;
    val |= ((uint32_t)ptr[3]) << 24;
    return val;
}

/*
 * xdata_load_dword_alt - Load 32-bit value from XDATA (alternate register allocation)
 * Address: 0x0d9d-0x0da8 (12 bytes)
 *
 * Original function loads 4 bytes from XDATA[DPTR] into R0-R3.
 *
 * Original disassembly:
 *   0d9d: movx a, @dptr     ; read byte 0
 *   0d9e: mov r0, a
 *   0d9f: inc dptr
 *   0da0: movx a, @dptr     ; read byte 1
 *   0da1: mov r1, a
 *   0da2: inc dptr
 *   0da3: movx a, @dptr     ; read byte 2
 *   0da4: mov r2, a
 *   ... continues
 */
uint32_t xdata_load_dword_alt(__xdata uint8_t *ptr)
{
    uint32_t val;
    val = ptr[0];
    val |= ((uint32_t)ptr[1]) << 8;
    val |= ((uint32_t)ptr[2]) << 16;
    val |= ((uint32_t)ptr[3]) << 24;
    return val;
}

/*
 * idata_store_dword - Store 32-bit value to IDATA
 * Address: 0x0db9-0x0dc4 (12 bytes)
 *
 * Stores R4-R7 (32-bit value) to IDATA[@R0].
 *
 * Original disassembly:
 *   0db9: mov a, r4
 *   0dba: mov @r0, a
 *   0dbb: inc r0
 *   0dbc: mov a, r5
 *   0dbd: mov @r0, a
 *   0dbe: inc r0
 *   0dbf: mov a, r6
 *   0dc0: mov @r0, a
 *   0dc1: inc r0
 *   0dc2: mov a, r7
 *   0dc3: mov @r0, a
 *   0dc4: ret
 */
void idata_store_dword(__idata uint8_t *ptr, uint32_t val)
{
    ptr[0] = (uint8_t)(val & 0xFF);
    ptr[1] = (uint8_t)((val >> 8) & 0xFF);
    ptr[2] = (uint8_t)((val >> 16) & 0xFF);
    ptr[3] = (uint8_t)((val >> 24) & 0xFF);
}

/*
 * xdata_store_dword - Store 32-bit value to XDATA
 * Address: 0x0dc5-0x0dd0 (12 bytes)
 *
 * Stores R4-R7 (32-bit value) to XDATA[DPTR].
 *
 * Original disassembly:
 *   0dc5: mov a, r4
 *   0dc6: movx @dptr, a
 *   0dc7: inc dptr
 *   0dc8: mov a, r5
 *   0dc9: movx @dptr, a
 *   0dca: inc dptr
 *   0dcb: mov a, r6
 *   0dcc: movx @dptr, a
 *   0dcd: inc dptr
 *   0dce: mov a, r7
 *   0dcf: movx @dptr, a
 *   0dd0: ret
 */
void xdata_store_dword(__xdata uint8_t *ptr, uint32_t val)
{
    ptr[0] = (uint8_t)(val & 0xFF);
    ptr[1] = (uint8_t)((val >> 8) & 0xFF);
    ptr[2] = (uint8_t)((val >> 16) & 0xFF);
    ptr[3] = (uint8_t)((val >> 24) & 0xFF);
}

/*
 * =============================================================================
 * 32-bit Math Functions
 *
 * Low-level arithmetic operations using register calling convention:
 *   - First operand:  R4:R5:R6:R7 (MSB:...:LSB)
 *   - Second operand: R0:R1:R2:R3 (MSB:...:LSB)
 *   - Result: R4:R5:R6:R7
 * =============================================================================
 */

/*
 * mul16x16 - 16x16-bit multiplication returning 24-bit result
 * Address: 0x0bfd-0x0c0e (18 bytes)
 *
 * Input: R6:R7 (16-bit multiplicand), R4:R5 (16-bit multiplier)
 * Output: R6:R7 (lower 16 bits), overflow in R0
 */
void mul16x16(void) __naked
{
    __asm
        mov a, r7
        mov b, r5
        mul ab
        mov r0, b
        xch a, r7
        mov b, r4
        mul ab
        add a, r0
        xch a, r6
        mov b, r5
        mul ab
        add a, r6
        mov r6, a
        ret
    __endasm;
}

/*
 * add32 - 32-bit addition
 * Address: 0x0c9e-0x0caa (13 bytes)
 *
 * R4:R5:R6:R7 = R4:R5:R6:R7 + R0:R1:R2:R3
 */
void add32(void) __naked
{
    __asm
        mov a, r7
        add a, r3
        mov r7, a
        mov a, r6
        addc a, r2
        mov r6, a
        mov a, r5
        addc a, r1
        mov r5, a
        mov a, r4
        addc a, r0
        mov r4, a
        ret
    __endasm;
}

/*
 * sub32 - 32-bit subtraction
 * Address: 0x0cab-0x0cb8 (14 bytes)
 *
 * R4:R5:R6:R7 = R4:R5:R6:R7 - R0:R1:R2:R3
 */
void sub32(void) __naked
{
    __asm
        clr c
        mov a, r7
        subb a, r3
        mov r7, a
        mov a, r6
        subb a, r2
        mov r6, a
        mov a, r5
        subb a, r1
        mov r5, a
        mov a, r4
        subb a, r0
        mov r4, a
        ret
    __endasm;
}

/*
 * mul32 - 32-bit multiplication
 * Address: 0x0cb9-0x0d07 (79 bytes)
 *
 * R4:R5:R6:R7 = R4:R5:R6:R7 * R0:R1:R2:R3 (lower 32 bits)
 */
void mul32(void) __naked
{
    __asm
        mov a, r0
        mov b, r7
        mul ab
        xch a, r4

        mov b, r3
        mul ab
        add a, r4
        mov r4, a

        mov a, r1
        mov b, r6
        mul ab
        add a, r4
        mov r4, a

        mov b, r2
        mov a, r5
        mul ab
        add a, r4
        mov r4, a

        mov a, r2
        mov b, r6
        mul ab
        xch a, r5
        mov r0, b

        mov b, r3
        mul ab
        add a, r5
        xch a, r4
        addc a, r0
        add a, b
        mov r5, a

        mov a, r1
        mov b, r7
        mul ab
        add a, r4
        xch a, r5
        addc a, b
        mov r4, a

        mov a, r3
        mov b, r6
        mul ab
        mov r6, a
        mov r1, b

        mov a, r3
        mov b, r7
        mul ab
        xch a, r7
        xch a, b
        add a, r6
        xch a, r5
        addc a, r1
        mov r6, a
        clr a
        addc a, r4
        mov r4, a

        mov a, r2
        mul ab
        add a, r5
        xch a, r6
        addc a, b
        mov r5, a
        clr a
        addc a, r4
        mov r4, a

        ret
    __endasm;
}

/*
 * or32 - 32-bit bitwise OR
 * Address: 0x0d08-0x0d14 (13 bytes)
 *
 * R4:R5:R6:R7 = R4:R5:R6:R7 | R0:R1:R2:R3
 */
void or32(void) __naked
{
    __asm
        mov a, r7
        orl a, r3
        mov r7, a
        mov a, r6
        orl a, r2
        mov r6, a
        mov a, r5
        orl a, r1
        mov r5, a
        mov a, r4
        orl a, r0
        mov r4, a
        ret
    __endasm;
}

/*
 * xor32 - 32-bit bitwise XOR
 * Address: 0x0d15-0x0d21 (13 bytes)
 *
 * R4:R5:R6:R7 = R4:R5:R6:R7 ^ R0:R1:R2:R3
 */
void xor32(void) __naked
{
    __asm
        mov a, r7
        xrl a, r3
        mov r7, a
        mov a, r6
        xrl a, r2
        mov r6, a
        mov a, r5
        xrl a, r1
        mov r5, a
        mov a, r4
        xrl a, r0
        mov r4, a
        ret
    __endasm;
}

/*
 * shl32 - 32-bit shift left
 * Address: 0x0d46-0x0d58 (19 bytes)
 *
 * Shifts R4:R5:R6:R7 left by R0 bits.
 */
void shl32(void) __naked
{
    __asm
        mov a, r0
        jz shl32_done
    shl32_loop:
        mov a, r7
        clr c
        rlc a
        mov r7, a
        mov a, r6
        rlc a
        mov r6, a
        mov a, r5
        rlc a
        mov r5, a
        mov a, r4
        rlc a
        mov r4, a
        djnz r0, shl32_loop
    shl32_done:
        ret
    __endasm;
}

/*
 * load_dword_r4r7 - Load XDATA dword at DPTR into R4-R7 (naked version)
 * Address: 0x0d84-0x0d8f (12 bytes)
 *
 * This is the naked version for inline assembly where DPTR is already set.
 * Does NOT take a parameter - caller must set DPTR before calling.
 */
void load_dword_r4r7(void) __naked
{
    __asm
        movx a, @dptr
        mov  r4, a
        inc  dptr
        movx a, @dptr
        mov  r5, a
        inc  dptr
        movx a, @dptr
        mov  r6, a
        inc  dptr
        movx a, @dptr
        mov  r7, a
        ret
    __endasm;
}

/*
 * load_dword_r0r3 - Load XDATA dword at DPTR into R0-R3 (naked version)
 * Address: 0x0d9d-0x0da8 (12 bytes)
 *
 * This is the naked version for inline assembly where DPTR is already set.
 * Does NOT take a parameter - caller must set DPTR before calling.
 */
void load_dword_r0r3(void) __naked
{
    __asm
        movx a, @dptr
        mov  r0, a
        inc  dptr
        movx a, @dptr
        mov  r1, a
        inc  dptr
        movx a, @dptr
        mov  r2, a
        inc  dptr
        movx a, @dptr
        mov  r3, a
        ret
    __endasm;
}

/*
 * store_dword_r4r7 - Store R4-R7 to XDATA at DPTR (naked version)
 * Address: 0x0dc5-0x0dd0 (12 bytes)
 *
 * This is the naked version for inline assembly where DPTR is already set.
 * Does NOT take a parameter - caller must set DPTR before calling.
 */
void store_dword_r4r7(void) __naked
{
    __asm
        mov  a, r4
        movx @dptr, a
        inc  dptr
        mov  a, r5
        movx @dptr, a
        inc  dptr
        mov  a, r6
        movx @dptr, a
        inc  dptr
        mov  a, r7
        movx @dptr, a
        ret
    __endasm;
}

/*
 * xdata_load_triple - Load 3 bytes from XDATA
 * Address: 0x0ddd-0x0de5 (9 bytes)
 *
 * Loads 3 bytes from XDATA[DPTR] into R3, R2, R1 (24-bit value).
 *
 * Original disassembly:
 *   0ddd: movx a, @dptr     ; read byte 0
 *   0dde: mov r3, a
 *   0ddf: inc dptr
 *   0de0: movx a, @dptr     ; read byte 1
 *   0de1: mov r2, a
 *   0de2: inc dptr
 *   0de3: movx a, @dptr     ; read byte 2
 *   0de4: mov r1, a
 *   0de5: ret
 */
uint32_t xdata_load_triple(__xdata uint8_t *ptr)
{
    uint32_t val;
    val = ptr[0];
    val |= ((uint32_t)ptr[1]) << 8;
    val |= ((uint32_t)ptr[2]) << 16;
    return val;
}

/*
 * xdata_store_triple - Store 3 bytes to XDATA
 * Address: 0x0de6-0x0dee (9 bytes)
 *
 * Stores R3, R2, R1 (24-bit value) to XDATA[DPTR].
 *
 * Original disassembly:
 *   0de6: mov a, r3
 *   0de7: movx @dptr, a
 *   0de8: inc dptr
 *   0de9: mov a, r2
 *   0dea: movx @dptr, a
 *   0deb: inc dptr
 *   0dec: mov a, r1
 *   0ded: movx @dptr, a
 *   0dee: ret
 */
void xdata_store_triple(__xdata uint8_t *ptr, uint32_t val)
{
    ptr[0] = (uint8_t)(val & 0xFF);
    ptr[1] = (uint8_t)((val >> 8) & 0xFF);
    ptr[2] = (uint8_t)((val >> 16) & 0xFF);
}

/*
 * dptr_index_mul - Calculate DPTR offset with multiplication
 * Address: 0x0dd1-0x0ddc (12 bytes)
 *
 * Multiplies A by B (index * element_size) and adds to DPTR.
 * Used for array indexing: DPTR = base + (index * element_size)
 *
 * Original disassembly:
 *   0dd1: mul ab           ; A = A * B (low), B = high
 *   0dd2: add a, 0x82      ; DPL += A
 *   0dd4: mov 0x82, a
 *   0dd6: mov a, 0xf0      ; A = B (high byte)
 *   0dd8: addc a, 0x83     ; DPH += carry + high
 *   0dda: mov 0x83, a
 *   0ddc: ret (falls through to next function)
 */
__xdata uint8_t *dptr_index_mul(__xdata uint8_t *base, uint8_t index, uint8_t element_size)
{
    uint16_t offset = (uint16_t)index * element_size;
    return base + offset;
}

//=============================================================================
// Register Helper Functions (0xbb00-0xbf00)
// These functions manipulate hardware registers with bit operations
//=============================================================================

#include "globals.h"

/* reg_clear_bits_and_init moved to drivers/nvme.c as nvme_clear_bits_and_init */

/*
 * reg_read_indexed_0a84 - Read from indexed register and store to 0x0A84
 * Address: 0xbb4f-0xbb5d (15 bytes)
 *
 * Calculates address 0x70XX where XX = R6 + A, reads that register,
 * stores result to G_ACTION_PARAM_0A84, reads back and returns.
 *
 * Original disassembly:
 *   bb4f: add a, r6
 *   bb50: mov dpl, a
 *   bb52: clr a
 *   bb53: addc a, #0x70
 *   bb55: mov dph, a       ; DPTR = 0x7000 + R6 + A
 *   bb57: movx a, @dptr    ; read register
 *   bb58: mov dptr, #0x0a84
 *   bb5b: movx @dptr, a    ; store to 0x0A84
 *   bb5c: movx a, @dptr    ; read back
 *   bb5d: ret
 */
uint8_t reg_read_indexed_0a84(uint8_t offset, uint8_t base)
{
    __xdata uint8_t *ptr;
    uint8_t val;

    ptr = (__xdata uint8_t *)(0x7000 + base + offset);
    val = *ptr;
    G_ACTION_PARAM_0A84 = val;
    return G_ACTION_PARAM_0A84;
}

/*
 * reg_extract_bit6 - Right rotate and extract bit 6 (becomes bit 0)
 * Address: 0xbb5e-0xbb67 (10 bytes)
 *
 * Rotates A right twice through carry, masks with 0x01, writes to DPTR,
 * reads from 0x707D and returns.
 *
 * Original disassembly:
 *   bb5e: rrc a
 *   bb5f: rrc a
 *   bb60: anl a, #0x01     ; extract what was bit 6
 *   bb62: movx @dptr, a    ; store result
 *   bb63: mov dptr, #0x707d
 *   bb66: movx a, @dptr    ; read from 0x707D
 *   bb67: ret
 */
uint8_t reg_extract_bit6(__xdata uint8_t *dest, uint8_t val)
{
    /* Extract bit 6 by shifting right twice (with carry) and masking */
    val = (val >> 6) & 0x01;
    *dest = val;
    return G_FLASH_CFG_FLAGS;
}

/*
 * reg_set_bits_1_2 - Set bits 1 and 2 in register
 * Address: 0xbb68-0xbb74 (13 bytes)
 *
 * Reads from DPTR, clears bit 1, sets bit 1, writes back.
 * Then reads again, clears bit 2, sets bit 2, writes back.
 *
 * Original disassembly:
 *   bb68: movx a, @dptr
 *   bb69: anl a, #0xfd     ; clear bit 1
 *   bb6b: orl a, #0x02     ; set bit 1
 *   bb6d: movx @dptr, a
 *   bb6e: movx a, @dptr
 *   bb6f: anl a, #0xfb     ; clear bit 2
 *   bb71: orl a, #0x04     ; set bit 2
 *   bb73: movx @dptr, a
 *   bb74: ret
 */
void reg_set_bits_1_2(__xdata uint8_t *reg)
{
    uint8_t val;

    /* Set bit 1 */
    val = *reg;
    val = (val & 0xFD) | 0x02;
    *reg = val;

    /* Set bit 2 */
    val = *reg;
    val = (val & 0xFB) | 0x04;
    *reg = val;
}

/*
 * reg_extract_bit7 - Right rotate and extract bit 7 (becomes bit 0)
 * Address: 0xbb75-0xbb7d (9 bytes)
 *
 * Rotates A right once, masks with 0x01, writes to DPTR,
 * reads from 0x707D and returns.
 */
uint8_t reg_extract_bit7(__xdata uint8_t *dest, uint8_t val)
{
    val = (val >> 7) & 0x01;
    *dest = val;
    return G_FLASH_CFG_FLAGS;
}

/* reg_clear_bit3_link_ctrl moved to drivers/nvme.c as nvme_clear_bit3_link_ctrl */

/*
 * reg_write_dph_r7 - Write R7 to XDATA at address formed from A (high) and R6 (low)
 * Address: 0xbb8f-0xbb95 (7 bytes)
 *
 * Forms address from A (DPH) and R6 (stored from previous call), writes R7 there.
 */
uint8_t reg_write_indexed(uint8_t dph, uint8_t dpl, uint8_t val)
{
    __xdata uint8_t *ptr = (__xdata uint8_t *)((dph << 8) | dpl);
    *ptr = val;
    return dpl + 1;
}

/*
 * reg_extract_bits_6_7 - Extract bits 6-7 (shift right 6, mask with 0x03)
 * Address: 0xbb96-0xbb9f (10 bytes)
 *
 * Rotates A right twice, masks with 0x03, writes to DPTR,
 * reads from 0x707B and returns.
 */
uint8_t reg_extract_bits_6_7(__xdata uint8_t *dest, uint8_t val)
{
    val = (val >> 6) & 0x03;
    *dest = val;
    return G_FLASH_FAN_MODE;
}

/*
 * reg_extract_bit0 - Extract bit 0 and store
 * Address: 0xbba0-0xbba7 (8 bytes)
 *
 * Masks A with 0x01, writes to DPTR, reads from 0x707D and returns.
 */
uint8_t reg_extract_bit0(__xdata uint8_t *dest, uint8_t val)
{
    *dest = val & 0x01;
    return G_FLASH_CFG_FLAGS;
}

/*
 * reg_set_bit6 - Set bit 6 in register (clear first, then set)
 * Address: 0xbba8-0xbbae (7 bytes)
 */
void reg_set_bit6(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xBF) | 0x40;
    *reg = val;
}

/*
 * reg_set_bit1 - Set bit 1 in register (clear first, then set)
 * Address: 0xbbaf-0xbbb5 (7 bytes)
 */
void reg_set_bit1(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xFD) | 0x02;
    *reg = val;
}

/*
 * reg_set_event_flag - Set event control to 4, return with DPTR at 0x0AE2
 * Address: 0xbbb6-0xbbbf (10 bytes)
 */
__xdata uint8_t *reg_set_event_flag(void)
{
    G_EVENT_CTRL_09FA = 0x04;
    return (__xdata uint8_t *)0x0AE2;
}

/*
 * reg_set_bit3 - Set bit 3 in register (clear first, then set)
 * Address: 0xbbc0-0xbbc6 (7 bytes)
 */
void reg_set_bit3(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xF7) | 0x08;
    *reg = val;
}

/*
 * reg_nibble_swap_store - Read from DPTR, store to 0x0A5C, swap nibbles and store to 0x0A5B
 * Address: 0xbc70-0xbc87 (24 bytes)
 *
 * Reads value from DPTR, stores to G_NIBBLE_SWAP_0A5C, then swaps nibbles
 * and combines with value at G_NIBBLE_SWAP_0A5C, storing result to G_NIBBLE_SWAP_0A5B.
 * Returns the combined value.
 */
uint8_t reg_nibble_swap_store(__xdata uint8_t *reg)
{
    uint8_t val, swapped, low_nibble;

    /* Read from register and store to 0x0A5C */
    val = *reg;
    G_NIBBLE_SWAP_0A5C = val;

    /* Read back, swap nibbles, extract low nibble */
    val = G_NIBBLE_SWAP_0A5C;
    swapped = (val >> 4) | (val << 4);  /* swap a */
    low_nibble = swapped & 0x0F;

    /* XOR with original swapped value (extracts high nibble as low) */
    swapped = swapped ^ low_nibble;  /* Now swapped has original low nibble in high position */
    G_NIBBLE_SWAP_0A5C = swapped;

    /* Read 0x0A5B, swap, keep high nibble, OR with low_nibble */
    val = G_NIBBLE_SWAP_0A5B;
    val = (val >> 4) | (val << 4);
    val = (val & 0xF0) | low_nibble;
    G_NIBBLE_SWAP_0A5B = val;

    return G_NIBBLE_SWAP_0A5B;
}

/*
 * reg_read_bank_1235 - Read from bank 0x1235
 * Address: 0xbc88-0xbc8e (7 bytes)
 *
 * Sets R2=0x12, R1=0x35 and jumps to read routine at 0x0bc8.
 * Returns byte read from address 0x1235.
 */
uint8_t reg_read_bank_1235(void)
{
    return REG_BANK_1235;
}

/*
 * reg_read_bank_0200 - Read from bank with R3=2, R2=0, R1=0
 * Address: 0xbc8f-0xbc97 (9 bytes)
 */
uint8_t reg_read_bank_0200(void)
{
    return REG_BANK_0200;
}

/*
 * reg_read_bank_1200 - Read from bank with R3=2, R2=0x12
 * Address: 0xbc98-0xbc9e (7 bytes)
 */
uint8_t reg_read_bank_1200(void)
{
    return REG_BANK_1200;
}

/*
 * reg_read_and_clear_bit3 - Read from bank 0x28xx and clear bit 3
 * Address: 0xbca5-0xbcae (10 bytes)
 */
uint8_t reg_read_and_clear_bit3(uint8_t offset)
{
    __xdata uint8_t *ptr = (__xdata uint8_t *)(0x2800 | offset);
    return *ptr & 0xF7;
}

/*
 * reg_read_bank_1603 - Read from bank 0x1603
 * Address: 0xbcaf-0xbcb7 (9 bytes)
 */
uint8_t reg_read_bank_1603(void)
{
    return REG_BANK_1603;
}

/*
 * reg_nibble_extract - Read from DPTR, extract high nibble, OR with 0x0A5C
 * Address: 0xbcb8-0xbcc3 (12 bytes)
 */
uint8_t reg_nibble_extract(__xdata uint8_t *reg)
{
    uint8_t val, high_nibble;

    val = *reg;
    high_nibble = (val >> 4) & 0x0F;
    val = G_NIBBLE_SWAP_0A5C;
    return val | high_nibble;
}

/*
 * reg_read_bank_1504_clear - Read from bank 0x1504 and clear bits 2-3
 * Address: 0xbcc4-0xbccf (12 bytes)
 */
uint8_t reg_read_bank_1504_clear(void)
{
    return REG_BANK_1504 & 0xF3;
}

/*
 * reg_read_bank_1200_alt - Read from bank 0x1200 (alternate)
 * Address: 0xbcd0-0xbcd6 (7 bytes)
 */
uint8_t reg_read_bank_1200_alt(void)
{
    return REG_BANK_1200;
}

/*
 * reg_read_event_mask - Read from 0x09FA and mask with 0x81
 * Address: 0xbcd7-0xbcdd (7 bytes)
 *
 * Returns bits 0 and 7 of the event control register.
 */
uint8_t reg_read_event_mask(void)
{
    return G_EVENT_CTRL_09FA & 0x81;
}

/*
 * reg_read_bank_1407 - Read from bank 0x1407
 * Address: 0xbcde-0xbce6 (9 bytes)
 */
uint8_t reg_read_bank_1407(void)
{
    return REG_BANK_1407;
}

/* reg_write_and_set_link_bit0 moved to drivers/phy.c as phy_write_and_set_link_bit0 */


/*
 * reg_set_bit5 - Set bit 5 in register at DPTR
 * Address: 0xbd23-0xbd29 (7 bytes)
 */
void reg_set_bit5(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xDF) | 0x20;
    *reg = val;
}

/*
 * reg_clear_bits_5_6 - Clear bits 5 and 6 in register at DPTR
 * Address: 0xbd2a-0xbd32 (9 bytes)
 */
void reg_clear_bits_5_6(__xdata uint8_t *reg)
{
    uint8_t val;

    val = *reg;
    *reg = val & 0xDF;  /* Clear bit 5 */

    val = *reg;
    *reg = val & 0xBF;  /* Clear bit 6 */
}

/*
 * reg_read_cc3e_clear_bit1 - Read from REG_CPU_CTRL_CC3E and clear bit 1
 * Address: 0xbd33-0xbd39 (7 bytes)
 */
uint8_t reg_read_cc3e_clear_bit1(void)
{
    return REG_CPU_CTRL_CC3E & 0xFD;
}

/*
 * reg_set_bit6_generic - Set bit 6 in register at DPTR
 * Address: 0xbd3a-0xbd40 (7 bytes)
 */
void reg_set_bit6_generic(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xBF) | 0x40;
    *reg = val;
}


/* reg_read_link_width moved to drivers/phy.c as phy_read_link_width */
/* reg_read_link_status_e716 moved to drivers/phy.c as phy_read_link_status */

/*
 * reg_read_cpu_mode_next - Read REG_CPU_MODE_NEXT and mask bits 0-4
 * Address: 0xbd57-0xbd5d (7 bytes)
 */
uint8_t reg_read_cpu_mode_next(void)
{
    return REG_CPU_MODE_NEXT & 0x1F;
}

/*
 * reg_set_bit2 - Set bit 2 in register at DPTR
 * Address: 0xbd5e-0xbd64 (7 bytes)
 */
void reg_set_bit2(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xFB) | 0x04;
    *reg = val;
}

/*
 * reg_set_bit7 - Set bit 7 in register at DPTR
 * Address: 0xbd65-0xbd6b (7 bytes)
 */
void reg_set_bit7(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0x7F) | 0x80;
    *reg = val;
}

/* reg_read_phy_mode_lane_config moved to drivers/phy.c as phy_read_mode_lane_config */
/* reg_read_phy_lanes moved to drivers/phy.c as phy_read_lanes */

/*
 * reg_delay_param_setup - Setup delay parameters for bank read
 * Address: 0xbefb-0xbf04 (10 bytes)
 *
 * Sets R3=0xFF, R2=0x22, R1=0x69 and calls bank read at 0x538d.
 */
uint8_t reg_delay_param_setup(void)
{
    /* This calls into bank read routine - returns value from 0xFF2269 */
    return REG_BANK_2269;  /* Simplified - actual routine uses banked memory */
}

/*
 * reg_clear_state_flags - Clear multiple state work flags to 0
 * Address: 0xbf8e-0xbfa2 (21 bytes)
 *
 * Clears G_STATE_WORK_0B3D, G_STATE_CTRL_0B3E, G_XFER_STATE_0AF6,
 * and global at 0x07EE and G_TRANSFER_FLAG_0AF2.
 */
void reg_clear_state_flags(void)
{
    G_STATE_WORK_0B3D = 0;
    G_STATE_CTRL_0B3E = 0;
    G_XFER_STATE_0AF6 = 0;
    G_SYS_FLAGS_07EE = 0;
    G_TRANSFER_FLAG_0AF2 = 0;
}

/*
 * init_sys_flags_07f0 - Initialize system flags at 0x07F0
 * Address: 0x4be6-0x4c03 (30 bytes)
 *
 * Initializes system configuration flags and clears bit 0 of REG_CPU_EXEC_STATUS_3.
 *
 * Original disassembly:
 *   4be6: mov dptr, #0x07f0
 *   4be9: mov a, #0x24
 *   4beb: movx @dptr, a
 *   4bec: inc dptr
 *   4bed: mov a, #0x04
 *   4bef: movx @dptr, a
 *   4bf0: inc dptr
 *   4bf1: mov a, #0x17
 *   4bf3: movx @dptr, a
 *   4bf4: inc dptr
 *   4bf5: mov a, #0x85
 *   4bf7: movx @dptr, a
 *   4bf8: inc dptr
 *   4bf9: clr a
 *   4bfa: movx @dptr, a
 *   4bfb: inc dptr
 *   4bfc: movx @dptr, a
 *   4bfd: mov dptr, #0xcc35
 *   4c00: movx a, @dptr
 *   4c01: anl a, #0xfe
 *   4c03: movx @dptr, a
 */
void init_sys_flags_07f0(void)
{
    G_SYS_FLAGS_07F0 = 0x24;
    G_SYS_FLAGS_07F1 = 0x04;
    G_SYS_FLAGS_07F2 = 0x17;
    G_SYS_FLAGS_07F3 = 0x85;
    G_SYS_FLAGS_07F4 = 0x00;
    G_SYS_FLAGS_07F5 = 0x00;
    REG_CPU_EXEC_STATUS_3 = REG_CPU_EXEC_STATUS_3 & ~CPU_EXEC_STATUS_3_BIT0;
}

/* delay functions moved to drivers/timer.c */

/*
 * cmp32 - 32-bit comparison (check if equal)
 * Address: 0x0d22-0x0d32 (17 bytes)
 *
 * Compares R0:R1:R2:R3 with R4:R5:R6:R7 using register calling convention.
 * Returns 0 in A if equal, non-zero if different.
 *
 * Original disassembly:
 *   0d22: mov a, r3       ; A = R3 (LSB of second operand)
 *   0d23: subb a, r7      ; A = R3 - R7
 *   0d24: mov 0xf0, a     ; B = result
 *   0d26: mov a, r2       ; A = R2
 *   0d27: subb a, r6      ; A = R2 - R6
 *   0d28: orl 0xf0, a     ; B |= result
 *   0d2a: mov a, r1       ; A = R1
 *   0d2b: subb a, r5      ; A = R1 - R5
 *   0d2c: orl 0xf0, a     ; B |= result
 *   0d2e: mov a, r0       ; A = R0 (MSB of second operand)
 *   0d2f: subb a, r4      ; A = R0 - R4
 *   0d30: orl a, 0xf0     ; A |= B
 *   0d32: ret             ; Return A (0 if equal)
 */
uint8_t cmp32(void) __naked
{
    __asm
        mov  a, r3
        subb a, r7
        mov  0xf0, a        ; B = result
        mov  a, r2
        subb a, r6
        orl  0xf0, a        ; B |= result
        mov  a, r1
        subb a, r5
        orl  0xf0, a        ; B |= result
        mov  a, r0
        subb a, r4
        orl  a, 0xf0        ; A |= B (return value: 0 if equal)
        ret
    __endasm;
}

/*
 * code_load_dword - Load 32-bit value from CODE memory
 * Address: 0x0da9-0x0db8 (16 bytes)
 *
 * Reads 4 bytes from CODE memory at DPTR into R0:R1:R2:R3.
 * Uses movc a, @a+dptr to read from code space.
 *
 * Original disassembly:
 *   0da9: clr a             ; A = 0
 *   0daa: movc a, @a+dptr   ; Read byte 0
 *   0dab: mov r0, a
 *   0dac: mov a, #0x01
 *   0dae: movc a, @a+dptr   ; Read byte 1
 *   0daf: mov r1, a
 *   0db0: mov a, #0x02
 *   0db2: movc a, @a+dptr   ; Read byte 2
 *   0db3: mov r2, a
 *   0db4: mov a, #0x03
 *   0db6: movc a, @a+dptr   ; Read byte 3
 *   0db7: mov r3, a
 *   0db8: ret
 */
uint32_t code_load_dword(__code uint8_t *ptr)
{
    uint32_t val;
    val = ptr[0];
    val |= ((uint32_t)ptr[1]) << 8;
    val |= ((uint32_t)ptr[2]) << 16;
    val |= ((uint32_t)ptr[3]) << 24;
    return val;
}

/*
 * pdata_store_dword - Store 32-bit value to PDATA (external RAM via @R0)
 * Address: 0x0e4f-0x0e5a (12 bytes)
 *
 * Stores R4:R5:R6:R7 (32-bit value) to PDATA at @R0 using movx @r0,a.
 * PDATA is the 256-byte page of external RAM addressed via R0/R1.
 *
 * Original disassembly:
 *   0e4f: mov a, r4
 *   0e50: movx @r0, a       ; Store byte 0
 *   0e51: inc r0
 *   0e52: mov a, r5
 *   0e53: movx @r0, a       ; Store byte 1
 *   0e54: inc r0
 *   0e55: mov a, r6
 *   0e56: movx @r0, a       ; Store byte 2
 *   0e57: inc r0
 *   0e58: mov a, r7
 *   0e59: movx @r0, a       ; Store byte 3
 *   0e5a: ret
 */
void pdata_store_dword(__pdata uint8_t *ptr, uint32_t val)
{
    ptr[0] = (uint8_t)(val & 0xFF);
    ptr[1] = (uint8_t)((val >> 8) & 0xFF);
    ptr[2] = (uint8_t)((val >> 16) & 0xFF);
    ptr[3] = (uint8_t)((val >> 24) & 0xFF);
}

/*
 * banked_store_dword - Store 32-bit value to banked XDATA memory
 * Address: 0x0ba9-0x0bc7 (31 bytes)
 *
 * Writes R4:R5:R6:R7 to external memory with bank select via DPX.
 * Input: R1=DPL, R2=DPH, R3=bank (will be adjusted)
 *
 * The bank (R3) is decremented and masked with 0x7F before use.
 * If the adjusted bank >= 0x80, the write is skipped.
 *
 * Original disassembly:
 *   0ba9: mov 0x82, r1      ; DPL = R1
 *   0bab: mov 0x83, r2      ; DPH = R2
 *   0bad: mov 0x93, r3      ; DPX = R3
 *   0baf: dec 0x93          ; DPX--
 *   0bb1: anl 0x93, #0x7f   ; DPX &= 0x7F
 *   0bb4: cjne r3, #0x80, 0x0bb7
 *   0bb7: jnc 0x0bc4        ; Skip if bank >= 0x80
 *   0bb9-0bc3: write R4:R5:R6:R7 to @DPTR
 *   0bc4: mov 0x93, #0x00   ; Reset DPX to 0
 *   0bc7: ret
 */
void banked_store_dword(uint8_t dpl, uint8_t dph, uint8_t bank, uint32_t val)
{
    uint8_t adjusted_bank;
    __xdata uint8_t *ptr;

    /* Adjust bank: decrement and mask with 0x7F */
    adjusted_bank = (bank - 1) & 0x7F;

    /* Only write if adjusted bank < 0x80 (which is always true after mask) */
    /* But the original checks if R3 >= 0x80 after adjustment */
    if (bank < 0x80) {
        /* Set DPX for bank selection */
        DPX = adjusted_bank;

        /* Write to XDATA */
        ptr = (__xdata uint8_t *)((dph << 8) | dpl);
        ptr[0] = (uint8_t)(val & 0xFF);
        ptr[1] = (uint8_t)((val >> 8) & 0xFF);
        ptr[2] = (uint8_t)((val >> 16) & 0xFF);
        ptr[3] = (uint8_t)((val >> 24) & 0xFF);
    }

    /* Reset DPX to 0 */
    DPX = 0x00;
}

/*
 * banked_load_byte - Load single byte from banked XDATA memory
 * Address: 0x0bc8-0x0bd4 (13 bytes)
 *
 * Reads one byte from external memory with bank select via DPX.
 * R3=memory type: 0x01=XDATA, <0x01=IDATA, 0xFE=PDATA
 *
 * Original disassembly:
 *   0bc8: cjne r3, #0x01, 0x0bd1
 *   0bcb: mov 0x82, r1      ; DPL = R1
 *   0bcd: mov 0x83, r2      ; DPH = R2
 *   0bcf: movx a, @dptr     ; Read from XDATA
 *   0bd0: ret
 *   0bd1: jnc 0x0bd5        ; If R3 > 0x01
 *   0bd3: mov a, @r1        ; Read from IDATA
 *   0bd4: ret
 */
uint8_t banked_load_byte(uint8_t addrlo, uint8_t addrhi, uint8_t memtype)
{
    if (memtype == 0x01) {
        /* XDATA access */
        __xdata uint8_t *ptr = (__xdata uint8_t *)((addrhi << 8) | addrlo);
        return *ptr;
    } else if (memtype < 0x01) {
        /* IDATA access - use addrlo as pointer */
        return *((__idata uint8_t *)addrlo);
    } else if (memtype == 0xFE) {
        /* PDATA access */
        return *((__pdata uint8_t *)addrlo);
    }
    /* Invalid memory type */
    return 0;
}

/*
 * banked_store_byte - Store byte to memory based on memory type
 * Address: 0x0be6-0x0bfc (23 bytes)
 *
 * Stores A to memory at address R2:R1 based on memory type in R3.
 * R3=0x00: IDATA, R3=0x01: XDATA, R3=0xFE: PDATA, R3=0xFF: no-op
 * Other R3 values: banked XDATA store via 0x0adc
 *
 * Original disassembly:
 *   0be6: cjne r3, #0x01, 0x0bef  ; if R3 != 1, check other types
 *   0be9: mov 0x82, r1            ; DPL = R1
 *   0beb: mov 0x83, r2            ; DPH = R2
 *   0bed: movx @dptr, a           ; Store to XDATA
 *   0bee: ret
 *   0bef: jnc 0x0bf3              ; if R3 > 1, check PDATA
 *   0bf1: mov @r1, a              ; Store to IDATA
 *   0bf2: ret
 *   0bf3: cjne r3, #0xfe, 0x0bf8  ; if R3 != 0xFE, check banked
 *   0bf6: movx @r1, a             ; Store to PDATA
 *   0bf7: ret
 *   0bf8: jnc 0x0bf7              ; if R3 == 0xFF, just return
 *   0bfa: ljmp 0x0adc             ; Otherwise banked store
 */
void banked_store_byte(uint8_t addrlo, uint8_t addrhi, uint8_t memtype, uint8_t val)
{
    if (memtype == 0x01) {
        /* XDATA access */
        __xdata uint8_t *ptr = (__xdata uint8_t *)((addrhi << 8) | addrlo);
        *ptr = val;
    } else if (memtype == 0x00) {
        /* IDATA access */
        *((__idata uint8_t *)addrlo) = val;
    } else if (memtype == 0xFE) {
        /* PDATA access */
        *((__pdata uint8_t *)addrlo) = val;
    } else if (memtype == 0xFF) {
        /* No-op */
        return;
    } else {
        /* Banked XDATA store: set DPX from memtype, store to XDATA */
        uint8_t adjusted_bank = (memtype - 1) & 0x7F;
        if (memtype < 0x80) {
            __xdata uint8_t *ptr;
            DPX = adjusted_bank;
            ptr = (__xdata uint8_t *)((addrhi << 8) | addrlo);
            *ptr = val;
            DPX = 0x00;
        }
    }
}

/*
 * banked_store_and_load_bc9f - Store value then load from banked memory
 * Address: 0xbc9f-0xbca4 (6 bytes)
 *
 * Stores the value in A via banked_store_byte then jumps to banked_load_byte.
 * This is a helper used in register configuration sequences.
 *
 * Original disassembly:
 *   bc9f: lcall 0x0be6        ; banked_store_byte
 *   bca2: ljmp 0x0bc8         ; banked_load_byte
 */
void banked_store_and_load_bc9f(uint8_t val)
{
    /* Store via banked_store_byte with R1/R2/R3 as set by caller */
    /* For now, use default bank 0x01 like the calling code does */
    banked_store_byte(0, 0, 0x01, val);
    /* Would then call banked_load_byte but caller doesn't use return value */
}

/*
 * banked_multi_store_bc63 - Multi-byte banked store sequence
 * Address: 0xbc63-0xbc6f (13 bytes)
 *
 * Performs a multi-byte store sequence with incrementing addresses.
 * R1 is incremented between stores.
 *
 * Original disassembly:
 *   bc63: inc r1              ; R1++
 *   bc64: lcall 0x0be6        ; banked_store_byte
 *   bc67: inc r1              ; R1++
 *   bc68: lcall 0x0bc8        ; banked_load_byte
 *   bc6b: anl a, #0xe0        ; mask with 0xE0
 *   bc6d: ljmp 0x0be6         ; banked_store_byte
 */
void banked_multi_store_bc63(uint8_t val)
{
    uint8_t tmp;

    /* inc r1, store val */
    banked_store_byte(0x01, 0, 0x01, val);

    /* inc r1, load, mask with 0xE0, store */
    tmp = banked_load_byte(0x02, 0, 0x01);
    tmp &= 0xE0;
    banked_store_byte(0x02, 0, 0x01, tmp);
}

/*
 * table_search_dispatch_alt - Table-driven dispatch with 8-bit key
 * Address: 0x0def-0x0e14 (38 bytes)
 *
 * Similar to table_search_dispatch but uses single-byte key in A/R0.
 * Table format (3 bytes per entry):
 *   Bytes 0-1: Target address (hi, lo)
 *   Byte 2: Key to match with R0
 *
 * End-of-table marker:
 *   Bytes 0-1: 0x00, 0x00
 *   Bytes 2-3: Default target address (hi, lo)
 *
 * Original disassembly:
 *   0def: pop 0x83          ; DPH = return_addr_hi
 *   0df1: pop 0x82          ; DPL = return_addr_lo
 *   0df3: mov r0, a         ; R0 = A (key to search for)
 *   ; Loop
 *   0df4: clr a
 *   0df5: movc a, @a+dptr   ; Read table[0]
 *   0df6: jnz 0x0e0a        ; if != 0, check key
 *   0df8: mov a, #0x01
 *   0dfa: movc a, @a+dptr   ; Read table[1]
 *   0dfb: jnz 0x0e0a        ; if != 0, check key
 *   ; End marker - jump to default
 *   0dfd: inc dptr
 *   0dfe: inc dptr
 *   ; Read target and jump
 *   0dff: movc a, @a+dptr   ; A = target_hi
 *   0e00: mov r0, a
 *   0e01: mov a, #0x01
 *   0e03: movc a, @a+dptr   ; A = target_lo
 *   0e04: mov 0x82, a       ; DPL = target_lo
 *   0e06: mov 0x83, r0      ; DPH = target_hi
 *   0e08: clr a
 *   0e09: jmp @a+dptr       ; Jump to target
 *   ; Check key match
 *   0e0a: mov a, #0x02
 *   0e0c: movc a, @a+dptr   ; A = key
 *   0e0d: xrl a, r0         ; Compare with R0
 *   0e0e: jz 0x0dff         ; Match!
 *   ; No match - skip to next entry
 *   0e10: inc dptr          ; 3x inc
 *   0e11: inc dptr
 *   0e12: inc dptr
 *   0e13: sjmp 0x0df4       ; Loop back
 */
void table_search_dispatch_alt(void) __naked
{
    __asm
        ; Pop return address into DPTR (points to table)
        pop  dph            ; 0x83
        pop  dpl            ; 0x82
        mov  r0, a          ; R0 = key (passed in A)

    _tsda_loop:
        ; Check for end-of-table marker (0x00, 0x00)
        clr  a
        movc a, @a+dptr     ; Read table[0]
        jnz  _tsda_check_key
        mov  a, #0x01
        movc a, @a+dptr     ; Read table[1]
        jnz  _tsda_check_key

        ; End marker found - skip to default address
        inc  dptr
        inc  dptr

    _tsda_jump:
        ; Read 2-byte target address and jump
        ; A is 0 here (from xrl match or end-marker read)
        movc a, @a+dptr     ; A = target_hi
        mov  r0, a          ; R0 = target_hi
        mov  a, #0x01
        movc a, @a+dptr     ; A = target_lo
        mov  dpl, a         ; DPL = target_lo
        mov  dph, r0        ; DPH = target_hi
        clr  a
        jmp  @a+dptr        ; Jump to target

    _tsda_check_key:
        ; Compare table[2] with R0
        mov  a, #0x02
        movc a, @a+dptr     ; A = key from table
        xrl  a, r0          ; Compare with R0
        jz   _tsda_jump     ; Match!

    _tsda_next:
        ; No match - advance to next 3-byte entry
        inc  dptr
        inc  dptr
        inc  dptr
        sjmp _tsda_loop
    __endasm;
}

/*
 * table_search_dispatch - Table-driven dispatch based on R4:R5:R6:R7 key
 * Address: 0x0e15-0x0e4e (58 bytes)
 *
 * This function pops the return address from the stack and uses it as a
 * pointer to a dispatch table in CODE memory. It searches for an entry
 * matching the key in R4:R5:R6:R7 and jumps to the corresponding target.
 *
 * Table format (6 bytes per entry):
 *   Bytes 0-1: Target address (hi, lo)
 *   Bytes 2-5: Key to match with R4:R5:R6:R7
 *
 * End-of-table marker:
 *   Bytes 0-1: 0x00, 0x00
 *   Bytes 2-3: Default target address (hi, lo)
 *
 * Algorithm:
 *   1. Pop return address (points to table after LCALL)
 *   2. For each entry:
 *      - If entry[0:1] == 0x0000: reached end, jump to default (entry[2:3])
 *      - If entry[2:5] matches R4:R5:R6:R7: jump to entry[0:1]
 *      - Else: advance to next entry (DPTR += 6)
 *   3. Loop until match or end
 *
 * Original disassembly:
 *   0e15: pop 0x83         ; DPH = return_addr_hi
 *   0e17: pop 0x82         ; DPL = return_addr_lo
 *   ; Loop start
 *   0e19: clr a
 *   0e1a: movc a, @a+dptr  ; A = table[0]
 *   0e1b: jnz 0x0e2f       ; if != 0, check key
 *   0e1d: mov a, #0x01
 *   0e1f: movc a, @a+dptr  ; A = table[1]
 *   0e20: jnz 0x0e2f       ; if != 0, check key
 *   ; End marker found - go to default
 *   0e22: inc dptr         ; skip 0x00
 *   0e23: inc dptr         ; skip 0x00
 *   ; Read target and jump (shared with match path)
 *   0e24: movc a, @a+dptr  ; A = target_hi (A was 0 from movc)
 *   0e25: mov r0, a
 *   0e26: mov a, #0x01
 *   0e28: movc a, @a+dptr  ; A = target_lo
 *   0e29: mov 0x82, a      ; DPL = target_lo
 *   0e2b: mov 0x83, r0     ; DPH = target_hi
 *   0e2d: clr a
 *   0e2e: jmp @a+dptr      ; Jump to target
 *   ; Check key match
 *   0e2f: mov a, #0x02
 *   0e31: movc a, @a+dptr  ; A = key[0]
 *   0e32: xrl a, r4        ; Compare with R4
 *   0e33: jnz 0x0e47       ; No match
 *   0e35: mov a, #0x03
 *   0e37: movc a, @a+dptr  ; A = key[1]
 *   0e38: xrl a, r5        ; Compare with R5
 *   0e39: jnz 0x0e47       ; No match
 *   0e3b: mov a, #0x04
 *   0e3d: movc a, @a+dptr  ; A = key[2]
 *   0e3e: xrl a, r6        ; Compare with R6
 *   0e3f: jnz 0x0e47       ; No match
 *   0e41: mov a, #0x05
 *   0e43: movc a, @a+dptr  ; A = key[3]
 *   0e44: xrl a, r7        ; Compare with R7
 *   0e45: jz 0x0e24        ; Match! Go read target
 *   ; No match - skip to next entry
 *   0e47: inc dptr         ; 6x inc to skip entry
 *   0e48: inc dptr
 *   0e49: inc dptr
 *   0e4a: inc dptr
 *   0e4b: inc dptr
 *   0e4c: inc dptr
 *   0e4d: sjmp 0x0e19      ; Loop back
 */
void table_search_dispatch(void) __naked
{
    __asm
        ; Pop return address into DPTR (points to table)
        pop  dph            ; 0x83
        pop  dpl            ; 0x82

    _tsd_loop:
        ; Check for end-of-table marker (0x00, 0x00)
        clr  a
        movc a, @a+dptr     ; Read table[0]
        jnz  _tsd_check_key ; If not 0, check key match
        mov  a, #0x01
        movc a, @a+dptr     ; Read table[1]
        jnz  _tsd_check_key ; If not 0, check key match

        ; End marker found - skip to default address
        inc  dptr
        inc  dptr

    _tsd_jump:
        ; Read 2-byte target address and jump
        ; Note: A is 0 here (either from end-marker path where table[1]=0,
        ;       or from match path where XOR result = 0)
        movc a, @a+dptr     ; A = target_hi
        mov  r0, a
        mov  a, #0x01
        movc a, @a+dptr     ; A = target_lo
        mov  dpl, a         ; DPL = target_lo
        mov  dph, r0        ; DPH = target_hi
        clr  a
        jmp  @a+dptr        ; Jump to target

    _tsd_check_key:
        ; Compare table[2:5] with R4:R5:R6:R7
        mov  a, #0x02
        movc a, @a+dptr     ; A = key[0]
        xrl  a, r4          ; Compare with R4
        jnz  _tsd_next      ; No match

        mov  a, #0x03
        movc a, @a+dptr     ; A = key[1]
        xrl  a, r5          ; Compare with R5
        jnz  _tsd_next      ; No match

        mov  a, #0x04
        movc a, @a+dptr     ; A = key[2]
        xrl  a, r6          ; Compare with R6
        jnz  _tsd_next      ; No match

        mov  a, #0x05
        movc a, @a+dptr     ; A = key[3]
        xrl  a, r7          ; Compare with R7
        jz   _tsd_jump      ; Match! A=0, jump to target

    _tsd_next:
        ; No match - advance to next 6-byte entry
        inc  dptr
        inc  dptr
        inc  dptr
        inc  dptr
        inc  dptr
        inc  dptr
        sjmp _tsd_loop
    __endasm;
}



/* PCIe transaction functions moved to drivers/pcie.c */

void dptr_setup_stub(void)
{
    /* This is a DPTR setup continuation - callers handle this inline */
}

void dptr_calc_ce40_indexed(uint8_t a, uint8_t b)
{
    (void)a; (void)b;
    /* Sets DPTR = 0xCE40 + index - callers should use helper_15ef_ptr() */
}

void dptr_calc_ce40_param(uint8_t param)
{
    (void)param;
    /* Sets DPTR = 0xCE40 + param - callers should use helper_15f1_ptr() */
}

/*
 * get_ep_config_indexed - Get endpoint config value with array index calculation
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
uint8_t get_ep_config_indexed(void)
{
    uint8_t idx = G_SYS_STATUS_SECONDARY;
    uint16_t addr = 0x054E + ((uint16_t)idx * 0x14);
    return XDATA_REG8(addr);
}

/*
 * addr_setup_0059 - Set up address pointer (0x59 + offset)
 * Address: 0x1752-0x175c (11 bytes)
 *
 * Sets DPTR = 0x0059 + offset (in low memory / IDATA space).
 * Used for accessing work variables at 0x59-0x7F.
 */
uint8_t addr_setup_0059(uint8_t offset)
{
    return *(__idata uint8_t *)(0x59 + offset);
}

/*
 * mem_write_via_ptr - Write value to XDATA via DPTR
 * Address: 0x159f (1 byte - just movx @dptr, a instruction)
 *
 * This is not a standalone function in the original firmware - it's
 * typically inlined. SDCC handles XDATA writes directly.
 * Kept for documentation purposes.
 */
void mem_write_via_ptr(uint8_t value)
{
    (void)value;
    /* Writes are handled inline by SDCC via XDATA macros */
}

void dptr_calc_work43(void)
{
    /* DPTR = 0x007C + I_CMD_SLOT_INDEX - handled inline by callers */
}

/*
 * get_sys_status_ptr_0456 - Calculate DPTR address 0x0456 + param
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
__xdata uint8_t * get_sys_status_ptr_0456(uint8_t param)
{
    return (__xdata uint8_t *)(0x0456 + param);
}

/*
 * get_sys_status_ptr_0400 - Calculate DPTR address 0x0400 + param (mid-entry of 16e9)
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
__xdata uint8_t * get_sys_status_ptr_0400(uint8_t param)
{
    return (__xdata uint8_t *)(0x0400 + param);
}

/*
 * dma_queue_ptr_setup - DMA/queue pointer helper used by SCSI path
 * Address: 0x173b (entry point only)
 *
 * The original firmware tail-calls into a small routine that prepares
 * DPTR before issuing a DMA-related request. The exact side effects are
 * still unknown, but we stub it out so the firmware links and higher
 * level logic can continue to be reverse engineered.
 */
void dma_queue_ptr_setup(void)
{
    /* TODO: Implement once behavior at 0x173b is understood. */
}

/*
 * xdata_read_0100 - Set DPTR and read from XDATA
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
uint8_t xdata_read_0100(uint8_t low_byte, uint8_t carry)
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
    return G_DMA_ADDR_HI;
}

uint8_t load_triple_1564_read(void)
{
    return G_DMA_ADDR_HI;
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
 * usb_buf_ptr_0108 - Get USB buffer pointer at 0x0108 + offset
 * Address: 0x1b2e-0x1b37 (10 bytes)
 */
__xdata uint8_t * usb_buf_ptr_0108(uint8_t param)
{
    return (__xdata uint8_t *)(0x0108 + param);
}

/*
 * usb_buf_ptr_0100 - Get USB buffer pointer at 0x0100 + offset
 * Address: 0x1b30-0x1b37 (8 bytes)
 */
__xdata uint8_t * usb_buf_ptr_0100(uint8_t param)
{
    return (__xdata uint8_t *)(0x0100 + param);
}

/*
 * xdata_ptr_from_param - Calculate DPTR address from param
 * Address: 0x1c13-0x1c1a (8 bytes)
 */
__xdata uint8_t * xdata_ptr_from_param(uint8_t param)
{
    return (__xdata uint8_t *)(uint16_t)param;
}

uint8_t protocol_helper_setup(void)
{
    protocol_setup_params(0, 0x20, 5);
    return 5;
}

/*
 * math_sub32 - 32-bit subtraction: R4-R7 = R4-R7 - R0-R3
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
uint8_t math_sub32(uint8_t r0, uint8_t r1, uint8_t r6, uint8_t r7) {
    /* This is a 32-bit subtraction helper used by calling code
     * The actual implementation manipulates R4-R7 registers directly
     * In C, we return a dummy value as the real work is done via registers */
    (void)r0; (void)r1; (void)r6; (void)r7;
    return 0;  /* Actual result is in R4-R7 registers */
}

/* pcie_txn_table_lookup moved to drivers/pcie.c */

/*
 * reg_poll - Register dispatch table polling
 */
void reg_poll(uint8_t param)
{
    (void)param;
}

/*
 * dispatch_event_e0d9 - Dispatch to event handler via address lookup
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
void dispatch_event_e0d9(uint8_t param)
{
    (void)param;

    /* This dispatches to the handler at 0xe0d9 */
    /* The actual dispatch mechanism uses DPTR and indirect jump */
    /* For C implementation, we would call the handler directly */
    /* TODO: implement handler_e0d9 call when available */
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
 * ext_mem_read_stub - Extended memory read stub
 * Address: 0xbc57
 *
 * Stub implementation - actual read would access extended memory.
 */
void ext_mem_read_stub(uint8_t r3, uint8_t r2, uint8_t r1)
{
    (void)r3; (void)r2; (void)r1;
    /* Extended memory read - stub */
}

/*
 * dptr_calc_work53_x4 - Calculate address 0x0477 + (IDATA 0x53 * 4)
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
void dptr_calc_work53_x4(void)
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
 * ep_config_write_calc - Write param and calculate endpoint address
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
void ep_config_write_calc(uint8_t param)
{
    /* The param would be written to whatever DPTR the caller set up.
     * Based on context, this is likely writing to an endpoint config area.
     * Then it calculates address 0x044E + G_SYS_STATUS_PRIMARY */
    uint8_t status = G_SYS_STATUS_PRIMARY;
    uint16_t addr = 0x044E + status;

    /* Write param to the computed address (approximation of the behavior) */
    XDATA_VAR8(addr) = param;
}

/*
 * dptr_calc_04b7_work23 - Calculate address 0x04B7 + IDATA[0x23]
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
void dptr_calc_04b7_work23(void)
{
    /* This sets up DPTR for subsequent read/write in the caller.
     * In C, subsequent operations need to explicitly use the address. */
    __idata uint8_t *work23 = (__idata uint8_t *)0x23;
    uint16_t addr = 0x04B7 + (*work23);
    /* The caller typically reads from this address next */
    (void)addr;
}

/*
 * xdata_read_044e - Get pointer to 0x044E + offset
 * Address: 0x165e-0x1667 (10 bytes)
 *
 * Adds 0x4E to offset and returns address in 0x04XX range.
 * In original assembly this sets DPTR; in C we return a pointer.
 * The return value from the pointer dereference is what callers expect.
 */
uint8_t xdata_read_044e(uint8_t param)
{
    uint8_t low = param + 0x4E;
    uint16_t addr = 0x0400 + low;
    if (low < 0x4E) {
        addr += 0x0100;  /* Handle overflow carry */
    }
    return *(__xdata uint8_t *)addr;
}

/*
 * xdata_write_0400 - Set up address and write to it
 * Address: 0x1660-0x1667 (8 bytes)
 *
 * This is entry 0x1660 which expects A already contains the computed offset.
 * First param is the offset (after add 0x4E), second is written to @DPTR.
 * Actually based on call site: xdata_write_0400(status + 0x4E, r6_val)
 * - param1 = computed offset
 * - param2 = value to write to that address
 */
void xdata_write_0400(uint8_t offset, uint8_t value)
{
    uint16_t addr = 0x0400 + offset;
    if (offset < 0x4E) {
        addr += 0x0100;  /* Handle potential carry */
    }
    *(__xdata uint8_t *)addr = value;
}

/*
 * ep_queue_ctrl_set_84 - Set endpoint queue control to 0x84
 * Address: 0x1cc1-0x1cc7 (7 bytes)
 */
void ep_queue_ctrl_set_84(void)
{
    G_EP_QUEUE_CTRL = 0x84;
}

/*
 * buf_addr_read16 - Read 16-bit buffer address
 * Address: 0x1ba5-0x1bad (9 bytes)
 *
 * Returns 16-bit buffer address (G_BUF_ADDR_HI:G_BUF_ADDR_LO).
 */
uint16_t buf_addr_read16(void)
{
    return ((uint16_t)G_BUF_ADDR_HI << 8) | G_BUF_ADDR_LO;
}

void mul_add_index(uint8_t param1, uint8_t param2)
{
    (void)param1;
    (void)param2;
}

/*
 * calc_chain_stub - Part of calculation chain
 * Address: 0x1bd7-0x1bdb (5 bytes)
 *
 * Mid-function entry for arithmetic chain.
 */
void calc_chain_stub(void)
{
    /* Part of calculation chain - context-dependent */
}

/*
 * core_state_sub16 - Subtract 16-bit value from core state
 * Address: 0x1c6d-0x1c76 (10 bytes)
 *
 * Subtracts r6:r7 from IDATA[0x16:0x17].
 * Note: IDATA stores in lo:hi order, params in hi:lo.
 */
void core_state_sub16(uint8_t hi, uint8_t lo)
{
    uint8_t temp = I_CORE_STATE_H;
    uint8_t borrow = (lo > temp) ? 1 : 0;
    I_CORE_STATE_H = temp - lo;
    I_CORE_STATE_L = I_CORE_STATE_L - hi - borrow;
}

uint8_t param_stub(uint8_t p1, uint16_t p2)
{
    (void)p1;
    (void)p2;
    return 0;
}

/*
 * state_ptr_calc_014e - Calculate state pointer from I_CMD_SLOT_INDEX
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
 * Returns pointer to XDATA at (0x014e + I_CMD_SLOT_INDEX).
 */
__xdata uint8_t * state_ptr_calc_014e(void)
{
    return (__xdata uint8_t *)(0x014e + I_CMD_SLOT_INDEX);
}

void power_config_wrapper(void)
{
    power_config_init();
}

void power_check_wrapper(void)
{
    power_check_status_e647();
}

/*
 * tlp_status_clear - Clear TLP status and setup transaction table
 * Address: 0x1d43-0x1d61+ (complex)
 *
 * Clears G_TLP_STATUS and performs table address calculation.
 */
void tlp_status_clear(void)
{
    G_TLP_STATUS = 0;
    /* Additional setup performed via helper calls */
}

/* nvme_status_update moved to drivers/nvme.c */

/*
 * nvme_dev_status_hi - Read NVMe device status high bits
 * Address: 0x1c56-0x1c5c (7 bytes)
 *
 * Returns REG_NVME_DEV_STATUS & 0xC0 (top 2 bits).
 */
uint8_t nvme_dev_status_hi(void)
{
    return REG_NVME_DEV_STATUS & 0xC0;
}

/*
 * usb_index_add_wrap - Add to USB index counter with 5-bit wrap
 * Address: 0x1d39-0x1d42 (10 bytes)
 *
 * Adds param to G_USB_INDEX_COUNTER, masks to 5 bits.
 */
void usb_index_add_wrap(uint8_t param)
{
    G_USB_INDEX_COUNTER = (G_USB_INDEX_COUNTER + param) & 0x1F;
}

/*
 * core_state_read16 - Read 16-bit core state
 * Address: 0x1b77-0x1b7d (7 bytes)
 *
 * Returns I_CORE_STATE_L:I_CORE_STATE_H as 16-bit value.
 */
uint16_t core_state_read16(void)
{
    return ((uint16_t)I_CORE_STATE_L << 8) | I_CORE_STATE_H;
}

uint8_t flash_extract_bit_shift2(uint8_t val, __xdata uint8_t *dest)
{
    /* rrc a (x2), anl a, #0x01, movx @dptr, a, return G_FLASH_CFG_FLAGS */
    val = (val >> 2) & 0x01;
    *dest = val;
    return G_FLASH_CFG_FLAGS;
}

void flash_set_bit2(__xdata uint8_t *dest)
{
    /* movx a, @dptr; anl a, #0xfb; orl a, #0x04; movx @dptr, a */
    uint8_t val = *dest;
    val = (val & 0xFB) | 0x04;
    *dest = val;
}

uint8_t flash_extract_bit_shift1(uint8_t val, __xdata uint8_t *dest)
{
    /* rrc a, anl a, #0x01, movx @dptr, a, return G_FLASH_CFG_FLAGS */
    val = (val >> 1) & 0x01;
    *dest = val;
    return G_FLASH_CFG_FLAGS;
}

uint8_t flash_extract_2bits_shift2(uint8_t val, __xdata uint8_t *dest)
{
    /* rrc a (x2), anl a, #0x03, movx @dptr, a, return G_FLASH_FAN_MODE */
    val = (val >> 2) & 0x03;
    *dest = val;
    return G_FLASH_FAN_MODE;
}

uint8_t flash_extract_bit0(uint8_t val, __xdata uint8_t *dest)
{
    /* anl a, #0x01, movx @dptr, a, return G_FLASH_CFG_FLAGS */
    val = val & 0x01;
    *dest = val;
    return G_FLASH_CFG_FLAGS;
}

void flash_set_bit3(__xdata uint8_t *dest)
{
    /* movx a, @dptr; anl a, #0xf7; orl a, #0x08; movx @dptr, a */
    uint8_t val = *dest;
    val = (val & 0xF7) | 0x08;
    *dest = val;
}

/*
 * state_checksum_calc - State checksum/validation helper
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
void state_checksum_calc(void)
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
        checksum ^= G_STATE_CHECKSUM_DATA[i];
    }

    /* Store result at 0x0240 */
    G_STATE_CHECKSUM = checksum;
}

/*
 * dispatch_nop_stub - Bank-switching trampoline to NOP space (empty)
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
void dispatch_nop_stub(void)
{
    /* Target is NOP space - intentionally empty */
}

/*
 * nvme_cmd_param_hi - Read NVMe command param high bits
 * Address: 0x1c77-0x1c7d (7 bytes)
 *
 * Returns REG_NVME_CMD_PARAM & 0xE0 (top 3 bits).
 */
uint8_t nvme_cmd_param_hi(void)
{
    return REG_NVME_CMD_PARAM & 0xE0;
}

/*
 * dispatch_nop_stub2 - Bank-switching trampoline to NOP space (empty)
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
void dispatch_nop_stub2(void)
{
    /* Target is NOP space - intentionally empty */
}

/*
 * queue_param_buf_calc - Setup queue parameter and calculate buffer offset
 * Address: 0x1aad-0x1acd (33 bytes)
 *
 * Stores param to G_EP_QUEUE_PARAM, then calculates:
 *   offset = G_BUF_BASE + (G_USB_PARAM_0B00 * 0x40)
 * And stores the result to G_BUF_OFFSET_HI:G_BUF_OFFSET_LO.
 */
void queue_param_buf_calc(uint8_t param)
{
    uint16_t base = ((uint16_t)G_BUF_BASE_HI << 8) | G_BUF_BASE_LO;
    uint16_t offset_val = (uint16_t)G_USB_PARAM_0B00 * 0x40;
    uint16_t result = base + offset_val;

    G_EP_QUEUE_PARAM = param;
    G_BUF_OFFSET_HI = (uint8_t)(result >> 8);
    G_BUF_OFFSET_LO = (uint8_t)result;
}

/*
 * queue_index_inc_wrap - Increment queue index with 5-bit wrap
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
void queue_index_inc_wrap(void)
{
    G_USB_PARAM_0B00 = (G_USB_PARAM_0B00 + 1) & 0x1F;
}

uint8_t stub_return_zero(void) { return 0; }

void ext_mem_init_address_e914(void)
{
    /* Set up extended memory address and call access routine */
    /* Address: Bank 0x02, offset 0x2805 */
    ext_mem_bank_access(0x02, 0x28, 0x05);
}

/*
 * xdata_cond_write - Conditional XDATA write helper (register-based)
 * Note: This is an alternative entry point into banked_store_byte() at 0x0be6.
 * When r3==1, it performs a simple XDATA write and returns at 0x0bee/0x0bef.
 * The full banked_store_byte() handles additional memory types (IDATA, PDATA, etc).
 *
 * Since this function operates on raw register state from the caller,
 * and C cannot easily replicate this register-passing behavior,
 * the stub remains empty. Callers that use this function should be
 * aware that the write operation is not performed in the C version.
 */
void xdata_cond_write(void)
{
    /* Register-based function - see banked_store_byte() at 0x0be6 */
}

void ext_mem_bank_access(uint8_t bank, uint8_t addr_hi, uint8_t addr_lo)
{
    (void)bank; (void)addr_hi; (void)addr_lo;
    /* Stub */
}

/*
 * dispatch_helper - Bank-switching trampoline to NOP space (empty)
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
void dispatch_helper(void)
{
    /* Target is NOP space - intentionally empty */
}

/*
 * reg_modify_helper - Set bit 5 in register at DPTR
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
void reg_modify_helper(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xDF) | 0x20;  /* Set bit 5 */
    *reg = val;
}

/*
 * ext_mem_access_wrapper_e914 - Extended memory access wrapper
 * Address: 0xe914-0xe91a (7 bytes)
 */
void ext_mem_access_wrapper_e914(void)
{
    ext_mem_bank_access(0x02, 0x28, 0x05);
}

void pcie_config_helper(void)
{
    uint8_t mode_bits;
    uint8_t val;

    /* Read E302 and check bits 4-5 */
    mode_bits = REG_PHY_MODE_E302 & 0x30;

    /* Extract and check if both bits 4-5 are set (mode_bits >> 4 == 3) */
    if (((mode_bits >> 4) & 0x0F) == 0x03) {
        /* Alternate path - both bits set */
        pcie_short_delay();
        uart_puthex(0);  /* lcall 0x51c7 */
        /* uart_puts - different address than main path */
        return;
    }

    /* Main processing path */
    pcie_short_delay();
    uart_puthex(0);

    /* Additional helper calls */
    cmd_engine_clear();
    cmd_engine_wait_idle();
    link_state_init_stub();

    /* Poll loop 1: Wait for CC89 bit 1 */
    while (!(REG_XFER_DMA_CMD & 0x02)) {
        /* Wait */
    }

    /* Configure registers */
    cmd_config_e40b();

    /* E403 = 0 */
    REG_CMD_CTRL_E403 = 0x00;
    /* E404 = 0x40 */
    REG_CMD_CFG_E404 = 0x40;
    /* E405: clear bits 0-2, set bits 0 and 2 (value 5) */
    val = REG_CMD_CFG_E405;
    val = (val & 0xF8) | 0x05;
    REG_CMD_CFG_E405 = val;
    /* E402: clear bits 5-7, set bit 5 */
    val = REG_CMD_STATUS_E402;
    val = (val & 0x1F) | 0x20;
    REG_CMD_STATUS_E402 = val;

    /* Poll loop 2: Wait for cmd_check_busy to return 0 */
    while (cmd_check_busy()) {
        /* Wait */
    }

    /* Start trigger */
    cmd_start_trigger();

    /* Poll loop 3: Wait for E41C bit 0 to clear */
    while (REG_CMD_BUSY_STATUS & 0x01) {
        /* Wait */
    }

    /* Set completion flag */
    G_PCIE_COMPLETE_07DF = 1;
}

void pcie_status_helper(void)
{
    reg_timer_init_and_start();
}
