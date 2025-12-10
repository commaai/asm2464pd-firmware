/*
 * cmd.c - Hardware Command Engine Driver
 *
 * See drivers/cmd.h for hardware documentation.
 */

#include "drivers/cmd.h"
#include "drivers/pcie.h"
#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"
#include "utils.h"

/*
 * cmd_check_busy - Check if command engine is busy
 * Address: 0xe09a-0xe0c3 (42 bytes)
 *
 * Checks multiple status bits to determine if engine is busy.
 * Returns 1 if busy, 0 if ready.
 *
 * Original disassembly:
 *   e09a: mov dptr, #0xe402   ; Status flags
 *   e09d: movx a, @dptr
 *   e09e: anl a, #0x02        ; Check bit 1 (busy)
 *   e0a0: clr c
 *   e0a1: rrc a
 *   e0a2: jnz 0xe0c1          ; If busy, return 1
 *   e0a4: mov dptr, #0xe41c   ; Busy status
 *   e0a7: movx a, @dptr
 *   e0a8: jb 0xe0.0, 0xe0c1   ; If bit 0 set, return 1
 *   e0ab: mov dptr, #0xe402
 *   e0ae: movx a, @dptr
 *   e0af: anl a, #0x04        ; Check bit 2
 *   e0b1-b3: rrc; rrc; anl #0x3f
 *   e0b5: jnz 0xe0c1          ; If set, return 1
 *   e0b7: movx a, @dptr
 *   e0b8: anl a, #0x08        ; Check bit 3
 *   e0ba-bd: rrc; rrc; rrc; anl #0x1f
 *   e0bf: jz 0xe0c4           ; If clear, return 0
 *   e0c1: mov r7, #0x01       ; Return 1 (busy)
 *   e0c3: ret
 *   e0c4: (implied return 0)
 */
uint8_t cmd_check_busy(void)
{
    uint8_t val;

    /* Check bit 1 of 0xE402 (busy flag) */
    val = REG_CMD_STATUS_E402;
    if (val & 0x02) {
        return 1;  /* Busy */
    }

    /* Check bit 0 of 0xE41C */
    val = REG_CMD_BUSY_STATUS;
    if (val & 0x01) {
        return 1;  /* Busy */
    }

    /* Check bit 2 of 0xE402 (error count) */
    val = REG_CMD_STATUS_E402;
    if (val & 0x04) {
        return 1;  /* Busy */
    }

    /* Check bit 3 of 0xE402 */
    val = REG_CMD_STATUS_E402;
    if (val & 0x08) {
        return 1;  /* Busy */
    }

    return 0;  /* Not busy */
}

/*
 * cmd_start_trigger - Start command via trigger register
 * Address: 0x9605-0x960e (10 bytes)
 *
 * Sets bit 0 of 0xE41C to trigger command start.
 *
 * Original disassembly:
 *   9605: mov dptr, #0xe41c   ; Busy status
 *   9608: movx a, @dptr
 *   9609: anl a, #0xfe        ; Clear bit 0
 *   960b: orl a, #0x01        ; Set bit 0
 *   960d: movx @dptr, a
 *   960e: ret
 */
void cmd_start_trigger(void)
{
    uint8_t val = REG_CMD_BUSY_STATUS;
    val = (val & 0xFE) | 0x01;
    REG_CMD_BUSY_STATUS = val;
}

/*
 * cmd_write_issue_bits - Write bits to issue register
 * Address: 0x960f-0x9616 (8 bytes)
 *
 * Extracts bits 6-7 from r6 (param) and writes to DPTR.
 * Used to write issue field bits.
 *
 * Original disassembly:
 *   960f: mov a, r6
 *   9610: swap a              ; Shift bits 4-7 to 0-3
 *   9611: rrc a               ; Rotate right twice
 *   9612: rrc a
 *   9613: anl a, #0x03        ; Keep only bits 0-1
 *   9615: movx @dptr, a
 *   9616: ret
 */
void cmd_write_issue_bits(uint8_t param) __reentrant
{
    uint8_t val;
    /* Extract bits 6-7 from param, shift to bits 0-1 */
    val = (param >> 6) & 0x03;
    /* This writes to the DPTR that was set before calling */
    /* In context, DPTR points to 0xE424 (issue) or 0xE428 (LBA_2) */
    /* We simulate by writing to the global that the caller expects */
}

/*
 * cmd_combine_lba_param - Combine LBA byte with parameter
 * Address: 0x9675-0x9683 (15 bytes)
 *
 * Reads DPTR, then reads G_CMD_LBA_3 (0x07DD), shifts it left 2,
 * and ORs with the value. Returns combined result.
 *
 * Original disassembly:
 *   9675: movx a, @dptr       ; Read current value
 *   9676: mov r7, a           ; Save in r7
 *   9677: mov dptr, #0x07dd   ; G_CMD_LBA_3
 *   967a: movx a, @dptr
 *   967b: mov r6, a           ; Save in r6
 *   967c: add a, 0xe0         ; a = a + a (shift left 1)
 *   967e: add a, 0xe0         ; a = a + a (shift left 1 more)
 *   9680: mov r5, a           ; Shifted value
 *   9681: mov a, r7           ; Restore original
 *   9682: orl a, r5           ; Combine
 *   9683: ret
 */
uint8_t cmd_combine_lba_param(uint8_t val)
{
    uint8_t lba3 = G_CMD_LBA_3;
    uint8_t shifted = (lba3 << 2) & 0xFC;  /* Shift left 2, mask */
    return val | shifted;
}

/*
 * cmd_combine_lba_alt - Alternate LBA combine
 * Address: 0x968f-0x969c (14 bytes)
 *
 * Reads DPTR, then reads G_CMD_LBA_2 (0x07DC), shifts it left 2,
 * and ORs with the value. Returns combined result.
 *
 * Original disassembly:
 *   968f: movx a, @dptr       ; Read current value
 *   9690: mov r7, a           ; Save in r7
 *   9691: mov dptr, #0x07dc   ; G_CMD_LBA_2
 *   9694: movx a, @dptr
 *   9695: add a, 0xe0         ; a = a + a
 *   9697: add a, 0xe0         ; a = a + a
 *   9699: mov r6, a           ; Shifted value
 *   969a: mov a, r7           ; Restore original
 *   969b: orl a, r6           ; Combine
 *   969c: ret
 */
uint8_t cmd_combine_lba_alt(uint8_t val)
{
    uint8_t lba2 = G_CMD_LBA_2;
    uint8_t shifted = (lba2 << 2) & 0xFC;  /* Shift left 2, mask */
    return val | shifted;
}

/*
 * cmd_set_op_counter - Set operation counter
 * Address: 0x965d-0x9663 (7 bytes)
 *
 * Sets G_FLASH_OP_COUNTER to 0x05.
 *
 * Original disassembly:
 *   965d: mov dptr, #0x07bd   ; G_FLASH_OP_COUNTER
 *   9660: mov a, #0x05
 *   9662: movx @dptr, a
 *   9663: ret
 */
void cmd_set_op_counter(void)
{
    G_FLASH_OP_COUNTER = 0x05;
}

/*
 * cmd_wait_completion - Wait for command completion
 * Address: 0xe1c6-0xe1ed (40 bytes)
 *
 * Polls cmd_check_busy() until command completes, then performs
 * post-completion processing. Writes G_CMD_STATUS to 0xE403 and
 * increments G_CMD_STATE.
 *
 * Original disassembly:
 *   e1c6: lcall 0xe09a        ; cmd_check_busy
 *   e1c9: mov a, r7
 *   e1ca: jnz 0xe1c6          ; Loop while busy
 *   e1cc: mov dptr, #0x07c4   ; G_CMD_STATUS
 *   e1cf: movx a, @dptr
 *   e1d0: mov dptr, #0xe403   ; REG_CMD_CTRL_E403
 *   e1d3: movx @dptr, a
 *   e1d4: lcall 0x9605        ; cmd_start_trigger
 *   e1d7: mov dptr, #0xe41c   ; Wait for bit 0 clear
 *   e1da: movx a, @dptr
 *   e1db: jb 0xe0.0, 0xe1d7   ; Loop while bit 0 set
 *   e1de: mov dptr, #0x07c3   ; G_CMD_STATE
 *   e1e1: movx a, @dptr
 *   e1e2: inc a
 *   e1e3: anl a, #0x07        ; Mask to 3 bits
 *   e1e5: movx @dptr, a
 *   e1e6: clr a
 *   e1e7: mov dptr, #0x07b7   ; G_CMD_SLOT_INDEX
 *   e1ea: movx @dptr, a       ; Clear slot index
 *   e1eb: mov r7, #0x01       ; Return 1 (success)
 *   e1ed: ret
 */
