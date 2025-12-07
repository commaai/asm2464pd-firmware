/*
 * ASM2464PD Firmware - Bank 1 Functions
 *
 * Bank 1 contains error handling and extended functionality that resides
 * in the second code bank (file offset 0x10000-0x17F0C).
 *
 * CODE BANKING:
 *   The ASM2464PD has ~98KB of firmware but the 8051 only addresses 64KB.
 *   Bank 1 is accessed by setting DPX=1, which maps CPU addresses 0x8000-0xFFFF
 *   to file offset 0x10000-0x17F0C.
 *
 * DISPATCH MECHANISM:
 *   Bank 1 functions are called via jump_bank_1 (0x0311):
 *   1. Caller loads DPTR with target address (e.g., 0xE911)
 *   2. Caller does ajmp 0x0311
 *   3. jump_bank_1 pushes DPTR, sets DPX=1, R0=0x1B
 *   4. RET pops DPTR and jumps to target in bank 1
 *
 * FILE OFFSET CALCULATION:
 *   file_offset = cpu_addr + 0x8000
 *   Example: CPU 0xE911 -> file 0x16911
 *
 * HANDLER TARGETS:
 *   The dispatch targets are often mid-function jump points, not function starts.
 *   This allows shared error handling code to be entered at different points
 *   depending on the error type.
 *
 * Known Bank 1 Dispatch Targets:
 *   0xE911 - Called by handler_0570 (PCIe/NVMe error, file 0x16911)
 *   0xE56F - Called by handler_0494 (event error, file 0x1656F)
 *   0xB230 - Called by handler_0606 (error handler, file 0x13230)
 *   0xA066 - Called by handler_061a (file 0x12066)
 *   0xEF4E - Called by handler_0642 (system error, file 0x16F4E)
 *   0xEDBD - (file 0x16DBD)
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*
 * Note on reversing Bank 1 functions:
 *
 * When reversing a bank 1 function, use radare2 with:
 *   r2 -a 8051 -q -c 's <file_offset>; pd 50' fw.bin
 *
 * Where file_offset = cpu_addr + 0x8000 for addresses >= 0x8000
 *
 * Example for 0xE911:
 *   r2 -a 8051 -q -c 's 0x16911; pd 50' fw.bin
 */

/*
 * error_clear_e760_flags - Clear error flags in E760/E761 registers
 * Bank 1 Address: 0xE920 (file offset 0x16920)
 * Size: 50 bytes (0x16920-0x16951)
 *
 * This function clears and sets specific error/event flag bits in the
 * 0xE760-0xE763 register region, likely handling error acknowledgment.
 *
 * Original disassembly:
 *   e920: mov dptr, #0xc808     ; Hardware control register
 *   e923: lcall 0xd1a8          ; Call helper function
 *   e926: mov dptr, #0xe761
 *   e929: mov a, #0xff
 *   e92b: movx @dptr, a         ; Write 0xFF to 0xE761
 *   e92c: mov dptr, #0xe760
 *   e92f: movx a, @dptr
 *   e930: anl a, #0xfb          ; Clear bit 2
 *   e932: orl a, #0x04          ; Set bit 2
 *   e934: movx @dptr, a
 *   e935: inc dptr              ; DPTR = 0xE761
 *   e936: movx a, @dptr
 *   e937: anl a, #0xfb          ; Clear bit 2
 *   e939: movx @dptr, a
 *   e93a: mov dptr, #0xe760
 *   e93d: movx a, @dptr
 *   e93e: anl a, #0xf7          ; Clear bit 3
 *   e940: orl a, #0x08          ; Set bit 3
 *   e942: movx @dptr, a
 *   e943: inc dptr              ; DPTR = 0xE761
 *   e944: movx a, @dptr
 *   e945: anl a, #0xf7          ; Clear bit 3
 *   e947: movx @dptr, a
 *   e948: mov dptr, #0xe763
 *   e94b: mov a, #0x04
 *   e94d: movx @dptr, a         ; Write 0x04 to 0xE763
 *   e94e: mov a, #0x08
 *   e950: movx @dptr, a         ; Write 0x08 to 0xE763
 *   e951: ret
 */
