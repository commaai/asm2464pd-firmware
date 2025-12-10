/*
 * ASM2464PD Firmware - Vendor SCSI Command Handlers
 *
 * Implements vendor-specific SCSI commands (0xE0-0xE8) used by the
 * tinygrad Python library for device control and firmware updates.
 *
 * ============================================================================
 * VENDOR COMMAND OVERVIEW
 * ============================================================================
 *
 * The ASM2464PD uses vendor SCSI commands for special operations:
 *
 *   0xE0 - Config Read   : Read 128-byte config blocks
 *   0xE1 - Config Write  : Write 128-byte config blocks (vendor/product info)
 *   0xE2 - Flash Read    : Read N bytes from SPI flash
 *   0xE3 - Firmware Write: Flash firmware to SPI (0x50=part1, 0xD0=part2)
 *   0xE4 - XDATA Read    : Read bytes from XDATA memory space
 *   0xE5 - XDATA Write   : Write single byte to XDATA memory space
 *   0xE6 - NVMe Admin    : Passthrough NVMe admin commands
 *   0xE8 - Reset/Commit  : System reset or commit flashed firmware
 *
 * ============================================================================
 * VENDOR COMMAND DISPATCH ADDRESSES (Real Firmware)
 * ============================================================================
 *
 * Bank 0 addresses are CPU addresses (0x0000-0xFFFF).
 * Bank 1 file offsets: file_offset = 0x10000 + (bank1_addr - 0x8000)
 *
 * E0 (Config Read):
 *   - Dispatch: 0x1353 (cjne a, #0xe0, jump to 0x12e5)
 *   - Handler:  0x12e5 (calls 0x1646 for CDB parsing)
 *   - Alt dispatch: 0x436e, 0x9073 (other entry points)
 *
 * E1 (Config Write):
 *   - Shares dispatch with E0, differentiated by CDB byte (bit 6)
 *   - Write mode when (opcode & 0x40) in CDB processing
 *
 * E2 (Flash Read):
 *   - Dispatch: 0x140dc (Bank 1, cjne a, #0xe2)
 *   - Handler:  0xd945 (Bank 0) - Flash read implementation
 *   - Checks state at 0x0B02, opcode at 0x0B03
 *
 * E3 (Firmware Write):
 *   - Dispatch: 0x140f9 (Bank 1, cjne a, #0xe3)
 *   - Handler:  0xcf5d (Bank 0) - Flash write implementation
 *   - State check: 0x0B02 == 0x02 for E3 mode
 *
 * E4 (XDATA Read):
 *   - Dispatch: 0x13473 (Bank 1 file offset)
 *   - Bank 1 CPU address: 0xb473
 *
 * E5 (XDATA Write):
 *   - Dispatch: 0x1343c (Bank 1 file offset, cjne a, #0xe5)
 *   - Bank 1 CPU address: 0xb43c
 *
 * E6 (NVMe Admin):
 *   - Dispatch: 0x395b (Bank 0, cjne a, #0xe6)
 *   - Jump table with sub-commands at 0x03ae-0x040d
 *   - Trampoline: 0x0311 (sets up Bank 1 calls via ret trick)
 *   - Sub-handler addresses loaded to DPTR then ajmp 0x0311:
 *       0x03ae: DPTR=0xef3e    0x03db: DPTR=0xef46
 *       0x03b3: DPTR=0xa327    0x03e0: DPTR=0xe01f
 *       0x03b8: DPTR=0xbd76    0x03e5: DPTR=0xca52
 *       0x03bd: DPTR=0xdde0    0x03ea: DPTR=0xec9b
 *       0x03c2: DPTR=0xe12b    0x03ef: DPTR=0xc98d
 *       0x03c7: DPTR=0xef42    0x03f4: DPTR=0xdd1a
 *       0x03cc: DPTR=0xe632    0x03f9: DPTR=...
 *       0x03d1: DPTR=0xd440    0x03fe: DPTR=...
 *       0x03d6: DPTR=0xc65f
 *   - Also 0x4abf, 0x50a2, 0x53e6, 0x5462 called directly
 *
 * E8 (Reset/Commit):
 *   - Setup at 0x13a66 (Bank 1 file offset)
 *   - Sets r6=0xC2, r7=0xE8 (addr 0xC2E8) when idata 0x56 == 0
 *   - Loops calling 0xb6e0, 0xb700 with counter at idata 0x54
 *   - Register access at 0xC2C3 or 0xC343 based on mode
 *
 * ============================================================================
 * STATE MACHINE (0x0B02 register)
 * ============================================================================
 *
 * The vendor command state machine uses XDATA 0x0B02:
 *   - State 0: Initial/idle
 *   - State 1: E2 Flash Read active
 *   - State 2: E3 Firmware Write active
 *
 * Magic check: XDATA 0xEA90 must contain 0x5A for vendor commands to work.
 *
 * ============================================================================
 * BANK 1 HELPER FUNCTIONS (File Offsets)
 * ============================================================================
 *
 *   vendor_cmd_e5_write : 0x1343c-0x13472 - XDATA write handler
 *   vendor_cmd_e4_read  : 0x13473-0x1351f - XDATA read handler
 *   set_dptr_0810_store        : 0x13665-0x1366a - set DPTR=0x0810, ljmp store_dword
 *   clear_bits_at_ptr        : 0x1367c-0x1368a - clear bits at DPTR (full sequence)
 *   or_bits_clear_bit6        : 0x13683-0x1368a - ORL 0x1C, ANL 0xBF (mid-entry)
 *   shift_store_bytes        : 0x136b5-0x136c3 - shift and store two bytes
 *   shift_merge_store        : 0x136ec-0x136f6 - shift a*4, merge/store
 *   load_ab7_compare        : 0x136f7-0x13700 - load 0x0AB7, compare
 *   loop_store_params        : 0x13720-0x1374d - loop store, copy params
 *   check_mode_control        : 0x13775-0x137xx - check mode/control
 *
 * ============================================================================
 * BANK 0 HELPER FUNCTIONS
 * ============================================================================
 *
 *   helper_0d08         : 0x0d08-0x0d14 - ORL 32-bit r4-r7 |= r0-r3
 *   helper_0d22         : 0x0d22-0x0d32 - SUBB 32-bit compare
 *   helper_0d46         : 0x0d46-0x0d58 - Left shift r4-r7 by r0 bits
 *   helper_0d84         : 0x0d84-0x0d9c - Read XDATA dword to r4-r7
 *   helper_0d9d         : 0x0d9d-0x0da8 - Read XDATA dword to r0-r3
 *   helper_0dc5         : 0x0dc5-0x0dd0 - Store r4-r7 to XDATA at DPTR
 *
 * ============================================================================
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"
#include "utils.h"

/*
 * IDATA work variables used by vendor handlers
 * These are defined in globals.h:
 *   I_WORK_51 (0x51) - loop counter
 *   I_WORK_55 (0x55) - state/mode
 *   I_WORK_56 (0x56) - secondary state
 *   I_WORK_57 (0x57) - CDB addr low
 *   I_WORK_58 (0x58) - CDB value/addr mid
 *   I_WORK_59 (0x59) - CDB addr high byte 1
 *   I_WORK_5A (0x5A) - CDB addr high byte 0
 */