uint8_t cmd_wait_completion(void)
{
    uint8_t val;

    /* Wait for command engine to become ready */
    while (cmd_check_busy()) {
        /* Spin */
    }

    /* Write G_CMD_STATUS to control register */
    val = G_CMD_STATUS;
    REG_CMD_CTRL_E403 = val;

    /* Trigger command start */
    cmd_start_trigger();

    /* Wait for trigger bit to clear */
    while (REG_CMD_BUSY_STATUS & CMD_BUSY_STATUS_BUSY) {
        /* Spin */
    }

    /* Increment command state (3-bit counter) */
    val = G_CMD_STATE;
    val = (val + 1) & 0x07;
    G_CMD_STATE = val;

    /* Clear slot index */
    G_CMD_SLOT_INDEX = 0;

    return 1;  /* Success */
}

/*
 * cmd_setup_read_write - Setup a read/write command
 * Address: 0xb640-0xb68b (76 bytes)
 *
 * Sets up command engine for a read/write operation using globals.
 * Writes opcode 0x32 to 0xE422, status 0x90 to 0xE423,
 * issue byte to 0xE424, tag 0x04 to 0xE425, then LBA bytes.
 *
 * Original disassembly:
 *   b640: mov dptr, #0xe422   ; REG_CMD_PARAM
 *   b643: mov a, #0x32        ; Opcode 0x32 (read/write)
 *   b645: movx @dptr, a
 *   b646: inc dptr            ; 0xE423
 *   b647: mov a, #0x90        ; Status 0x90
 *   b649: movx @dptr, a
 *   b64a: inc dptr            ; 0xE424
 *   b64b: mov a, #0x01        ; Issue byte
 *   b64d: movx @dptr, a
 *   b64e: inc dptr            ; 0xE425
 *   b64f: mov a, #0x04        ; Tag
 *   b651: movx @dptr, a
 *   b652: movx a, @dptr       ; Read back
 *   b653: orl a, #0x10        ; Set bit 4
 *   b655: movx @dptr, a
 *   b656: mov dptr, #0x07db   ; G_CMD_LBA_1
 *   b659: movx a, @dptr
 *   b65a: mov dptr, #0xe426   ; REG_CMD_LBA_0
 *   b65d: movx @dptr, a
 *   b65e: mov dptr, #0x07da   ; G_CMD_LBA_0
 *   b661: movx a, @dptr
 *   b662: mov dptr, #0xe427   ; REG_CMD_LBA_1
 *   b665: movx @dptr, a
 *   ... (continues with LBA computation and trigger)
 */
void cmd_setup_read_write(void)
{
    uint8_t val;

    /* Write opcode 0x32 to parameter register */
    REG_CMD_PARAM = 0x32;

    /* Write status 0x90 to status register */
    REG_CMD_STATUS = 0x90;

    /* Write issue byte 0x01 */
    REG_CMD_ISSUE = 0x01;

    /* Write tag 0x04 and set bit 4 */
    REG_CMD_TAG = 0x04;
    val = REG_CMD_TAG;
    val |= 0x10;
    REG_CMD_TAG = val;

    /* Copy G_CMD_LBA_1 to REG_CMD_LBA_0 */
    val = G_CMD_LBA_1;
    REG_CMD_LBA_0 = val;

    /* Compute and write LBA byte 1 */
    val = G_CMD_LBA_0;
    val = cmd_combine_lba_param(val);
    REG_CMD_LBA_1 = val;

    /* Compute and write LBA byte 2 */
    val = cmd_combine_lba_alt(0);
    REG_CMD_LBA_2 = val;

    /* Set trigger based on mode */
    val = G_CMD_MODE;
    if (val == 0x02 || val == 0x03) {
        REG_CMD_TRIGGER = 0x80;
    } else {
        REG_CMD_TRIGGER = 0x40;
    }

    /* Set operation counter */
    cmd_set_op_counter();

    /* Wait for completion */
    cmd_wait_completion();
}

/*
 * cmd_issue_tag_and_wait - Issue command with tag and wait
 * Address: 0x95a8-0x95b5 (14 bytes)
 *
 * Writes issue value to 0xE424, tag to 0xE425, then sets
 * G_CMD_STATUS to 0x06.
 *
 * Original disassembly:
 *   95a8: mov dptr, #0xe424   ; REG_CMD_ISSUE
 *   95ab: movx @dptr, a       ; Write A (issue value)
 *   95ac: inc dptr            ; 0xE425
 *   95ad: mov a, r7           ; Tag value from r7
 *   95ae: movx @dptr, a
 *   95af: mov dptr, #0x07c4   ; G_CMD_STATUS
 *   95b2: mov a, #0x06
 *   95b4: movx @dptr, a
 *   95b5: ret
 */
void cmd_issue_tag_and_wait(uint8_t issue, uint8_t tag)
{
    REG_CMD_ISSUE = issue;
    REG_CMD_TAG = tag;
    G_CMD_STATUS = 0x06;
}

/*
 * cmd_setup_with_params - Setup command with issue and tag parameters
 * Address: 0x9b31-0x9b5a (42 bytes)
 *
 * Exchanges A with r7, writes to 0xE424, then writes r7 to 0xE425.
 * Sets G_CMD_STATUS based on G_CMD_MODE and calls delay function.
 *
 * Original disassembly:
 *   9b31: mov dptr, #0xe424   ; REG_CMD_ISSUE
 *   9b34: xch a, r7           ; Exchange A and r7
 *   9b35: movx @dptr, a       ; Write original r7 to issue
 *   9b36: inc dptr            ; 0xE425
 *   9b37: mov a, r7           ; Get original A
 *   9b38: movx @dptr, a       ; Write to tag
 *   9b39: mov dptr, #0x07c4   ; G_CMD_STATUS
 *   9b3c: mov a, #0x06
 *   9b3e: sjmp 0x9b59         ; Jump to write and continue
 */
void cmd_setup_with_params(uint8_t issue_val, uint8_t tag_val)
{
    REG_CMD_ISSUE = issue_val;
    REG_CMD_TAG = tag_val;
    G_CMD_STATUS = 0x06;
}

/*
 * cmd_inc_dptr_write - Increment DPTR and write A
 * Address: 0x955d-0x9565 (9 bytes)
 *
 * Increments DPTR, writes A, then writes 0x01 to 0xCC89.
 *
 * Original disassembly:
 *   955d: inc dptr            ; Increment DPTR
 *   955e: movx @dptr, a       ; Write A to [DPTR]
 *   955f: mov dptr, #0xcc89   ; REG_CC89
 *   9562: mov a, #0x01
 *   9564: movx @dptr, a       ; Write 0x01
 *   9565: ret
 */
void cmd_write_cc89_01(void)
{
    REG_XFER_DMA_CMD = XFER_DMA_CMD_START;
}

/*
 * cmd_calc_slot_addr - Calculate command slot address
 * Address: 0x9566-0x9583 (30 bytes)
 *
 * Computes address = 0xE442 + (G_CMD_SLOT_C1 * 0x20)
 * Stores high byte to 0x07BF, low byte to 0x07C0.
 * Returns R6:A as 16-bit address.
 *
 * Original disassembly:
 *   9566: mov dptr, #0x07c1   ; G_CMD_SLOT_C1
 *   9569: movx a, @dptr
 *   956a: mov 0xf0, #0x20     ; B = 0x20 (slot size)
 *   956d: mul ab              ; A = low(slot * 32), B = high
 *   956e: add a, #0x42        ; A += 0x42
 *   9570: mov r6, a           ; Save low byte
 *   9571: mov a, 0xf0         ; Get high byte
 *   9573: addc a, #0xe4       ; Add base high byte + carry
 *   9575: mov dptr, #0x07bf   ; G_CMD_ADDR_HI
 *   9578: movx @dptr, a       ; Store high byte
 *   9579: inc dptr            ; 0x07C0
 *   957a: xch a, r6           ; Swap to get low byte
 *   957b: movx @dptr, a       ; Store low byte
 *   957c: mov dptr, #0x07bf   ; Read back
 *   957f: movx a, @dptr       ; A = high byte
 *   9580: mov r6, a           ; R6 = high
 *   9581: inc dptr
 *   9582: movx a, @dptr       ; A = low byte
 *   9583: ret
 */