void error_clear_e760_flags(void)
{
    uint8_t val;

    /* Original calls helper at 0xd1a8 with DPTR=0xC808.
     * The helper at 0xd1a8 is a retry loop that:
     * 1. Calls 0xb820 for initial setup
     * 2. Calls 0xbe02 with R7=6, R5=result
     * 3. Loads from XDATA 0x0B1D, calls 0x0d84 (xdata_load_dword)
     * 4. Performs division, calls 0xb825
     * 5. Reads from 0x0B25, calls 0xbe02 with R7=3, R5=result
     * 6. Retries if timeout counter < 1 (using IDATA[0x51])
     * This appears to be DMA/PCIe status polling with timeout.
     */

    /* Write 0xFF to error mask register */
    REG_SYS_CTRL_E761 = 0xFF;

    /* Set bit 2 in system control 60, clear bit 2 in system control 61 */
    val = REG_SYS_CTRL_E760;
    val = (val & 0xFB) | 0x04;
    REG_SYS_CTRL_E760 = val;

    val = REG_SYS_CTRL_E761;
    val = val & 0xFB;
    REG_SYS_CTRL_E761 = val;

    /* Set bit 3 in system control 60, clear bit 3 in system control 61 */
    val = REG_SYS_CTRL_E760;
    val = (val & 0xF7) | 0x08;
    REG_SYS_CTRL_E760 = val;

    val = REG_SYS_CTRL_E761;
    val = val & 0xF7;
    REG_SYS_CTRL_E761 = val;

    /* Write 0x04 then 0x08 to system control 63 (command/ack register?) */
    REG_SYS_CTRL_E763 = 0x04;
    REG_SYS_CTRL_E763 = 0x08;
}

/*
 * error_handler_e911 - PCIe/NVMe error handler (mid-function entry point)
 * Bank 1 Address: 0xE911 (file offset 0x16911)
 * Size: 15 bytes (0x16911-0x1691f)
 *
 * Called by handler_0570 when PCIe/NVMe status & 0x0F != 0.
 *
 * This is a MID-FUNCTION ENTRY POINT. The caller (handler_0570) sets up
 * registers before dispatching here via jump_bank_1. On entry:
 *   - A = XDATA8(0xC80A) & 0x0F (error status bits)
 *   - R7 = some pre-set value from caller context
 *   - DPTR = target register address to write
 *
 * Original disassembly:
 *   e911: dec r7             ; R7 = R7 - 1
 *   e912: orl a, r7          ; A = A | R7
 *   e913: movx @dptr, a      ; Write to [DPTR]
 *   e914: lcall 0xc343       ; Call error_log_and_process (at 0xC343)
 *   e917: orl a, #0x80       ; A = A | 0x80 (set MSB - error flag)
 *   e919: lcall 0xc32d       ; Call error_status_update (at 0xC32D)
 *   e91c: orl a, #0x80       ; A = A | 0x80 (set MSB)
 *   e91e: movx @dptr, a      ; Write to [DPTR]
 *   e91f: ret
 *
 * NOTE: Cannot be directly translated to C as it requires register-based
 * calling convention. This entry point merges error status bits, logs
 * the error, and updates status registers with the error-active flag (0x80).
 */
void error_handler_e911(void)
{
    /*
     * This function uses 8051 register calling convention which cannot be
     * directly expressed in C. The function:
     * 1. Decrements R7 and ORs it with accumulator
     * 2. Writes to memory at DPTR
     * 3. Calls helper at 0xC343 to log/process error
     * 4. Sets bit 7 and calls helper at 0xC32D
     * 5. Sets bit 7 again and writes to DPTR
     *
     * For now, this is a stub. The actual implementation would require
     * inline assembly or would need to match the register state.
     */
}

