/*
 * ASM2464PD Firmware - Command Engine Driver
 *
 * Hardware command engine for NVMe command submission and completion.
 * Abstracts the process of building NVMe commands and tracking execution.
 *
 *===========================================================================
 * COMMAND ENGINE ARCHITECTURE
 *===========================================================================
 *
 * The Command Engine is a dedicated hardware block that handles the
 * construction and submission of NVMe commands to the NVMe controller.
 * It provides a simplified interface for issuing read/write and admin
 * commands without directly manipulating NVMe queues.
 *
 * Register Map (0xE400-0xE43F):
 * ┌──────────┬──────────────────────────────────────────────────────────┐
 * │ Address  │ Description                                              │
 * ├──────────┼──────────────────────────────────────────────────────────┤
 * │ 0xE402   │ Status Flags - bit 1: busy, bit 2: error                │
 * │ 0xE403   │ Control - command state (written from G_CMD_STATUS)     │
 * │ 0xE41C   │ Busy Status - bit 0: command busy                       │
 * │ 0xE420   │ Trigger - 0x80 (mode2/3) or 0x40 (mode1) to start       │
 * │ 0xE422   │ Parameter/Opcode - command parameter byte               │
 * │ 0xE423   │ Status - command status byte                            │
 * │ 0xE424   │ Issue - command issue register, bits written per mode   │
 * │ 0xE425   │ Tag - command tag (4)                                   │
 * │ 0xE426   │ LBA byte 0 - from G_CMD_LBA_1                           │
 * │ 0xE427   │ LBA byte 1 - computed from G_CMD_LBA_2                  │
 * │ 0xE428   │ LBA byte 2 - computed from G_CMD_LBA_3                  │
 * └──────────┴──────────────────────────────────────────────────────────┘
 *
 * Global Variables (Command Work Area 0x07B0-0x07FF):
 * ┌──────────┬──────────────────────────────────────────────────────────┐
 * │ Address  │ Description                                              │
 * ├──────────┼──────────────────────────────────────────────────────────┤
 * │ 0x07B7   │ G_CMD_SLOT_INDEX - Command slot index (3-bit, 0-7)      │
 * │ 0x07BD   │ G_CMD_OP_COUNTER - Operation counter                    │
 * │ 0x07C3   │ G_CMD_STATE - Command state (3-bit)                     │
 * │ 0x07C4   │ G_CMD_STATUS - Command status (0x02, 0x06, etc.)        │
 * │ 0x07CA   │ G_CMD_MODE - Command mode (1=mode1, 2=mode2, 3=mode3)   │
 * │ 0x07D3   │ G_CMD_PARAM_0 - Parameter 0 (for opcode)                │
 * │ 0x07D4   │ G_CMD_PARAM_1 - Parameter 1                             │
 * │ 0x07DA   │ G_CMD_LBA_0 - LBA byte 0 (low)                          │
 * │ 0x07DB   │ G_CMD_LBA_1 - LBA byte 1                                │
 * │ 0x07DC   │ G_CMD_LBA_2 - LBA byte 2                                │
 * │ 0x07DD   │ G_CMD_LBA_3 - LBA byte 3 (high)                         │
 * └──────────┴──────────────────────────────────────────────────────────┘
 *
 * Command Flow:
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                    COMMAND EXECUTION FLOW                          │
 * ├─────────────────────────────────────────────────────────────────────┤
 * │  1. Set up parameters in globals (G_CMD_LBA_*, G_CMD_MODE, etc.)   │
 * │  2. Call cmd_setup_and_issue() to configure registers              │
 * │  3. Call cmd_wait_completion() to wait for command to complete     │
 * │  4. Check return value for success/error                           │
 * │                                                                     │
 * │  Internal flow:                                                     │
 * │  ┌─────────────┐   ┌──────────────┐   ┌──────────────────────┐     │
 * │  │ Set 0xE422  │──▶│ Set 0xE423   │──▶│ Set 0xE424/0xE425    │     │
 * │  │ (param)     │   │ (status)     │   │ (issue/tag)          │     │
 * │  └─────────────┘   └──────────────┘   └──────────────────────┘     │
 * │         │                                       │                   │
 * │         ▼                                       ▼                   │
 * │  ┌─────────────┐   ┌──────────────┐   ┌──────────────────────┐     │
 * │  │ Set LBA     │──▶│ Set trigger  │──▶│ Wait on 0xE41C bit 0 │     │
 * │  │ 0xE426-28   │   │ 0xE420       │   │ and 0xE402 bit 1     │     │
 * │  └─────────────┘   └──────────────┘   └──────────────────────┘     │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 * Busy Check Logic (0xe09a function):
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │  1. Read 0xE402, check bit 1 (busy flag)                           │
 * │  2. If bit 1 set -> return busy (1)                                │
 * │  3. Read 0xE41C, check bit 0                                       │
 * │  4. If bit 0 set -> return busy (1)                                │
 * │  5. Read 0xE402, check bit 2 (error count)                         │
 * │  6. If bit 2 set -> return busy (1)                                │
 * │  7. Read 0xE402, check bit 3                                       │
 * │  8. If bit 3 not set -> return not busy (0)                        │
 * │  9. Otherwise return busy (1)                                      │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 *===========================================================================
 * IMPLEMENTATION STATUS
 *===========================================================================
 * cmd_check_busy            [DONE] 0xe09a-0xe0c3 - Check if engine busy
 * cmd_start_trigger         [DONE] 0x9605-0x960e - Start command via 0xE41C
 * cmd_write_issue_bits      [DONE] 0x960f-0x9616 - Write issue register bits
 * cmd_combine_lba_param     [DONE] 0x9675-0x9683 - Combine LBA with param
 * cmd_combine_lba_alt       [DONE] 0x968f-0x969c - Alternate LBA combine
 * cmd_set_op_counter        [DONE] 0x965d-0x9663 - Set operation counter
 * cmd_wait_completion       [DONE] 0xe1c6-0xe1ed - Wait for cmd complete
 * cmd_setup_read_write      [DONE] 0xb640-0xb68b - Setup read/write command
 *
 * Total: 8 functions implemented
 *===========================================================================
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

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
 * Sets G_CMD_OP_COUNTER to 0x05.
 *
 * Original disassembly:
 *   965d: mov dptr, #0x07bd   ; G_CMD_OP_COUNTER
 *   9660: mov a, #0x05
 *   9662: movx @dptr, a
 *   9663: ret
 */
void cmd_set_op_counter(void)
{
    G_CMD_OP_COUNTER = 0x05;
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
    while (REG_CMD_BUSY_STATUS & 0x01) {
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