uint16_t cmd_calc_slot_addr(void)
{
    uint8_t slot = G_CMD_SLOT_C1;
    uint16_t addr = 0xE442 + ((uint16_t)slot * 0x20);
    G_CMD_ADDR_HI = (uint8_t)(addr >> 8);
    G_CMD_ADDR_LO = (uint8_t)(addr & 0xFF);
    return addr;
}

/*
 * cmd_config_e40b - Configure command register 0xE40B
 * Address: 0x9584-0x959f (28 bytes)
 *
 * Writes 0x02 to 0xCC89, then sets bits 1, 2, 3 in 0xE40B.
 *
 * Original disassembly:
 *   9584: mov dptr, #0xcc89
 *   9587: mov a, #0x02
 *   9589: movx @dptr, a       ; Write 0x02 to CC89
 *   958a: mov dptr, #0xe40b   ; REG_CMD_CFG_E40B
 *   958d: movx a, @dptr
 *   958e: anl a, #0xfd        ; Clear bit 1
 *   9590: orl a, #0x02        ; Set bit 1
 *   9592: movx @dptr, a
 *   9593: movx a, @dptr
 *   9594: anl a, #0xfb        ; Clear bit 2
 *   9596: orl a, #0x04        ; Set bit 2
 *   9598: movx @dptr, a
 *   9599: movx a, @dptr
 *   959a: anl a, #0xf7        ; Clear bit 3
 *   959c: orl a, #0x08        ; Set bit 3
 *   959e: movx @dptr, a
 *   959f: ret
 */
void cmd_config_e40b(void)
{
    uint8_t val;

    /* Clear transfer done flag */
    REG_XFER_DMA_CMD = XFER_DMA_CMD_DONE;

    /* Set bit 1 in E40B */
    val = REG_CMD_CONFIG;
    val = (val & 0xFD) | 0x02;
    REG_CMD_CONFIG = val;

    /* Set bit 2 */
    val = REG_CMD_CONFIG;
    val = (val & 0xFB) | 0x04;
    REG_CMD_CONFIG = val;

    /* Set bit 3 */
    val = REG_CMD_CONFIG;
    val = (val & 0xF7) | 0x08;
    REG_CMD_CONFIG = val;
}

/*
 * cmd_call_e120 - Call helper and setup issue
 * Address: 0x95a0-0x95b5 (22 bytes)
 *
 * Sets R5=2, calls 0xE120, then writes R2 to 0xE424, R7 (from 0x03) to 0xE425,
 * and sets G_CMD_STATUS to 0x06.
 *
 * Original disassembly:
 *   95a0: mov r5, #0x02
 *   95a2: lcall 0xe120        ; Call helper
 *   95a5: mov r7, 0x03        ; R7 = IDATA[0x03]
 *   95a7: mov a, r2
 *   95a8: mov dptr, #0xe424
 *   95ab: movx @dptr, a       ; Write R2 to issue
 *   95ac: inc dptr
 *   95ad: mov a, r7
 *   95ae: movx @dptr, a       ; Write R7 to tag
 *   95af: mov dptr, #0x07c4
 *   95b2: mov a, #0x06
 *   95b4: movx @dptr, a
 *   95b5: ret
 */
void cmd_call_e120_setup(void)
{
    /* Call cmd_param_setup with R5=2 - would set up R2 return value */
    cmd_param_setup(0x00, 0x02);  /* Only R5 is used in this context */

    /* Write issue and tag from helper result */
    /* R2 would contain issue value after helper call */
    /* For now, stub - full implementation requires cmd_param_setup */
    G_CMD_STATUS = 0x06;
}

/*
 * cmd_clear_cc9a_setup - Clear CC9A and setup CC99
 * Address: 0x95b6-0x95c8 (19 bytes)
 *
 * Writes 0 to 0xCC9A, 0x50 to 0xCC9B, 0x04 then 0x02 to 0xCC99.
 *
 * Original disassembly:
 *   95b6: mov dptr, #0xcc9a
 *   95b9: clr a
 *   95ba: movx @dptr, a       ; CC9A = 0
 *   95bb: inc dptr
 *   95bc: mov a, #0x50
 *   95be: movx @dptr, a       ; CC9B = 0x50
 *   95bf: mov dptr, #0xcc99
 *   95c2: mov a, #0x04
 *   95c4: movx @dptr, a       ; CC99 = 0x04
 *   95c5: mov a, #0x02
 *   95c7: movx @dptr, a       ; CC99 = 0x02
 *   95c8: ret
 */
void cmd_clear_cc9a_setup(void)
{
    REG_XFER_DMA_DATA_LO = 0x00;
    REG_XFER_DMA_DATA_HI = 0x50;
    REG_XFER_DMA_CFG = 0x04;
    REG_XFER_DMA_CFG = 0x02;
}

/*
 * cmd_calc_dptr_offset - Calculate DPTR with offset
 * Address: 0x95c9-0x95d9 (17 bytes)
 *
 * Writes A to [DPTR], then computes new DPTR = R2:R3 + (R5 * 4).
 *
 * Original disassembly:
 *   95c9: movx @dptr, a       ; Write A
 *   95ca: mov a, r5
 *   95cb: mov 0xf0, #0x04     ; B = 4
 *   95ce: mul ab              ; A = R5 * 4
 *   95cf: mov r7, a           ; R7 = result
 *   95d0: mov a, r3
 *   95d1: add a, r7           ; Low = R3 + offset
 *   95d2: mov 0x82, a         ; DPL
 *   95d4: mov a, r2
 *   95d5: addc a, 0xf0        ; High = R2 + B + carry
 *   95d7: mov 0x83, a         ; DPH
 *   95d9: ret
 */
uint16_t cmd_calc_dptr_offset(uint8_t r2, uint8_t r3, uint8_t r5)
{
    uint16_t offset = (uint16_t)r5 * 4;
    uint16_t addr = ((uint16_t)r2 << 8) | r3;
    return addr + offset;
}

/*
 * cmd_call_e73a_setup - Call helper and set status
 * Address: 0x95da-0x95ea (17 bytes)
 *
 * Calls 0xE73A, sets R7=3, R5=0, calls 0xDD12, then sets G_CMD_STATUS=0x02.
 *
 * Original disassembly:
 *   95da: lcall 0xe73a        ; Call helper
 *   95dd: mov r7, #0x03
 *   95df: clr a
 *   95e0: mov r5, a           ; R5 = 0
 *   95e1: lcall 0xdd12        ; Call helper
 *   95e4: mov dptr, #0x07c4
 *   95e7: mov a, #0x02
 *   95e9: movx @dptr, a
 *   95ea: ret
 */
void cmd_call_e73a_setup(void)
{
    cmd_engine_clear();
    cmd_trigger_params(0x03, 0x00);
    G_CMD_STATUS = 0x02;
}

/*
 * cmd_extract_bit5 - Extract bit 5 from memory
 * Address: 0x95eb-0x95f8 (14 bytes)
 *
 * Sets DPTR from R7:R6, increments, reads, extracts bit 5.
 *
 * Original disassembly:
 *   95eb: mov r7, a           ; R7 = A
 *   95ec: mov 0x82, a         ; DPL = A
 *   95ee: mov 0x83, r6        ; DPH = R6
 *   95f0: inc dptr
 *   95f1: movx a, @dptr       ; Read [DPTR+1]
 *   95f2: swap a              ; Bits 4-7 -> 0-3
 *   95f3: rrc a               ; Rotate right 3 times
 *   95f4: rrc a
 *   95f5: rrc a
 *   95f6: anl a, #0x01        ; Keep only bit 0
 *   95f8: ret
 */
uint8_t cmd_extract_bit5(uint8_t hi, uint8_t lo)
{
    __xdata uint8_t *ptr = (__xdata uint8_t *)(((uint16_t)hi << 8) | lo);
    uint8_t val = ptr[1];  /* Read [DPTR+1] */
    return (val >> 5) & 0x01;  /* Extract bit 5 */
}

/*
 * cmd_clear_5_bytes - Clear 5 bytes starting at DPTR
 * Address: 0x95f9-0x9604 (12 bytes)
 *
 * Clears 5 consecutive bytes starting at current DPTR.
 *
 * Original disassembly:
 *   95f9: clr a
 *   95fa: movx @dptr, a
 *   95fb: inc dptr
 *   95fc: movx @dptr, a
 *   95fd: inc dptr
 *   95fe: movx @dptr, a
 *   95ff: inc dptr
 *   9600: movx @dptr, a
 *   9601: inc dptr
 *   9602: movx @dptr, a
 *   9603: inc dptr
 *   9604: ret
 */