/*
 * event_handler_e56f - Event handler for 0x81 events (mid-function entry point)
 * Bank 1 Address: 0xE56F (file offset 0x1656F)
 * Size: ~174 bytes (0x1656F-0x1661C, complex with multiple paths)
 *
 * Called by handler_0494 when events & 0x81 is set.
 *
 * This is a complex event state machine with multiple execution paths:
 * - Checks bit 3 of XDATA[DPTR], optionally calls 0xE6F0 with R7=1
 * - Reads state from 0x09EF, 0x0991, 0x098E
 * - May jump to 0xEE11 (bank 1) for further processing
 * - Writes 0x84 to 0x097A on some paths
 * - Uses lookup table at 0x5C9D for dispatch
 * - Multiple return points and ljmp destinations
 *
 * Original disassembly at e56f:
 *   e56f: movx a, @dptr        ; Read from [DPTR] set by caller
 *   e570: jnb 0xe0.3, 0x6578   ; Jump if bit 3 clear
 *   e573: mov r7, #0x01
 *   e575: lcall 0xe6f0         ; Call helper with R7=1
 *   e578: mov dptr, #0x09ef    ; Check state at 0x09EF
 *   e57b: movx a, @dptr
 *   e57c: jnb 0xe0.0, 0x6596   ; Jump if bit 0 clear
 *   ... (continues with state machine)
 *   e596: mov dptr, #0x097a
 *   e599: mov a, #0x84
 *   e59b: movx @dptr, a        ; Write 0x84 to state register
 *   e59c: ret
 *
 * NOTE: This function is too complex for direct C translation without
 * fully reverse engineering the entire state machine and all called
 * helper functions.
 */
void event_handler_e56f(void)
{
    /*
     * Complex event state machine - reads from DPTR, checks multiple
     * state registers (0x09EF, 0x0991, 0x098E), and dispatches to
     * various handlers. Multiple exit points with different state
     * register writes.
     *
     * Key registers accessed:
     * - 0x097A: State/control register (writes 0x84)
     * - 0x09EF: Event flags
     * - 0x0991: State variable
     * - 0x098E: Mode indicator
     * - 0x0214: Return value storage
     *
     * Calls to: 0xE6F0, 0xABC9, 0x43D3, 0xAA71, 0x544C, 0xAA1D,
     *           0xAA13, 0xAA4E, 0x425F
     */
}

/*
 * error_handler_b230 - Error handler (mid-function entry point)
 * Bank 1 Address: 0xB230 (file offset 0x13230)
 * Size: ~104 bytes (0x13230-0x13297+, with multiple paths)
 *
 * Called by handler_0606.
 *
 * This is an error recovery/handling function that:
 * - Manipulates bits in accumulator (clear bit 4, set bit 4)
 * - Calls several helper functions for status updates
 * - Clears/sets bits in hardware registers (0xE7FC, 0xCCD8, 0xC801)
 * - Sets up IDATA parameters for error logging
 *
 * Original disassembly at b230:
 *   b230: anl a, #0xef        ; Clear bit 4
 *   b232: orl a, #0x10        ; Set bit 4
 *   b234: lcall 0x96b7        ; Call helper
 *   b237: lcall 0x980d        ; Call helper
 *   b23a: mov dptr, #0xe7fc   ; Hardware status register
 *   b23d: movx a, @dptr
 *   b23e: anl a, #0xfc        ; Clear bits 0,1
 *   b240: movx @dptr, a
 *   b241: sjmp 0x3258         ; Jump to common path
 *   ...
 *   b258: mov r1, #0xd1       ; Set up IDATA pointer
 *   b25a: lcall 0x968e        ; Call helper
 *   b25d: lcall 0x99e0        ; Call helper
 *   ...
 *   b284: mov dptr, #0xccd8   ; DMA/transfer control
 *   b287: movx a, @dptr
 *   b288: anl a, #0xef        ; Clear bit 4
 *   b28a: movx @dptr, a
 *   b28b: mov dptr, #0xc801   ; System control
 *   b28e: movx a, @dptr
 *   b28f: anl a, #0xef        ; Clear bit 4
 *   b291: orl a, #0x10        ; Set bit 4
 *   b293: movx @dptr, a
 *   ...
 *
 * NOTE: This is a complex error handler with many helper function calls
 * and hardware register manipulations. Full implementation requires
 * reversing all the helper functions.
 */