/*
 * Bank 0 helper function declarations - pointer-taking wrappers
 *
 * Note: or32(), cmp32(), shl32() are in utils.c as naked functions.
 * The functions below take __xdata pointers so SDCC will set DPTR.
 */

/* 0x0d84-0x0d9c: Read XDATA dword at DPTR to r4-r7 */
void helper_load_dword_r4r7(__xdata uint8_t *ptr) __naked
{
    (void)ptr;
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

/* 0x0d9d-0x0da8: Read XDATA dword at DPTR to r0-r3 */
void helper_load_dword_r0r3(__xdata uint8_t *ptr) __naked
{
    (void)ptr;
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

/* 0x0dc5-0x0dd0: Store r4-r7 to XDATA at DPTR */
void helper_store_dword(__xdata uint8_t *ptr) __naked
{
    (void)ptr;
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
 * Bank 1 helper functions for vendor commands
 * All addresses are FILE OFFSETS (bank 1 starts at 0x10000)
 */

/*
 * set_dptr_0810_store - Set DPTR to 0x0810 and store dword
 * File offset: 0x13665-0x1366a
 *
 * Disassembly:
 *   0x13665: mov dptr, #0x0810
 *   0x13668: ljmp 0x0dc5        ; store r4-r7 to DPTR
 *
 * Sets DPTR = 0x0810 (G_VENDOR_CDB_BASE) and stores r4-r7 there
 */
static void set_dptr_0810_store(void)
{
    __xdata uint8_t *ptr = (__xdata uint8_t *)0x0810;
    helper_store_dword(ptr);
}

/*
 * clear_bits_at_ptr - Clear bits at DPTR
 * File offset: 0x1367c-0x1368a
 *
 * Disassembly:
 *   0x1367c: movx a, @dptr
 *   0x1367d: anl a, #0xfd       ; clear bit 1
 *   0x1367f: movx @dptr, a
 *   0x13680: movx a, @dptr
 *   0x13681: anl a, #0xc3       ; clear bits 2-5
 *   0x13683: orl a, #0x1c       ; set bits 2-4  <- or_bits_clear_bit6 entry
 *   0x13685: movx @dptr, a
 *   0x13686: movx a, @dptr
 *   0x13687: anl a, #0xbf       ; clear bit 6
 *   0x13689: movx @dptr, a
 *   0x1368a: ret
 */
static void clear_bits_at_ptr(__xdata uint8_t *ptr)
{
    uint8_t val;

    /* Step 1: Clear bit 1 */
    val = *ptr;
    val &= 0xFD;
    *ptr = val;

    /* Step 2: Clear bits 2-5, set bits 2-4 */
    val = *ptr;
    val &= 0xC3;  /* Clear bits 2-5 */
    val |= 0x1C;  /* Set bits 2-4 */
    *ptr = val;

    /* Step 3: Clear bit 6 */
    val = *ptr;
    val &= 0xBF;
    *ptr = val;
}

/*
 * or_bits_clear_bit6 - OR bits and clear bit 6
 * File offset: 0x13683-0x1368a (mid-entry into clear_bits_at_ptr)
 *
 * Disassembly:
 *   0x13683: orl a, #0x1c       ; set bits 2-4 (assumes ANL 0xC3 already done)
 *   0x13685: movx @dptr, a
 *   0x13686: movx a, @dptr
 *   0x13687: anl a, #0xbf       ; clear bit 6
 *   0x13689: movx @dptr, a
 *   0x1368a: ret
 *
 * Entry point for E5 handler - expects a already has ANL 0xC3 applied
 */
static void or_bits_clear_bit6(__xdata uint8_t *ptr)
{
    uint8_t val;

    val = *ptr;
    val &= 0xC3;
    val |= 0x1C;
    *ptr = val;

    val = *ptr;
    val &= 0xBF;
    *ptr = val;
}

/*
 * shift_store_bytes - Shift and store two bytes
 * File offset: 0x136b5-0x136c3
 *
 * Disassembly:
 *   0x136b5: add a, 0xe0        ; a = a + a (0xe0 is ACC)
 *   0x136b7: mov r7, a
 *   0x136b8: movx a, @dptr
 *   0x136b9: rlc a              ; rotate left through carry
 *   0x136ba: mov r6, a
 *   0x136bb: mov a, r7
 *   0x136bc: orl a, r5
 *   0x136bd: mov r7, a
 *   0x136be: mov a, r6
 *   0x136bf: movx @dptr, a
 *   0x136c0: inc dptr
 *   0x136c1: mov a, r7
 *   0x136c2: movx @dptr, a
 *   0x136c3: ret
 */
static void shift_store_bytes(__xdata uint8_t *ptr, uint8_t r5_val) __naked
{
    (void)ptr;
    (void)r5_val;
    __asm
        ; dptr already set by caller
        ; r5 has r5_val
        add  a, acc         ; a = a * 2
        mov  r7, a
        movx a, @dptr
        rlc  a
        mov  r6, a
        mov  a, r7
        orl  a, r5
        mov  r7, a
        mov  a, r6
        movx @dptr, a
        inc  dptr
        mov  a, r7
        movx @dptr, a
        ret
    __endasm;
}

/*
 * shift_merge_store - Shift a*4, merge with DPTR value, store
 * File offset: 0x136ec-0x136f6
 *
 * Disassembly:
 *   0x136ec: add a, 0xe0        ; a = a * 2
 *   0x136ee: add a, 0xe0        ; a = a * 4
 *   0x136f0: mov r7, a
 *   0x136f1: movx a, @dptr
 *   0x136f2: anl a, #0xc3       ; clear bits 2-5
 *   0x136f4: orl a, r7          ; merge in shifted value
 *   0x136f5: movx @dptr, a
 *   0x136f6: ret
 */
static uint8_t shift_merge_store(__xdata uint8_t *ptr, uint8_t val)
{
    uint8_t r7;
    uint8_t result;

    r7 = val << 2;  /* a + a + a in original = shift left 2 */

    result = *ptr;
    result &= 0xC3;
    result |= r7;
    *ptr = result;

    return r7;
}

/*
 * load_ab7_compare - Load from 0x0AB7 and compare
 * File offset: 0x136f7-0x13700
 *
 * Disassembly:
 *   0x136f7: mov dptr, #0x0ab7
 *   0x136fa: lcall 0x0d9d       ; load dword to r0-r3
 *   0x136fd: clr c
 *   0x136fe: ljmp 0x0d22        ; 32-bit compare
 *
 * Note: Function actually starts at 0x136f7, not 0x136fa
 */
static uint8_t load_ab7_compare(void)
{
    __xdata uint8_t *ptr = &G_VENDOR_DATA_0AB7;

    helper_load_dword_r0r3(ptr);
    /* The carry is cleared and compare is done */
    /* cmp32 is a naked assembly function that sets carry flag for result */
    cmp32();
    return 0;  /* Result is in carry flag, can't easily get from C */
}

/*
 * loop_store_params - Loop store, copy params, check flags
 * File offset: 0x13720-0x1374d
 *
 * Disassembly:
 *   0x13720: mov a, r7
 *   0x13721: movx @dptr, a      ; store r7 to DPTR
 *   0x13722: inc 0x51           ; increment loop counter
 *   0x13724: mov a, 0x51
 *   0x13726: cjne a, #0x64, 0x3712  ; loop until 100 iterations
 *   0x13729: mov dptr, #0x0a57  ; G_CMD_CTRL_PARAM
 *   0x1372c: movx a, @dptr
 *   0x1372d: mov dptr, #0x0804  ; G_VENDOR_CMD_BUF_0804
 *   0x13730: movx @dptr, a      ; copy param
 *   0x13731: mov dptr, #0x0a58  ; G_CMD_TIMEOUT_PARAM
 *   0x13734: movx a, @dptr
 *   0x13735: mov dptr, #0x0805  ; G_VENDOR_CMD_BUF_0805
 *   0x13738: movx @dptr, a      ; copy param
 *   0x13739: mov dptr, #0x09f9  ; G_EVENT_FLAGS
 *   0x1373c: movx a, @dptr
 *   0x1373d: jb 0xe0.7, 0x3747  ; if bit 7 set, skip
 *   0x13740: mov dptr, #0x081b  ; G_VENDOR_STATUS_081B
 *   0x13743: movx a, @dptr
 *   0x13744: anl a, #0xfd       ; clear bit 1
 *   0x13746: movx @dptr, a
 */
static uint8_t loop_store_params(__xdata uint8_t *ptr, uint8_t r7_val, uint8_t r1_offset)
{
    (void)r1_offset;

    /* Store r7 to DPTR */
    *ptr = r7_val;

    /* Increment loop counter */
    I_WORK_51++;

    /* Check if reached 0x64 (100) iterations */
    if (I_WORK_51 != 0x64) {
        return 0;  /* Continue looping */
    }

    /* Copy command parameters */
    G_VENDOR_CMD_BUF_0804 = G_CMD_CTRL_PARAM;
    G_VENDOR_CMD_BUF_0805 = G_CMD_TIMEOUT_PARAM;

    /* Check event flags bit 7 */
    if (!(G_EVENT_FLAGS & 0x80)) {
        /* Clear bit 1 of status */
        G_VENDOR_STATUS_081B &= 0xFD;
    }

    /* Clear loop counter */
    I_WORK_51 = 0;

    /* Return for table lookup phase */
    return 1;
}

/*
 * check_mode_control - Check mode and control flags
 * File offset: 0x13775-0x137xx
 *
 * Checks vendor mode and control flags, modifies status.
 * The actual disassembly shows an 'inc r1' which is part of
 * a larger loop context from the caller.
 */
static void check_mode_control(void)
{
    uint8_t mode_07cc;
    uint8_t ctrl_07b9;
    uint8_t mode_07cf;

    mode_07cc = G_VENDOR_MODE_07CC;

    /* Check if mode < 3 */
    if (mode_07cc < 3) {
        return;  /* Skip modification */
    }

    ctrl_07b9 = G_VENDOR_CTRL_07B9;
    if (ctrl_07b9 == 0) {
        return;  /* Skip modification */
    }

    mode_07cf = G_VENDOR_MODE_07CF;
    if (mode_07cf == 1) {
        return;  /* Skip modification */
    }

    /* Modify status based on mode_07cf */
    /* Original: if mode != 1, modify 0x081a */
}

/*
 * vendor_cmd_e5_xdata_write - Write to XDATA memory space
 * Bank 1 Address: 0xb43c-0xb472
 *
 * CDB format:
 *   Byte 0: 0xE5
 *   Byte 1: Value to write
 *   Byte 2: Address bits 16-23
 *   Byte 3: Address bits 8-15
 *   Byte 4: Address bits 0-7
 *
 * Disassembly:
 *   0x1343c: cjne a, #0xe5, 0x3497    ; check opcode (E4 handler)
 *   0x1343f: movx @dptr, a            ; acknowledge
 *   0x13440: mov a, 0x55              ; get state
 *   0x13442: jnb acc.1, 0x346c        ; check mode bit 1
 *   0x13445: mov r1, #0x6c            ; offset
 *   0x13447: lcall 0xb720             ; loop_store_params
 *   0x1344a: mov r7, #0x00
 *   0x1344c: jb acc.0, 0x3451         ; check flag bit 0
 *   0x1344f: mov r7, #0x01
 *   0x13451: mov r5, 0x57             ; get value from CDB
 *   0x13453: lcall 0xea7c             ; execute actual write (bank 1: 0x1ea7c)
 *   0x13456: lcall 0xb6b5             ; shift_store_bytes
 *   0x13459: mov dptr, #0xc343        ; REG_VENDOR_CTRL_C343
 *   0x1345c: lcall 0xb683             ; or_bits_clear_bit6
 *   0x1345f: mov a, r7
 *   0x13460: anl a, #0x01
 *   0x13462: mov r7, a
 *   0x13463: mov a, r7
 *   0x13464: jz 0x346c                ; if zero, skip
 *   0x13466: mov dptr, #0x0ab5        ; G_VENDOR_DATA_0AB5
 *   0x13469: mov a, 0x58              ; get value
 *   0x1346b: movx @dptr, a            ; store
 *   0x1346c: lcall 0xb775             ; check_mode_control
 *   0x1346f: lcall 0xb6fa             ; load_ab7_compare
 *   0x13472: ret
 */
void vendor_cmd_e5_xdata_write(void)
{
    uint8_t state;
    uint8_t r7;
    uint8_t value;
    __xdata uint8_t *ctrl_reg;

    /* Get state from I_WORK_55 */
    state = I_WORK_55;

    /* Check if bit 1 is set */
    if (!(state & 0x02)) {
        /* Skip to end helper calls */
        goto end_helper;
    }

    /* Parse CDB and loop */
    /* r1 = 0x6c offset, call helper_b720 */
    /* This sets up the CDB address parsing */
    /* The actual XDATA write uses address from I_WORK_57-5A */

    r7 = 0x00;

    /* Check bit 0 of accumulator (from helper) */
    if (!(state & 0x01)) {
        r7 = 0x01;
    }

    /* Get value to write from I_WORK_57 (CDB byte 1) */
    value = I_WORK_57;

    /* Execute the actual XDATA write */
    /* The real firmware calls 0xea7c here which performs the write */
    /* using the address from I_WORK_58/59/5A */
    {
        uint16_t addr;
        __xdata uint8_t *dst;

        addr = ((uint16_t)I_WORK_59 << 8) | I_WORK_58;
        dst = (__xdata uint8_t *)addr;
        *dst = value;
    }

    /* Call shift and store helper */
    /* helper_b6b5 with r5 = value */

    /* Set vendor control register */
    ctrl_reg = &REG_VENDOR_CTRL_C343;
    or_bits_clear_bit6(ctrl_reg);

    /* Check r7 bit 0 */
    r7 &= 0x01;
    if (r7 != 0) {
        /* Store value to vendor data storage */
        G_VENDOR_DATA_0AB5 = I_WORK_58;
    }

end_helper:
    /* Call end helper functions */
    check_mode_control();
    load_ab7_compare();
}

/*
 * vendor_cmd_e4_xdata_read - Read from XDATA memory space
 * Bank 1 Address: 0xb473-0xbd1f
 *
 * CDB format:
 *   Byte 0: 0xE4
 *   Byte 1: Size (number of bytes to read)
 *   Byte 2: Address bits 16-23
 *   Byte 3: Address bits 8-15
 *   Byte 4: Address bits 0-7
 *
 * Disassembly:
 *   0x13473: lcall 0xb663             ; set_dptr_0810_store
 *   0x13476: lcall 0x0d08             ; helper_0d08 - ORL 32-bit
 *   0x13479: push 0x04                ; push r4
 *   0x1347b: push 0x05                ; push r5
 *   0x1347d: push 0x06                ; push r6
 *   0x1347f: push 0x07                ; push r7
 *   0x13481: mov dptr, #0x0816        ; G_VENDOR_RESP_BUF
 *   0x13484: lcall 0xb67c             ; clear_bits_at_ptr
 *   0x13487: mov r0, #0x10            ; 16 bits
 *   0x13489: lcall 0x0d46             ; helper_0d46 - shift left
 *   0x1348c: pop 0x03                 ; pop to r3
 *   0x1348e: pop 0x02                 ; pop to r2
 *   0x13490: pop 0x01                 ; pop to r1
 *   0x13492: pop 0x00                 ; pop to r0
 *   0x13494: lcall 0x0d08             ; helper_0d08 - ORL 32-bit
 *   ... continues with second pass using shift 24 bits
 *   0x134bb: lcall 0xb6ec             ; shift_merge_store
 *   ... state machine with PHY vendor registers
 */
void vendor_cmd_e4_xdata_read(void)
{
    uint8_t state;
    uint8_t sec_state;
    __xdata uint8_t *ptr;
    __xdata uint8_t *resp_buf;
    uint8_t r4, r5, r6, r7;
    uint8_t val;

    /* Call set_dptr_0810_store - set DPTR=0x0810 and store CDB dword */
    set_dptr_0810_store();

    /* ORL 32-bit operation */
    or32();

    /* Save r4-r7 on stack (push) */
    /* These contain the parsed CDB data */

    /* Set DPTR to response buffer 0x0816 */
    resp_buf = &G_VENDOR_RESP_BUF;

    /* Clear bits in response buffer */
    clear_bits_at_ptr(resp_buf);

    /* Shift left 16 bits (r0 = 0x10) */
    __asm
        mov r0, #0x10
        lcall _shl32
    __endasm;

    /* Pop r0-r3 (restore saved r4-r7 to r0-r3) */
    /* ORL 32-bit again */
    or32();

    /* Second pass: inc DPTR (0x0817), clear bits, shift 24 (0x18) */
    clear_bits_at_ptr(resp_buf + 1);
    __asm
        mov r0, #0x18
        lcall _shl32
    __endasm;

    /* Pop and ORL again */
    or32();

    /* Store address bytes to idata work variables */
    /* In firmware: mov 0x5a,r7; mov 0x59,r6; mov 0x58,r5; mov 0x57,r4 */
    /* These get the parsed address from CDB */
    /* For now, read from CDB buffer */
    ptr = &G_VENDOR_CDB_BASE;
    r4 = ptr[1];  /* Size */
    r5 = ptr[2];  /* Addr high */
    r6 = ptr[3];  /* Addr mid */
    r7 = ptr[4];  /* Addr low */

    I_WORK_5A = r7;
    I_WORK_59 = r6;
    I_WORK_58 = r5;
    I_WORK_57 = r4;

    /* Call shift_merge_store - shift and merge to get state */
    ptr = &G_VENDOR_CDB_BASE;
    state = shift_merge_store(ptr, I_WORK_58);

    /* Store state to I_WORK_55 */
    I_WORK_55 = state;

    /* State machine: check state value */
    if (state == 0) {
        /* State 0: skip secondary state setup */
        sec_state = 0;
    } else if (state == 1) {
        /* State 1: skip secondary state setup */
        sec_state = 0;
    } else {
        /* Other states: need secondary processing */
        sec_state = 1;
    }

    I_WORK_56 = sec_state;

    /* Select register pair based on secondary state */
    if (sec_state == 0) {
        ptr = &REG_PHY_VENDOR_CTRL_C2E2;
    } else {
        ptr = &REG_VENDOR_CTRL_C362;
    }

    /* Read from control register */
    helper_load_dword_r4r7(ptr);

    /* Store to data area 0x0AAC */
    helper_store_dword(&G_STATE_COUNTER_0AAC);

    /* Select second register based on state */
    if (sec_state == 0) {
        ptr = &REG_PHY_VENDOR_CTRL_C2E0;
    } else {
        ptr = &REG_VENDOR_CTRL_C360;
    }

    /* Read two bytes from register */
    val = ptr[0];
    r6 = val;
    val = ptr[1];
    r7 = val;

    /* Store to 0x0AB0-0x0AB1 */
    G_FLASH_ADDR_3 = r6;
    G_FLASH_LEN_LO = r7;

    /* More processing continues... */
    /* The actual XDATA read and response is done through DMA */
}

/*
 * vendor_is_vendor_command - Check if opcode is a vendor command
 *
 * Returns 1 if opcode is 0xE0-0xE8 (vendor range), 0 otherwise.
 */
uint8_t vendor_is_vendor_command(uint8_t opcode)
{
    return (opcode >= 0xE0 && opcode <= 0xE8) ? 1 : 0;
}

/*
 * flash_read_setup_spi - Setup SPI for flash read operation
 * Address: 0xd932-0xd955
 *
 * Configures SPI controller registers for flash read:
 *   - Sets buffer config 0x9300 = 0x04
 *   - Sets PHY control 0x91d1 = 0x02
 *   - Sets buffer config 0x9301 = 0x40, then 0x80
 *   - Sets PHY control 0x91d1 = 0x08, then 0x01
 *   - Clears system state 0x0ae2
 *
 * Disassembly:
 *   0xd932: mov dptr, #0x9300; mov a, #0x04; movx @dptr, a
 *   0xd938: mov dptr, #0x91d1; mov a, #0x02; movx @dptr, a
 *   0xd93e: mov dptr, #0x9301; mov a, #0x40; movx @dptr, a
 *   0xd944: mov a, #0x80; movx @dptr, a
 *   0xd947: mov dptr, #0x91d1; mov a, #0x08; movx @dptr, a
 *   0xd94d: mov a, #0x01; movx @dptr, a
 *   0xd950: clr a; mov dptr, #0x0ae2; movx @dptr, a
 *   0xd955: ret
 */
void flash_read_setup_spi(void)
{
    /* Configure buffer for SPI read */
    REG_BUF_CFG_9300 = 0x04;

    /* Enable SPI PHY */
    REG_USB_PHY_CTRL_91D1 = 0x02;

    /* Set buffer mode to 0x40, then enable with 0x80 */
    REG_BUF_CFG_9301 = 0x40;
    REG_BUF_CFG_9301 = 0x80;

    /* Configure PHY for read transfer */
    REG_USB_PHY_CTRL_91D1 = 0x08;
    REG_USB_PHY_CTRL_91D1 = 0x01;

    /* Clear system state */
    G_SYSTEM_STATE_0AE2 = 0;
}

/*
 * flash_read_execute - Execute flash read with address setup
 * Address: 0xd956-0xd995
 *
 * Sets up flash address from 0x0a5f, performs 32-bit address shift
 * and executes the read via PCIe helper.
 *
 * Disassembly:
 *   0xd956: mov r3, 0x07              ; save r7 to r3
 *   0xd958: mov r0, #0x65; mov @r0, #0x07  ; idata 0x65 = 0x07
 *   0xd95c: mov r0, #0x63; mov @r0, #0x00  ; idata 0x63 = 0x00
 *   0xd960: inc r0; mov @r0, #0x06        ; idata 0x64 = 0x06
 *   0xd963: mov a, r5; mov r6, a         ; r6 = r5
 *   0xd965: mov a, r3; mov r7, a         ; r7 = r3 (saved r7)
 *   0xd967: clr a; mov r4, a; mov r5, a  ; r4 = r5 = 0
 *   0xd96a: push r4-r7                   ; save 32-bit value
 *   0xd972: mov dptr, #0x0a5f            ; flash address base
 *   0xd975: lcall 0x0d84                 ; load dword to r4-r7
 *   0xd978: mov r0, #0x10                ; shift count = 16
 *   0xd97a: lcall 0x0d46                 ; shift left by 16
 *   0xd97d: pop r0-r3                    ; restore to r0-r3
 *   0xd985: lcall 0x0d08                 ; ORL 32-bit (r4-r7 |= r0-r3)
 *   0xd988: lcall 0x994e                 ; store to PCIe data reg 0xb220
 *   0xd98b: lcall 0xe91d                 ; Bank 1 helper
 *   0xd98e: mov a, r7; mov r7, #0x00     ; check r7
 *   0xd991: jz 0xd995; mov r7, #0xff     ; return 0 or 0xff
 *   0xd995: ret
 *
 * Parameters:
 *   r5 = address high byte (passed in)
 *   r7 = address low byte (passed in)
 *
 * Returns:
 *   r7 = 0x00 on success, 0xff on error
 */
uint8_t flash_read_execute(uint8_t addr_hi, uint8_t addr_lo) __naked
{
    (void)addr_hi;
    (void)addr_lo;
    __asm
        ; r5 = addr_hi, r7 = addr_lo (from caller)
        mov  a, r7
        mov  r3, a          ; save addr_lo to r3

        ; Setup idata work variables
        mov  r0, #0x65
        mov  @r0, #0x07     ; I_WORK_65 = 0x07
        mov  r0, #0x63
        mov  @r0, #0x00     ; I_WORK_63 = 0x00
        inc  r0
        mov  @r0, #0x06     ; I_WORK_64 = 0x06

        ; Setup r4-r7 with address (r6=addr_hi, r7=addr_lo, r4=r5=0)
        mov  a, r5
        mov  r6, a          ; r6 = addr_hi
        mov  a, r3
        mov  r7, a          ; r7 = addr_lo
        clr  a
        mov  r4, a          ; r4 = 0
        mov  r5, a          ; r5 = 0

        ; Push r4-r7
        push 0x04
        push 0x05
        push 0x06
        push 0x07

        ; Load flash address from 0x0a5f to r4-r7
        mov  dptr, #0x0a5f
        lcall _load_dword_r4r7

        ; Shift left by 16 bits
        mov  r0, #0x10
        lcall _shl32

        ; Pop to r0-r3
        pop  0x03
        pop  0x02
        pop  0x01
        pop  0x00

        ; ORL 32-bit: r4-r7 |= r0-r3
        lcall _or32

        ; Store to PCIe data register 0xB220
        mov  dptr, #0xb220
        lcall _store_dword_r4r7

        ; Call Bank 1 helper (0xe91d)
        ; This performs the actual flash read operation
        ; For now, we assume success and return 0
        mov  r7, #0x00
        ret
    __endasm;
}

/*
 * vendor_cmd_e3_fw_write_dispatch - E3 Firmware Write state dispatch
 * Address: 0xcf5d-0xcfd4
 *
 * State machine for firmware write operation. Checks register 0x9091
 * and dispatches to appropriate handler based on state bits.
 *
 * Disassembly:
 *   0xcf5d: jnc 0xcfa3                  ; if no carry, jump (error path)
 *   0xcf5f: rr a; movx @dptr, a         ; store rotated value
 *   0xcf61: mov dptr, #0xcc3e; ...      ; setup registers
 *   0xcf78: mov dptr, #0xca81; lcall 0xbceb  ; final config
 *   0xcf7e: ret                         ; success return
 *   0xcf7f: (alternate path - check 0x9091 bits)
 *   0xcf83: jnb bit0, 0xcf8d           ; check bit 0
 *   0xcf87: jb bit2, 0xcf8d            ; check bit 2
 *   0xcf8a: lcall 0xa740               ; bit handler
 *   0xcf8d: (continue state machine)
 *   0xcf91: jb bit1, 0xcfa4            ; check bit 1
 *   0xcf98: jnb bit1, 0xcfa4           ; more checks
 *   0xcf9b: lcall 0xd21e               ; bit 1 handler
 *   0xcfa8: jnb bit2, 0xcfb4           ; check bit 2
 *   0xcfab: lcall 0xdeb1               ; bit 2 handler
 *   0xcfb8: jnb bit3, 0xcfc4           ; check bit 3
 *   0xcfbb: lcall 0xb28c               ; bit 3 handler
 *   0xcfc8: jnb bit4, 0xcfd4           ; check bit 4
 *   0xcfcb: lcall 0xb6cf               ; bit 4 handler
 *   0xcfd4: ret
 *
 * State bits in REG_INT_FLAGS_EX0 (0x9091):
 *   Bit 0: Initial state check
 *   Bit 1: Write pending
 *   Bit 2: Erase pending
 *   Bit 3: Verify pending
 *   Bit 4: Commit pending
 */
void vendor_cmd_e3_fw_write_dispatch(void)
{
    uint8_t state;

    /* Read current state from interrupt flags register */
    state = REG_INT_FLAGS_EX0;

    /* Bit 0 check - initial state */
    if ((state & 0x01) == 0) {
        /* Check bit 2 */
        if (!(state & 0x04)) {
            /* Call handler 0xa740 */
            /* helper_a740(); */
        }
    }

    /* Bit 1 check - write pending */
    if (state & 0x02) {
        /* Call handler 0xd21e */
        /* helper_d21e(); */
        REG_INT_FLAGS_EX0 = 0x02;  /* Acknowledge bit 1 */
    }

    /* Bit 2 check - erase pending */
    if (state & 0x04) {
        /* Call handler 0xdeb1 */
        /* helper_deb1(); */
        REG_INT_FLAGS_EX0 = 0x04;  /* Acknowledge bit 2 */
    }

    /* Bit 3 check - verify pending */
    if (state & 0x08) {
        /* Call handler 0xb28c */
        /* helper_b28c(); */
        REG_INT_FLAGS_EX0 = 0x08;  /* Acknowledge bit 3 */
    }

    /* Bit 4 check - commit pending */
    if (state & 0x10) {
        /* Call handler 0xb6cf */
        /* helper_b6cf(); */
        REG_INT_FLAGS_EX0 = 0x10;  /* Acknowledge bit 4 */
    }
}

/*
 * vendor_cmd_e6_nvme_admin_dispatch - E6 NVMe Admin command dispatch
 * Address: 0x395b (Bank 0 jump table)
 *
 * This handler dispatches NVMe admin passthrough commands based on
 * a sub-command index. Each entry loads a Bank 1 function address
 * into DPTR and jumps to trampoline at 0x0311.
 *
 * Jump table entries (at 0x03ae-0x040d):
 *   0x03ae: DPTR=0xef3e (sub-cmd 0)
 *   0x03b3: DPTR=0xa327 (sub-cmd 1)
 *   0x03b8: DPTR=0xbd76 (sub-cmd 2)
 *   0x03bd: DPTR=0xdde0 (sub-cmd 3)
 *   0x03c2: DPTR=0xe12b (sub-cmd 4)
 *   0x03c7: DPTR=0xef42 (sub-cmd 5)
 *   0x03cc: DPTR=0xe632 (sub-cmd 6)
 *   0x03d1: DPTR=0xd440 (sub-cmd 7)
 *   0x03d6: DPTR=0xc65f (sub-cmd 8)
 *   0x03db: DPTR=0xef46 (sub-cmd 9)
 *   0x03e0: DPTR=0xe01f (sub-cmd 10)
 *   0x03e5: DPTR=0xca52 (sub-cmd 11)
 *   0x03ea: DPTR=0xec9b (sub-cmd 12)
 *   0x03ef: DPTR=0xc98d (sub-cmd 13)
 *   0x03f4: DPTR=0xdd1a (sub-cmd 14)
 *
 * The sub-command index comes from the CDB and selects which
 * NVMe admin operation to perform (identify, get features, etc.)
 */

/* E6 sub-command handler addresses (Bank 1) */
#define E6_HANDLER_0   0xef3e  /* Identify controller */
#define E6_HANDLER_1   0xa327  /* Get features */
#define E6_HANDLER_2   0xbd76  /* Set features */
#define E6_HANDLER_3   0xdde0  /* Get log page */
#define E6_HANDLER_4   0xe12b  /* Async event */
#define E6_HANDLER_5   0xef42  /* Abort command */
#define E6_HANDLER_6   0xe632  /* Create I/O CQ */
#define E6_HANDLER_7   0xd440  /* Create I/O SQ */
#define E6_HANDLER_8   0xc65f  /* Delete I/O CQ */
#define E6_HANDLER_9   0xef46  /* Delete I/O SQ */
#define E6_HANDLER_10  0xe01f  /* Firmware commit */
#define E6_HANDLER_11  0xca52  /* Firmware download */
#define E6_HANDLER_12  0xec9b  /* Format NVM */
#define E6_HANDLER_13  0xc98d  /* Sanitize */
#define E6_HANDLER_14  0xdd1a  /* Security operations */

/*
 * bank1_trampoline - Call Bank 1 function via ret trick
 * Address: 0x0311
 *
 * This trampoline enables calling functions in Bank 1 (file offset 0x10000+)
 * by pushing the return address, setting up the bank switch, and using RET.
 *
 * Disassembly:
 *   0x0311: push dpl           ; push target address low
 *   0x0313: push dph           ; push target address high
 *   0x0315: (bank switch logic)
 *   0x0318: ret                ; "call" target via ret
 *
 * Usage: Load target address into DPTR, then AJMP 0x0311
 */

/*
 * vendor_cmd_dispatch - Main vendor command dispatcher (SYNTHETIC)
 *
 * NOTE: This function is NOT a direct copy from the real firmware.
 * The real firmware uses a chain of CJNE instructions at multiple
 * locations to dispatch vendor commands:
 *   - 0x1353: cjne a,#0xe0 → ajmp 0x12e5
 *   - 0x140dc (Bank 1): cjne a,#0xe2
 *   - 0x140f9 (Bank 1): cjne a,#0xe3
 *   - 0x1343c (Bank 1): cjne a,#0xe5
 *   - 0x395b: cjne a,#0xe6
 *
 * This wrapper is provided for organizational purposes.
 * Called from SCSI command handler when opcode is in vendor range.
 * Dispatches based on command opcode to appropriate handler.
 */
void vendor_cmd_dispatch(uint8_t opcode)
{
    switch (opcode) {
    case 0xE0:
    case 0xE1:
        /* Config Read/Write - handled together, bit 6 differentiates */
        /* vendor_cmd_e0_e1_config(); */
        break;

    case 0xE2:
        /* Flash Read */
        flash_read_setup_spi();
        break;

    case 0xE3:
        /* Firmware Write */
        vendor_cmd_e3_fw_write_dispatch();
        break;

    case 0xE4:
        /* XDATA Read */
        vendor_cmd_e4_xdata_read();
        break;

    case 0xE5:
        /* XDATA Write */
        vendor_cmd_e5_xdata_write();
        break;

    case 0xE6:
        /* NVMe Admin passthrough */
        /* vendor_cmd_e6_nvme_admin_dispatch(); */
        break;

    case 0xE8:
        /* Reset/Commit */
        /* vendor_cmd_e8_reset_commit(); */
        break;

    default:
        /* Unknown vendor command */
        break;
    }
}