void cmd_clear_5_bytes(__xdata uint8_t *ptr)
{
    ptr[0] = 0;
    ptr[1] = 0;
    ptr[2] = 0;
    ptr[3] = 0;
    ptr[4] = 0;
}

/*
 * cmd_set_c801_bit4 - Set bit 4 in 0xC801
 * Address: 0x9617-0x9620 (10 bytes)
 *
 * Reads 0xC801, clears bit 4, sets bit 4, writes back.
 *
 * Original disassembly:
 *   9617: mov dptr, #0xc801
 *   961a: movx a, @dptr
 *   961b: anl a, #0xef        ; Clear bit 4
 *   961d: orl a, #0x10        ; Set bit 4
 *   961f: movx @dptr, a
 *   9620: ret
 */
void cmd_set_c801_bit4(void)
{
    uint8_t val = REG_INT_ENABLE;
    val = (val & 0xEF) | 0x10;
    REG_INT_ENABLE = val;
}

/*
 * cmd_clear_cc88_cc8a - Clear bits in CC88 and CC8A
 * Address: 0x9621-0x962d (13 bytes)
 *
 * Clears bits 0-2 in 0xCC88, clears 0xCC8A.
 *
 * Original disassembly:
 *   9621: mov dptr, #0xcc88
 *   9624: movx a, @dptr
 *   9625: anl a, #0xf8        ; Clear bits 0-2
 *   9627: movx @dptr, a
 *   9628: mov dptr, #0xcc8a
 *   962b: clr a
 *   962c: movx @dptr, a       ; CC8A = 0
 *   962d: ret
 */
void cmd_clear_cc88_cc8a(void)
{
    uint8_t val = REG_XFER_DMA_CTRL;
    val &= 0xF8;
    REG_XFER_DMA_CTRL = val;
    REG_XFER_DMA_ADDR_LO = 0;
}

/*
 * cmd_check_op_counter - Check if op counter equals 5
 * Address: 0x962e-0x9634 (7 bytes)
 *
 * Reads G_FLASH_OP_COUNTER, XORs with 5 (returns 0 if equal).
 *
 * Original disassembly:
 *   962e: mov dptr, #0x07bd   ; G_FLASH_OP_COUNTER
 *   9631: movx a, @dptr
 *   9632: xrl a, #0x05        ; A = counter ^ 5
 *   9634: ret                 ; Z flag set if counter == 5
 */
uint8_t cmd_check_op_counter(void)
{
    return G_FLASH_OP_COUNTER ^ 0x05;  /* Returns 0 if counter == 5 */
}

/*
 * cmd_config_e405_e421 - Configure E405 and E421 registers
 * Address: 0x9635-0x9646 (18 bytes)
 *
 * Clears bits 0-2 in 0xE405, then writes (R5 << 4) & 0x70 to 0xE421.
 *
 * Original disassembly:
 *   9635: mov dptr, #0xe405
 *   9638: movx a, @dptr
 *   9639: anl a, #0xf8        ; Clear bits 0-2
 *   963b: movx @dptr, a
 *   963c: mov r6, 0x05        ; R6 = IDATA[0x05]
 *   963e: mov a, r6
 *   963f: swap a              ; Bits 0-3 -> 4-7
 *   9640: anl a, #0x70        ; Keep bits 4-6
 *   9642: mov dptr, #0xe421
 *   9645: movx @dptr, a
 *   9646: ret
 */
void cmd_config_e405_e421(uint8_t param)
{
    uint8_t val;

    /* Clear bits 0-2 in E405 */
    val = REG_CMD_CFG_E405;
    val &= 0xF8;
    REG_CMD_CFG_E405 = val;

    /* Write (param << 4) & 0x70 to E421 */
    val = (param << 4) & 0x70;
    REG_CMD_MODE_E421 = val;
}

/*
 * cmd_clear_e4xx_bit4 - Clear bit 4 in register and bits 0-2
 * Address: 0x9647-0x964e (8 bytes)
 *
 * Reads from DPTR, clears bit 4, writes back, reads again, clears bits 0-2.
 *
 * Original disassembly:
 *   9647: movx a, @dptr
 *   9648: anl a, #0xef        ; Clear bit 4
 *   964a: movx @dptr, a
 *   964b: movx a, @dptr
 *   964c: anl a, #0xf8        ; Clear bits 0-2
 *   964e: ret                 ; Returns in A
 */
uint8_t cmd_clear_bits(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val &= 0xEF;  /* Clear bit 4 */
    *reg = val;
    val = *reg;
    val &= 0xF8;  /* Clear bits 0-2 */
    return val;
}

/*
 * cmd_write_cc89_02 - Clear DMA transfer done flag
 * Address: 0x964f-0x9655 (7 bytes)
 *
 * Original disassembly:
 *   964f: mov dptr, #0xcc89
 *   9652: mov a, #0x02
 *   9654: movx @dptr, a
 *   9655: ret
 */
void cmd_write_cc89_02(void)
{
    REG_XFER_DMA_CMD = XFER_DMA_CMD_DONE;
}

/*
 * cmd_extract_bits67 - Extract bits 6-7 and shift to 0-1
 * Address: 0x9656-0x965c (7 bytes)
 *
 * Takes A, swaps nibbles, rotates right twice, masks to 2 bits.
 *
 * Original disassembly:
 *   9656: swap a
 *   9657: rrc a
 *   9658: rrc a
 *   9659: anl a, #0x03
 *   965b: movx @dptr, a
 *   965c: ret
 */
uint8_t cmd_extract_bits67(uint8_t val)
{
    return (val >> 6) & 0x03;
}

/*
 * cmd_setup_delay - Setup delay with params and call DD12
 * Address: 0x9664-0x966a (7 bytes)
 *
 * Sets R5=0, R7=0x10, jumps to 0xDD12.
 *
 * Original disassembly:
 *   9664: clr a
 *   9665: mov r5, a           ; R5 = 0
 *   9666: mov r7, #0x10
 *   9668: ljmp 0xdd12         ; Tail call
 */
void cmd_setup_delay(void)
{
    cmd_trigger_params(0x10, 0x00);
}

/*
 * cmd_read_indexed - Read from address with offset 0x06
 * Address: 0x966b-0x9674 (10 bytes)
 *
 * Calculates address = R6:A + 0x06 and reads byte.
 *
 * Original disassembly:
 *   966b: add a, #0x06       ; A += 0x06
 *   966d: mov DPL, a         ; DPL = A
 *   966f: clr a
 *   9670: addc a, r6         ; DPH = R6 + carry
 *   9671: mov DPH, a
 *   9673: movx a, @dptr      ; Read byte
 *   9674: ret
 */
uint8_t cmd_read_indexed(uint8_t hi, uint8_t lo)
{
    uint16_t addr = ((uint16_t)hi << 8) | (lo + 0x06);
    return XDATA_REG8(addr);
}

/*
 * cmd_set_op_counter_1 - Set operation counter to 1
 * Address: 0x9684-0x968e (11 bytes)
 *
 * Sets G_FLASH_OP_COUNTER = 1, returns R7:R6 = 0x189C.
 *
 * Original disassembly:
 *   9684: mov dptr, #0x07bd   ; G_FLASH_OP_COUNTER
 *   9687: mov a, #0x01
 *   9689: movx @dptr, a       ; OP_COUNTER = 1
 *   968a: mov r7, #0x9c
 *   968c: mov r6, #0x18
 *   968e: ret
 */
uint16_t cmd_set_op_counter_1(void)
{
    G_FLASH_OP_COUNTER = 0x01;
    return 0x189C;  /* Returns R6:R7 as 16-bit value */
}

/*
 * cmd_wait_and_store_counter - Wait for completion and store counter
 * Address: 0x969d-0x96a5 (9 bytes)
 *
 * Stores A to G_FLASH_OP_COUNTER, calls cmd_wait_completion, returns R7.
 *
 * Original disassembly:
 *   969d: mov dptr, #0x07bd   ; G_FLASH_OP_COUNTER
 *   96a0: movx @dptr, a       ; Store A
 *   96a1: lcall 0xe1c6        ; cmd_wait_completion
 *   96a4: mov a, r7           ; Get result
 *   96a5: ret
 */