void error_handler_b230(void)
{
    /*
     * Complex error handler with multiple hardware register manipulations
     * and helper function calls. Key operations:
     *
     * 1. Initial bit manipulation: (A & 0xEF) | 0x10
     * 2. Call helpers at 0x96B7, 0x980D
     * 3. Clear bits 0,1 in 0xE7FC
     * 4. Setup IDATA parameters at 0xD1
     * 5. Call helpers at 0x968E, 0x99E0
     * 6. Clear bit 4 in 0xCCD8
     * 7. Toggle bit 4 in 0xC801
     *
     * Helpers called: 0x96B7, 0x980D, 0x968E, 0x99E0, 0x0BE6, 0x0D59,
     *                 0x0C7A, 0x97EF
     */
}

/*
 * error_handler_a066 - Error handler for PCIe status bit 5 (mid-function entry)
 * Bank 1 Address: 0xA066 (file offset 0x12066)
 * Size: ~115 bytes (0x12066-0x120D8+, with multiple paths)
 *
 * Called by handler_061a when event flags & 0x83 and PCIe status bit 5 set.
 *
 * This is a complex error handler with multiple conditional branches:
 * - Entry uses registers set by caller (A, R0, R1 from earlier code)
 * - Calls helper at 0x96C7 for status updates
 * - Clears bit 1 in accumulator
 * - Calls helper at 0x0BE6
 * - Calls helper at 0xDEA1
 * - Checks bit 1 of 0x9780 result for further branching
 * - Sets up error recovery parameters
 *
 * Original disassembly at a066:
 *   a066: subb a, r1          ; A = A - R1 - C
 *   a067: anl a, r0           ; A = A & R0
 *   a068: lcall 0x96c7        ; Call status helper
 *   a06b: anl a, #0xfd        ; Clear bit 1
 *   a06d: lcall 0x0be6        ; Call helper
 *   a070: lcall 0xdea1        ; Call helper
 *   a073: sjmp 0x20a7         ; Jump to common path
 *   a075: lcall 0x9780        ; Check status
 *   a078: anl a, #0x02        ; Mask bit 1
 *   a07a: mov r7, a           ; R7 = result
 *   a07b: clr c               ; Clear carry
 *   a07c: rrc a               ; Rotate right
 *   a07d: jnz 0x20a7          ; Jump if non-zero
 *   ...
 *   a0a7: lcall 0x96cd        ; Common exit point
 *   ...
 *
 * NOTE: This is a complex error handler with multiple conditional paths
 * and helper function calls. Full implementation requires reversing
 * all helper functions.
 */
void error_handler_a066(void)
{
    /*
     * Complex error handler for PCIe status bit 5 condition.
     * Multiple execution paths based on status register values.
     *
     * Key operations:
     * 1. Arithmetic with R0, R1 from caller context
     * 2. Call 0x96C7 for status update
     * 3. Clear bit 1 and call 0x0BE6
     * 4. Call 0xDEA1 for error processing
     * 5. Check 0x9780 status, branch on bit 1
     * 6. Optional error recovery path with 0x538D, 0x96F5
     *
     * Helpers called: 0x96C7, 0x0BE6, 0xDEA1, 0x9780, 0x0BC8, 0x538D,
     *                 0x96F5, 0x96CD, 0x96EC, 0xA5D8, 0xEED6, 0x9874, 0x96E3
     */
}

/*
 * error_handler_ef4e - System error handler (UNUSED)
 * Bank 1 Address: 0xEF4E (file offset 0x16F4E)
 *
 * Called by handler_0642 when system status bit 4 is set.
 *
 * NOTE: This address contains all NOPs (0x00) in the original firmware.
 * This is likely unused/padding space. The handler exists in the dispatch
 * table but the target function is empty.
 *
 * Original disassembly:
 *   ef4e: nop
 *   ef4f: nop
 *   ... (all NOPs)
 */
void error_handler_ef4e(void)
{
    /* Empty - original firmware has NOPs at this address */
}