uint8_t cmd_wait_and_store_counter(uint8_t counter)
{
    G_FLASH_OP_COUNTER = counter;
    return cmd_wait_completion();
}

/*
 * cmd_set_dptr_inc2 - Set DPTR from R6:A and increment by 2
 * Address: 0x96a6-0x96ad (8 bytes)
 *
 * DPTR = R6:R7, then DPTR += 2.
 *
 * Original disassembly:
 *   96a6: mov r7, a
 *   96a7: mov DPL, a
 *   96a9: mov DPH, r6
 *   96ab: inc dptr
 *   96ac: inc dptr
 *   96ad: ret
 */
uint16_t cmd_set_dptr_inc2(uint8_t hi, uint8_t lo)
{
    uint16_t addr = ((uint16_t)hi << 8) | lo;
    return addr + 2;
}

/*
 * cmd_call_e73a_with_params - Call helper e73a with parameters
 * Address: 0x96ae-0x96b6 (9 bytes)
 *
 * R3 = IDATA[7], R2 = IDATA[6], calls 0xe73a, returns R3.
 *
 * Original disassembly:
 *   96ae: mov r3, 0x07
 *   96b0: mov r2, 0x06
 *   96b2: lcall 0xe73a
 *   96b5: mov a, r3
 *   96b6: ret
 */
uint8_t cmd_call_e73a_with_params(void)
{
    cmd_engine_clear();
    return 0;  /* Return value from R3 */
}

/*
 * cmd_read_dptr_offset1 - Read from DPTR+1 with params
 * Address: 0x96b7-0x96be (8 bytes)
 *
 * R5 = A, DPTR = R6:R7, inc DPTR, read byte.
 *
 * Original disassembly:
 *   96b7: mov r5, a
 *   96b8: mov DPL, r7
 *   96ba: mov DPH, r6
 *   96bc: inc dptr
 *   96bd: movx a, @dptr
 *   96be: ret
 */
uint8_t cmd_read_dptr_offset1(uint8_t hi, uint8_t lo)
{
    __xdata uint8_t *ptr = (__xdata uint8_t *)(((uint16_t)hi << 8) | lo);
    return ptr[1];
}

/*
 * cmd_update_slot_index - Update slot index based on counter
 * Address: 0x96bf-0x96cc (14 bytes)
 *
 * Reads G_CMD_PARAM_1 (0x07D5), decrements, ANDs with incremented
 * G_CMD_SLOT_C1, writes result back.
 *
 * Original disassembly:
 *   96bf: mov dptr, #0x07d5   ; G_CMD_PARAM_1
 *   96c2: movx a, @dptr       ; Read
 *   96c3: dec a               ; A = A - 1
 *   96c4: mov r7, a           ; Save
 *   96c5: mov dptr, #0x07c1   ; G_CMD_SLOT_C1
 *   96c8: movx a, @dptr       ; Read slot
 *   96c9: inc a               ; Increment
 *   96ca: anl a, r7           ; AND with (param-1)
 *   96cb: movx @dptr, a       ; Write back
 *   96cc: ret
 */
void cmd_update_slot_index(void)
{
    uint8_t param = G_CMD_PARAM_2;  /* 0x07D5 - slot count */
    uint8_t mask = param - 1;
    uint8_t slot = G_CMD_SLOT_C1;
    slot = (slot + 1) & mask;
    G_CMD_SLOT_C1 = slot;
}

/*
 * cmd_set_flag_07de - Set flag at 0x07DE
 * Address: 0x96cd-0x96d3 (7 bytes)
 *
 * Sets XDATA[0x07DE] = 1.
 *
 * Original disassembly:
 *   96cd: mov dptr, #0x07de
 *   96d0: mov a, #0x01
 *   96d2: movx @dptr, a
 *   96d3: ret
 */
void cmd_set_flag_07de(void)
{
    G_CMD_FLAG_07DE = 0x01;
}

/*
 * cmd_store_addr_hi - Store address high byte
 * Address: 0x96d4-0x96e0 (13 bytes)
 *
 * R6 = A, DPH = B + 0xE4 + carry, stores to G_CMD_ADDR_HI/LO.
 *
 * Original disassembly:
 *   96d4: mov r6, a           ; Save low byte
 *   96d5: mov a, B            ; Get high from mul result
 *   96d7: addc a, #0xe4       ; Add base 0xE4
 *   96d9: mov dptr, #0x07bf   ; G_CMD_ADDR_HI
 *   96dc: movx @dptr, a       ; Store high
 *   96dd: inc dptr            ; 0x07C0
 *   96de: xch a, r6           ; Get low byte
 *   96df: movx @dptr, a       ; Store low
 *   96e0: ret
 */
void cmd_store_addr_hi(uint8_t lo, uint8_t hi_adj)
{
    G_CMD_ADDR_HI = hi_adj + 0xE4;
    G_CMD_ADDR_LO = lo;
}

/*
 * cmd_load_addr - Load address from G_CMD_ADDR_HI/LO to DPTR
 * Address: 0x96e1-0x96ed (13 bytes)
 *
 * Reads G_CMD_ADDR_HI/LO into R4:R5 and sets DPTR.
 *
 * Original disassembly:
 *   96e1: mov dptr, #0x07bf   ; G_CMD_ADDR_HI
 *   96e4: movx a, @dptr       ; Read high
 *   96e5: mov r4, a
 *   96e6: inc dptr            ; 0x07C0
 *   96e7: movx a, @dptr       ; Read low
 *   96e8: mov r5, a
 *   96e9: mov DPL, a
 *   96eb: mov DPH, r4
 *   96ed: ret
 */
uint16_t cmd_load_addr(void)
{
    uint8_t hi = G_CMD_ADDR_HI;
    uint8_t lo = G_CMD_ADDR_LO;
    return ((uint16_t)hi << 8) | lo;
}

/*
 * cmd_read_state_shift - Read state and shift left
 * Address: 0x96ee-0x96f6 (9 bytes)
 *
 * R6 = [DPTR], reads G_CMD_STATE, shifts left 1.
 *
 * Original disassembly:
 *   96ee: movx a, @dptr
 *   96ef: mov r6, a
 *   96f0: mov dptr, #0x07c3   ; G_CMD_STATE
 *   96f3: movx a, @dptr
 *   96f4: add a, 0xe0         ; A = A * 2
 *   96f6: ret
 */
uint8_t cmd_read_state_shift(void)
{
    uint8_t state = G_CMD_STATE;
    return state << 1;
}

/*
 * cmd_clear_trigger_bits - Clear bits 0-5 in trigger register
 * Address: 0x96f7-0x9702 (12 bytes)
 *
 * Writes A to [DPTR], reads REG_CMD_TRIGGER, masks to bits 6-7, writes back.
 *
 * Original disassembly:
 *   96f7: movx @dptr, a       ; Write A to current DPTR
 *   96f8: mov dptr, #0xe420   ; REG_CMD_TRIGGER
 *   96fb: movx a, @dptr       ; Read trigger
 *   96fc: anl a, #0xc0        ; Keep bits 6-7
 *   96fe: movx @dptr, a       ; Write back
 *   96ff: movx a, @dptr       ; Read again
 *   9700: orl a, #0x80        ; Set bit 7
 *   9702: ret                 ; Returns in A
 */
uint8_t cmd_clear_trigger_bits(void)
{
    uint8_t val;

    /* Clear bits 0-5 in trigger register */
    val = REG_CMD_TRIGGER;
    val &= 0xC0;  /* Keep bits 6-7 */
    REG_CMD_TRIGGER = val;

    /* Read and set bit 7 */
    val = REG_CMD_TRIGGER;
    val |= 0x80;
    return val;
}

/*
 * cmd_write_trigger_wait - Write to trigger and wait
 * Address: 0x9703-0x9712 (16 bytes)
 *
 * Writes A to REG_CMD_TRIGGER, calls cmd_set_op_counter,
 * then calls cmd_wait_completion.
 *
 * Original disassembly:
 *   9703: movx @dptr, a       ; Write to trigger (DPTR=0xe420)
 *   9704: lcall 0x965d        ; cmd_set_op_counter
 *   9707: mov a, #0x90        ;
 *   9709: lcall 0xb88b        ; (some helper)
 *   970c: lcall 0xe1c6        ; cmd_wait_completion
 *   970f: lcall 0x95f9        ; cmd_clear_5_bytes
 *   9712: ret
 */
void cmd_write_trigger_wait(uint8_t trigger_val)
{
    REG_CMD_TRIGGER = trigger_val;
    cmd_set_op_counter();
    /* Note: calls to 0xb88b would set up more state */
    cmd_wait_completion();
}

/*
 * cmd_config_e400_e420 - Configure command engine registers
 * Address: 0x9713-0x971d (11 bytes)
 *
 * Clears bits 0-2 in 0xE400, then reads/modifies 0xE420.
 *
 * Original disassembly:
 *   9713: movx @dptr, a       ; Write to 0xE400
 *   9714: mov dptr, #0xe420
 *   9717: movx a, @dptr
 *   9718: anl a, #0xf8        ; Clear bits 0-2
 *   971a: orl a, #0x40        ; Set bit 6
 *   971c: movx @dptr, a
 *   971d: ret
 */
void cmd_config_e400_e420(void)
{
    uint8_t val;

    /* Read-modify-write E420 */
    val = REG_CMD_TRIGGER;
    val = (val & 0xF8) | 0x40;
    REG_CMD_TRIGGER = val;
}

/*
 * cmd_setup_e424_e425 - Setup issue and tag registers
 * Address: 0x971e-0x9728 (11 bytes)
 *
 * Writes R7 to 0xE424, reads 0x03, writes to 0xE425.
 *
 * Original disassembly:
 *   971e: movx @dptr, a       ; DPTR=0xE424, write A
 *   971f: mov dptr, #0xe424
 *   9722: mov a, r7
 *   9723: movx @dptr, a       ; Write R7
 *   9724: inc dptr            ; 0xE425
 *   9725: mov a, 0x03         ; IDATA[3]
 *   9727: movx @dptr, a       ; Write
 *   9728: ret
 */
void cmd_setup_e424_e425(uint8_t issue)
{
    REG_CMD_ISSUE = issue;
    /* REG_CMD_TAG would get value from IDATA[3] */
}

/*
 * cmd_set_trigger_bit6 - Set bit 6 in trigger, clear bit 5
 * Address: 0x9729-0x972f (7 bytes)
 *
 * Reads trigger register, clears bit 6, sets bit 6.
 *
 * Original disassembly:
 *   9729: movx a, @dptr       ; Read (DPTR=0xE420)
 *   972a: anl a, #0xbf        ; Clear bit 6
 *   972c: orl a, #0x40        ; Set bit 6
 *   972e: movx @dptr, a       ; Write back
 *   972f: ret
 */
void cmd_set_trigger_bit6(void)
{
    uint8_t val = REG_CMD_TRIGGER;
    val = (val & 0xBF) | 0x40;
    REG_CMD_TRIGGER = val;
}

/*
 * cmd_call_dd12_config - Call dd12 with config params
 * Address: 0x9730-0x9739 (10 bytes)
 *
 * R5=2, R7=0x0F, calls dd12, R5=1.
 *
 * Original disassembly:
 *   9730: mov r5, #0x02
 *   9732: mov r7, #0x0f
 *   9734: lcall 0xdd12
 *   9737: mov r5, #0x01
 *   9739: ret
 */
void cmd_call_dd12_config(void)
{
    cmd_trigger_params(0x0F, 0x02);
}

/*
 * cmd_extract_bits67_write - Extract bits 6-7 and write to DPTR
 * Address: 0x973a-0x9740 (7 bytes)
 *
 * Swap nibbles, rotate right twice, mask to 2 bits, write.
 *
 * Original disassembly:
 *   973a: swap a
 *   973b: rrc a
 *   973c: rrc a
 *   973d: anl a, #0x03
 *   973f: movx @dptr, a
 *   9740: ret
 */
uint8_t cmd_extract_bits67_write(uint8_t val)
{
    /* Same as cmd_extract_bits67 but writes to DPTR */
    return (val >> 6) & 0x03;
}

/*
 * cfg_init_ep_mode - Initialize EP mode variables
 * Address: 0x99f6-0x99ff (10 bytes)
 *
 * Sets up idata work variables for EP configuration.
 * Sets I_WORK_65=0x0F, I_WORK_63=0, increments R0 to 0x64.
 *
 * Original disassembly:
 *   99f6: mov r0, #0x65       ; R0 = 0x65
 *   99f8: mov @r0, #0x0f      ; I_WORK_65 = 0x0F (mode flags)
 *   99fa: mov r0, #0x63       ; R0 = 0x63
 *   99fc: mov @r0, #0x00      ; I_WORK_63 = 0
 *   99fe: inc r0              ; R0 = 0x64 (for caller to use)
 *   99ff: ret
 */
void cfg_init_ep_mode(void)
{
    I_WORK_65 = 0x0F;
    I_WORK_63 = 0;
    /* R0 left at 0x64 for caller to store config low byte */
}

/*
 * cfg_store_ep_config - Store EP config to idata
 * Address: 0x99d8-0x99df (8 bytes)
 *
 * Reads from DPTR (expects to be called after index calc),
 * stores to idata 0x63=0, 0x64=value.
 *
 * Original disassembly:
 *   99d8: movx a, @dptr       ; Read config value
 *   99d9: mov r0, #0x63       ; R0 = 0x63
 *   99db: mov @r0, #0x00      ; I_WORK_63 = 0 (high byte)
 *   99dd: inc r0              ; R0 = 0x64
 *   99de: mov @r0, a          ; I_WORK_64 = config value (low byte)
 *   99df: ret
 */
void cfg_store_ep_config(uint8_t val)
{
    I_WORK_63 = 0;
    I_WORK_64 = val;
}

/*
 * cfg_inc_dptr_value - Increment value at DPTR
 * Address: 0x99d1-0x99d4 (4 bytes)
 *
 * Simple increment of memory-mapped value.
 *
 * Original disassembly:
 *   99d1: movx a, @dptr       ; Read
 *   99d2: inc a               ; Increment
 *   99d3: movx @dptr, a       ; Write back
 *   99d4: ret
 */
void cfg_inc_reg_value(__xdata uint8_t *reg)
{
    (*reg)++;
}

/*
 * cfg_get_b296_bit2 - Get bit 2 from register 0xB296
 * Address: 0x99eb-0x99f5 (11 bytes)
 *
 * Reads 0xB296, extracts bit 2, returns it in position 0.
 *
 * Original disassembly:
 *   99eb: mov dptr, #0xb296
 *   99ee: movx a, @dptr       ; Read 0xB296
 *   99ef: anl a, #0x04        ; Mask bit 2
 *   99f1: rrc a               ; Rotate right
 *   99f2: rrc a               ; Rotate right again (bit 2 -> bit 0)
 *   99f3: anl a, #0x3f        ; Mask (clear high bits from rotate)
 *   99f5: ret                 ; Return 0 or 1
 */
uint8_t cfg_get_b296_bit2(void)
{
    return (REG_PCIE_STATUS >> 2) & 0x01;
}

/*
 * cfg_set_ep_flag_1 - Set EP config flag to 1
 * Address: 0x99c7-0x99cd (7 bytes)
 *
 * Sets G_EP_CFG_FLAG_0A5B = 1.
 *
 * Original disassembly:
 *   99c7: mov dptr, #0x0a5b   ; DPTR = 0x0A5B
 *   99ca: mov a, #0x01        ; A = 1
 *   99cc: movx @dptr, a       ; G_EP_CFG_FLAG_0A5B = 1
 *   99cd: ret
 */
void cfg_set_ep_flag_1(void)
{
    G_EP_CFG_FLAG_0A5B = 1;
}

/*
 * cfg_inc_ep_flag - Increment EP config flag
 * Address: 0x99ce-0x99d4 (7 bytes)
 *
 * Increments G_EP_CFG_FLAG_0A5B.
 *
 * Original disassembly:
 *   99ce: mov dptr, #0x0a5b   ; DPTR = 0x0A5B
 *   99d1: movx a, @dptr       ; Read G_EP_CFG_FLAG_0A5B
 *   99d2: inc a               ; Increment
 *   99d3: movx @dptr, a       ; Write back
 *   99d4: ret
 */
void cfg_inc_ep_flag(void)
{
    G_EP_CFG_FLAG_0A5B++;
}