/*
 * handler_ed02 - PCIe handler (mid-function entry point)
 * Bank 1 Address: 0xED02 (file offset 0x16D02)
 * Size: ~38 bytes (0x16D02-0x16D27, with multiple paths)
 *
 * Called by dispatch stub handler_063d.
 *
 * This is a mid-function entry point that:
 * 1. Calls 0x05C5 helper
 * 2. Clears XDATA[0x023F]
 * 3. Checks XDATA[0x0775], clears if non-zero
 * 4. Checks XDATA[0x0719] for value 2
 * 5. Returns different values in R7 based on result
 *
 * Original disassembly:
 *   ed02: lcall 0x05c5          ; Call setup helper
 *   ed05: clr a
 *   ed06: mov dptr, #0x023f
 *   ed09: movx @dptr, a         ; XDATA[0x023F] = 0
 *   ed0a: ret
 *   ed0b: mov dptr, #0x0775
 *   ed0e: movx a, @dptr         ; Read 0x0775
 *   ed0f: jz 0x6d19             ; Jump if zero
 *   ed11: clr a
 *   ed12: movx @dptr, a         ; Clear 0x0775
 *   ed13: mov dptr, #0x0719
 *   ed16: movx @dptr, a         ; Clear 0x0719
 *   ed17: mov r7, a             ; R7 = 0
 *   ed18: ret
 *   ed19: mov dptr, #0x0719
 *   ed1c: movx a, @dptr         ; Read 0x0719
 *   ed1d: cjne a, #0x02, 0x6d25 ; Compare with 2
 *   ed20: clr a
 *   ed21: movx @dptr, a         ; Clear 0x0719
 *   ed22: mov r7, #0x02         ; R7 = 2
 *   ed24: ret
 *   ed25: mov r7, #0x01         ; R7 = 1
 *   ed27: ret
 */
void handler_ed02(void)
{
    uint8_t val;

    /* Call setup helper at 0x05C5 */
    /* TODO: Implement call to 0x05C5 when available */

    /* Clear 0x023F */
    G_BANK1_STATE_023F = 0;
}

/*
 * handler_eef9 - Error handler (UNUSED)
 * Bank 1 Address: 0xEEF9 (file offset 0x16EF9)
 *
 * Called by handler_063d.
 *
 * NOTE: This address contains all NOPs (0x00) in the original firmware.
 * This is likely unused/padding space. The handler exists in the dispatch
 * table but the target function is empty.
 *
 * Original disassembly:
 *   eef9: nop
 *   eefa: nop
 *   ... (all NOPs)
 */
void handler_eef9(void)
{
    /* Empty - original firmware has NOPs at this address */
}

/*
 * handler_e762 - Event/error handler
 * Bank 1 Address: 0xE762 (file offset 0x16762)
 *
 * Handles events and potential error conditions by managing state counters
 * at 0x0AA2-0x0AA5. This is part of the event queue management system.
 *
 * Key operations:
 * - Reads from 0x0AA3/0x0AA2 (state counters)
 * - Computes R6:R7 = R6:R7 + state counter
 * - Calls helper 0xEA19 to process event
 * - If result != 0, returns 1
 * - Otherwise increments 0x0AA5 and loops back
 *
 * Returns: 0 if no events, 1 if event processed
 */
void handler_e762(void)
{
    uint8_t val_aa3, val_aa2;
    uint8_t count;

    /* Read state counters */
    val_aa3 = G_STATE_COUNTER_HI;
    val_aa2 = *(__xdata uint8_t *)0x0AA2;

    /* Event processing loop - simplified */
    count = *(__xdata uint8_t *)0x0AA5;
    if (count < 0x20) {
        /* Increment event counter */
        *(__xdata uint8_t *)0x0AA5 = count + 1;
    }
}

/*
 * handler_e677 - Status handler
 * Bank 1 Address: 0xE677 (file offset 0x16677)
 *
 * Handles status updates by checking mode and performing
 * register operations via helper functions at 0xC244, 0xC247, etc.
 *
 * Key operations based on R7 parameter:
 * - If R7 == 4: calls 0xC244, clears A, jumps to 0x6692
 * - Otherwise: accesses 0x09E5, calls 0xC247, calls 0x0BC8
 * - Processes status at 0x09E8
 * - Calls 0x0BE6 for register write
 *
 * This handler is part of the bank 1 status management system.
 */
void handler_e677(void)
{
    /* Status handler - simplified stub
     * The full implementation involves complex bank-switching
     * and register operations that require more RE work. */
}