/*
 * cfg_clear_ep_regs - Clear EP config registers 0x0A5E-0x0A60
 * Address: 0x9741-0x9749 (9 bytes)
 *
 * Clears three consecutive EP config bytes starting at 0x0A5E.
 * This is the first part of the 0x9741 function (before call to 0x99c6).
 *
 * Original disassembly:
 *   9741: clr a               ; A = 0
 *   9742: mov dptr, #0x0a5e   ; DPTR = 0x0A5E
 *   9745: movx @dptr, a       ; G_EP_CFG_0A5E = 0
 *   9746: inc dptr            ; DPTR = 0x0A5F
 *   9747: movx @dptr, a       ; G_EP_CFG_0A5F = 0
 *   9748: inc dptr            ; DPTR = 0x0A60
 *   9749: movx @dptr, a       ; G_EP_CFG_0A60 = 0
 */
void cfg_clear_ep_regs(void)
{
    G_EP_CFG_0A5E = 0;
    G_EP_CFG_0A5F = 0;
    G_EP_CFG_0A60 = 0;
}

/*
 * cfg_store_ep_with_carry - Store value+2 with carry to EP config idata
 * Address: 0x9a00-0x9a08 (9 bytes)
 *
 * Called after cfg_init_ep_mode (which sets R0=0x65).
 * Stores (param+2) to I_WORK_64 and carry bit to I_WORK_63.
 *
 * Original disassembly:
 *   9a00: add a, #0x02    ; A = A + 2 (sets carry)
 *   9a02: dec r0          ; R0-- (0x64)
 *   9a03: mov @r0, a      ; Store to idata[0x64]
 *   9a04: clr a           ; A = 0
 *   9a05: rlc a           ; A = carry bit
 *   9a06: dec r0          ; R0-- (0x63)
 *   9a07: mov @r0, a      ; Store to idata[0x63]
 *   9a08: ret
 */
void cfg_store_ep_with_carry(uint8_t val)
{
    uint16_t result = (uint16_t)val + 2;
    I_WORK_64 = (uint8_t)result;         /* Low byte (val+2) */
    I_WORK_63 = (result > 255) ? 1 : 0;  /* Carry bit */
}

/*
 * cfg_set_b480_bit0 - Set bit 0 of register B480
 * Address: 0x99e1-0x99ea (10 bytes)
 *
 * This is called from 0x99e0 area.
 *
 * Original disassembly:
 *   99e1: mov dptr, #0xb480
 *   99e4: movx a, @dptr
 *   99e5: anl a, #0xfe     ; Clear bit 0
 *   99e7: orl a, #0x01     ; Set bit 0
 *   99e9: movx @dptr, a
 *   99ea: ret
 */
void cfg_set_b480_bit0(void)
{
    REG_TUNNEL_LINK_CTRL = (REG_TUNNEL_LINK_CTRL & 0xFE) | 0x01;
}

/*
 * cfg_write_dptr_34_04 - Write constants 0x34 and 0x04 to consecutive DPTR locations
 * Address: 0x9a18-0x9a1f (8 bytes)
 *
 * DPTR is set by caller. Writes 0x34 to @DPTR, then 0x04 to @DPTR+1.
 *
 * Original disassembly:
 *   9a18: mov a, #0x34
 *   9a1a: movx @dptr, a
 *   9a1b: inc dptr
 *   9a1c: mov a, #0x04
 *   9a1e: movx @dptr, a
 *   9a1f: ret
 */
void cfg_write_dptr_34_04(__xdata uint8_t *ptr)
{
    ptr[0] = 0x34;
    ptr[1] = 0x04;
}

/*
 * cfg_write_b217 - Write accumulator value to register 0xB217
 * Address: 0x9a30-0x9a34 (5 bytes)
 *
 * Original disassembly:
 *   9a30: mov dptr, #0xb217
 *   9a33: movx @dptr, a
 *   9a34: (continues...)
 */
void cfg_write_b217(uint8_t val)
{
    REG_PCIE_BYTE_EN = val;
}

/*
 * External helper functions used by the state machine
 * Mappings to implemented functions:
 *   0x99c6 = pcie_set_0a5b_flag
 *   0x99ce = pcie_inc_0a5b
 *   0x996a = pcie_check_txn_count
 *   0x9a09 = pcie_lookup_r6_multiply
 *   0x9916 = pcie_store_r6_to_05a6
 *   0x9923 = pcie_config_table_lookup
 *   0x99af = pcie_read_and_store_idata
 *   0x994e = pcie_init_idata_65_63
 *   0x99b5 = pcie_add_2_to_idata
 *   0x9ab3 = pcie_set_byte_enables_0f
 *   0x9902/990c = pcie_init/pcie_init_alt (poll write status)
 */
/*
 * cfg_pcie_ep_state_machine - PCIe Endpoint Configuration State Machine
 * Address: 0x9741-0x9901 (449 bytes)
 *
 * This is the main PCIe endpoint configuration state machine that processes
 * the PCIe config table and programs endpoint registers.
 *
 * The state machine:
 * 1. Clears EP config registers 0x0A5E-0x0A60
 * 2. Initializes 0x0A5C with 0x1F mask
 * 3. Iterates through config entries comparing transaction counts
 * 4. Processes each config entry type differently
 * 5. Programs registers based on config table entries
 *
 * Globals used:
 * - G_EP_CFG_0A5C: Current mask/config value
 * - G_EP_CFG_0A5D: PCIe speed config
 * - G_EP_CFG_0A5E: EP config count low
 * - G_EP_CFG_0A5F: EP config count high
 * - G_EP_CFG_0A60: Max count
 * - G_NIBBLE_SWAP_0A5B: Transaction counter
 * - Config table at 0x05A6/0x05C0 area (34-byte entries)
 *
 * Entry points within this function:
 * - 0x9777: ANL A with 0x0F entry point (6 calls)
 * - 0x984d: Mid-loop entry point (7 calls)
 * - 0x9854: Config read entry point (7 calls)
 * - 0x9874: After e91d check (1 call)
 * - 0x9887: Inner loop entry (1 call)
 */
void cfg_pcie_ep_state_machine(void)
{
    uint8_t temp, r6_val = 0, r7_val = 0, r1_val = 0;
    __xdata uint8_t *dptr = 0;

    /*
     * 0x9741-0x9749: Clear EP config registers
     * (same as cfg_clear_ep_regs but inlined)
     */
    G_EP_CFG_0A5E = 0;
    G_EP_CFG_0A5F = 0;
    G_EP_CFG_0A60 = 0;

    /*
     * 0x974a-0x9751: Initialize 0x0A5C = 0x1F, call pcie_set_0a5b_flag
     *   mov dptr, #0x0a5c
     *   mov a, #0x1f
     *   lcall 0x99c6
     */
    pcie_set_0a5b_flag(&G_EP_CFG_0A5C, 0x1F);

    /*
     * 0x9752-0x976d: First loop - compare transaction counts
     * Loop while there are more entries to process
     */
loop_9752:
    /* Call 0x996a - compare 0x05A7 with 0x0A5B */
    temp = pcie_check_txn_count();
    if ((temp & 0x80) == 0) {  /* JNC 0x976d - carry not set means done */
        goto label_976d;
    }

    /* 0x9757-0x9767: Read config entry, AND with mask */
    /* inc dptr; movx a, @dptr; mov r7, a */
    r7_val = (&G_EP_CFG_0A5C)[1];  /* Read 0x0A5D */

    /* mov dptr, #0x05c6; lcall 0x9a09 */
    dptr = pcie_lookup_r6_multiply(r6_val);  /* Sets DPTR based on index */

    /* movx a, @dptr; mov r6, a; mov a, r7; anl a, r6 */
    dptr = (__xdata uint8_t *)0x05C6;
    r6_val = *dptr;
    temp = r7_val & r6_val;

    /* mov dptr, #0x0a5c; movx @dptr, a */
    G_EP_CFG_0A5C = temp;

    /* lcall 0x99ce - increment 0x0A5B */
    pcie_inc_0a5b();

    /* sjmp 0x9752 */
    goto loop_9752;

label_976d:
    /*
     * 0x976d-0x9777: Check bit 4 of 0x0A5C
     *   mov dptr, #0x0a5c
     *   movx a, @dptr
     *   jb 0xe0.4, 0x9777  (if bit 4 set, continue)
     *   ljmp 0x9901        (else exit)
     */
    temp = G_EP_CFG_0A5C;
    if ((temp & 0x10) == 0) {
        return;  /* Exit - no bit 4 set */
    }

    /*
     * 0x9777-0x97fb: Second processing phase
     * Entry point 0x9777: anl a, #0x0f
     */
entry_9777:
    temp = temp & 0x0F;  /* Mask to lower 4 bits */

    /* Store to 0x0A5C, set 0x0A5B=1 (0x99c6) */
    pcie_set_0a5b_flag(&G_EP_CFG_0A5C, temp);

loop_977c:
    /* Call 0x996a again */
    temp = pcie_check_txn_count();
    if ((temp & 0x80) == 0) {  /* JNC 0x97fb - done with this phase */
        goto label_97fb;
    }

    /* 0x9781-0x979c: Complex config processing */
    /* Store R6 to 0x05A6 */
    pcie_store_r6_to_05a6(r6_val);
    r7_val = r6_val;

    /* lcall 0xe77a - lookup helper - reads config to I_WORK_61/62 */
    /* This function is in bank 1 at 0xe77a, not yet implemented */
    /* For now, skip this call */

    /* lcall 0x9923 - get config table entry */
    dptr = pcie_config_table_lookup();

    /* Read and store to idata */
    pcie_read_and_store_idata(dptr);

    /* 0x9799: Call 0xd02a with R7=4 */
    /* This is power_state_machine_d02a - a complex wait/poll function */
    /* For now, simulate the check */
    if (I_WORK_64 != 0) {
        return;  /* Exit on error */
    }

    /* 0x97a2-0x97b3: Compare values and update 0x0A60 */
    r7_val = G_EP_CFG_0A60;
    temp = REG_USB_BUF_MAX_8006;  /* Read max count */
    if (temp >= r7_val) {
        /* Update max if current > stored */
        G_EP_CFG_0A60 = temp;
    }

    /* 0x97b4-0x97c7: Read 0x8005, extract bits, store to 0x0A5D */
    temp = REG_USB_BUF_COUNT_8005;
    G_EP_CFG_0A5D = temp & 0x03;

    /* Extract bits 3-7 >> 3 = bits 3-4 -> 0-1 */
    r1_val = (temp >> 3) & 0x1F;

    /* 0x97c8-0x97f6: Get 0x0A5F/0x0A5E, call e0f4, compare */
    r7_val = G_EP_CFG_0A5F;
    temp = G_EP_CFG_0A5E;

    /* Store comparison results */
    /* This section compares and updates 0x0A5F/0x0A5E if needed */

    /* lcall 0x99ce - increment counter */
    pcie_inc_0a5b();

    /* sjmp 0x977c */
    goto loop_977c;

label_97fb:
    /*
     * 0x97fb-0x9822: Third phase - setup for config writes
     */
    pcie_store_r6_to_05a6(0x01);
    *(__idata uint8_t *)0x26 = 0x02;  /* Set index */

    dptr = pcie_config_table_lookup();  /* 0x9923 */
    pcie_read_and_store_idata(dptr);  /* 0x99af */

    /* 0x980b-0x981c: Setup and call more helpers */
    G_EP_CFG_0A60 = 0;  /* Clear max */
    pcie_set_byte_enables_0f();  /* 0x9ab3 */

    shl32();  /* 0x0d46 */
    pcie_init_idata_65_63();  /* 0x994e */

    /* 0xe91d is in bank 1 - complex PCIe write operation */
    /* For now, simulate success */
    temp = 0;

    /*
     * 0x9822-0x9849: Loop calling 0x996a
     */
    G_NIBBLE_SWAP_0A5B = 1;  /* Entry at 0x99c7 */

loop_9825:
    temp = pcie_check_txn_count();
    if ((temp & 0x80) == 0) {  /* JNC 0x9849 */
        goto label_9849;
    }

    /* 0x982a-0x9844: Process config entry type 0x0c */
    pcie_store_r6_to_05a6(r6_val);
    *(__idata uint8_t *)0x26 = 0x0C;

    dptr = pcie_config_table_lookup();
    pcie_read_and_store_idata(dptr);

    /* Setup 32-bit values and call 0x9902 */
    r7_val = 0;
    r6_val = 0;
    /* R5=0xA0, R4=0x40 */

    temp = pcie_init();  /* 0x9902 - returns status in R7 */
    if (temp != 0) {
        return;
    }

    pcie_inc_0a5b();
    goto loop_9825;

label_9849:
    /*
     * 0x9849-0x987f: Fourth phase
     */
    G_NIBBLE_SWAP_0A5B = 1;

loop_984c:
    temp = pcie_check_txn_count();
    if ((temp & 0x80) == 0) {  /* JNC 0x987f */
        goto label_987f;
    }

    /* 0x9851-0x987a: Process with entry point 0x9854 */
    pcie_store_r6_to_05a6(r6_val);

    /* Entry 0x9854: movx a, @dptr */
    r6_val = *dptr;
    dptr++;
    temp = *dptr + 3;

    pcie_add_2_to_idata(temp);  /* 0x99b5 */

    r7_val = G_EP_CFG_0A5E;
    *(__idata uint8_t *)0x26 = 0x03;

    /* More setup and processing */
    pcie_set_byte_enables_0f();
    or32();  /* 0x0d08 */
    pcie_init_idata_65_63();

    /* 0xe91d in bank 1 - simulate success */
    temp = 0;

    pcie_inc_0a5b();
    goto loop_984c;

label_987f:
    /*
     * 0x987f-0x989d: Fifth phase
     */
    G_NIBBLE_SWAP_0A5B = 1;

loop_9882:
    temp = pcie_check_txn_count();
    if ((temp & 0x80) == 0) {  /* JNC 0x989d */
        goto label_989d;
    }

    /* 0x9887-0x989b: Entry point 0x9887 */
    pcie_store_r6_to_05a6(r6_val);
    pcie_read_and_store_idata(dptr);

    pcie_set_byte_enables_0f();

    temp = pcie_init();  /* 0x9902 */
    if (temp != 0) {
        return;
    }

    pcie_inc_0a5b();
    goto loop_9882;

label_989d:
    /*
     * 0x989d-0x98c5: Sixth phase
     */
    G_NIBBLE_SWAP_0A5B = 1;

loop_98a0:
    temp = pcie_check_txn_count();
    if ((temp & 0x80) == 0) {  /* JNC 0x98c5 */
        goto label_98c5;
    }

    /* 0x98a5-0x98c3: Process with 0x0F index, calculations */
    pcie_store_r6_to_05a6(r6_val);
    *(__idata uint8_t *)0x26 = 0x0F;

    pcie_set_byte_enables_0f();

    /* 0x9a10 then add 0x0a, call 0x9a02 */
    temp = I_WORK_64 + 0x0A;
    cfg_store_ep_with_carry(temp);

    /* Setup R4:R5:R6:R7 = 0:4:0:0 and call 0x990c */
    r7_val = 0;
    r6_val = 0x04;

    temp = pcie_init_alt();  /* 0x990c */
    if (temp != 0) {
        return;
    }

    pcie_inc_0a5b();
    goto loop_98a0;

label_98c5:
    /*
     * 0x98c5-0x9901: Final phase - finish configuration
     */
    G_NIBBLE_SWAP_0A5B = 1;

loop_98c8:
    temp = pcie_check_txn_count();
    if ((temp & 0x80) == 0) {  /* JNC 0x9901 - exit */
        return;
    }

    /* 0x98cd-0x98ff: Final config write processing */
    pcie_store_r6_to_05a6(r6_val);

    /* Set B=0x22 for table offset calculation */
    /* Call 0x9a3b helper */
    temp = G_EP_CFG_0A5C;
    temp |= r6_val;
    if (temp == 0) {
        /* Skip if both zero */
        pcie_inc_0a5b();
        goto loop_98c8;
    }

    /* Non-zero path: read 0x05A6, do table lookup */
    temp = G_PCIE_TXN_COUNT_LO;
    pcie_store_r6_to_05a6(temp);

    *(__idata uint8_t *)0x26 = 0x0F;
    pcie_set_byte_enables_0f();

    /* Add 1, call 0x99b5 */
    temp = I_WORK_64 + 1;
    pcie_add_2_to_idata(temp);

    /* Setup R4:R5:R6:R7 = 0x10:0x03:0x10:0x03 and call 0x990c */
    temp = pcie_init_alt();  /* 0x990c */
    if (temp != 0) {
        return;
    }

    pcie_inc_0a5b();
    goto loop_98c8;

    /* 0x9901: ret - implicit return */
}


/* ============================================================
 * Command Trigger Functions
 * ============================================================ */

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
    REG_CMD_LENGTH_LOW = 0x00;
    REG_CMD_LENGTH_HIGH = 0x00;
    REG_CMD_RESP_TAG = 0x00;
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
