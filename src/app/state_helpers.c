/*
 * ASM2464PD Firmware - State and Address Helper Functions
 *
 * Collection of small helper functions for address calculations,
 * state lookups, and data access patterns used throughout the firmware.
 *
 * These functions implement common patterns for:
 * - Computing addresses in various XDATA regions (0x00xx, 0x01xx, 0x04xx, 0x05xx, 0xCExx)
 * - Loading and storing multi-byte values
 * - State machine support (counters, flags, indices)
 *
 * ============================================================================
 * ADDRESS CALCULATION PATTERNS
 * ============================================================================
 *
 * The firmware uses several address regions with computed offsets:
 *
 *   0x00xx region (low XDATA):
 *     - 0x0007: Triple load source
 *     - 0x0059+offset: State array access
 *
 *   0x01xx region (mid XDATA):
 *     - 0x014E+IDATA[0x43]: Indexed state access
 *     - 0x0159+IDATA[0x43]: Indexed state write
 *     - 0x0171+IDATA[0x43]: Related state
 *
 *   0x04xx region (work area):
 *     - 0x0464: G_SYS_STATUS_PRIMARY
 *     - 0x0465: G_SYS_STATUS_SECONDARY
 *     - 0x0474-0x0475: State write targets
 *     - 0x045E: Triple load destination
 *
 *   0x05xx region (buffer/state):
 *     - 0x053D + (G_SYS_STATUS_SECONDARY * 0x14): State table
 *     - 0x05B4 + (index * 0x22): Array access (34-byte entries)
 *     - 0x05A6: G_PCIE_TXN_COUNT_LO
 *
 *   0xCExx region (SCSI/hardware):
 *     - 0xCE40+offset: Register array access
 *
 * ============================================================================
 * IDATA LOCATIONS USED
 * ============================================================================
 *
 *   0x3F: Offset modifier (used with IDATA[0x41])
 *   0x40: Temporary storage (used by multiple functions)
 *   0x41: Index or counter
 *   0x43: Base offset for 0x01xx calculations
 *   0x52: Base offset for 0x00xx calculations
 *
 * ============================================================================
 */

#include "../types.h"
#include "../sfr.h"
#include "../registers.h"
#include "../globals.h"

/*
 * state_get_table_entry - Get state table entry pointer
 * Address: 0x15dc-0x15ee (19 bytes)
 *
 * Computes: 0x053D + (XDATA[0x0465] * 0x14)
 * Used to access 20-byte (0x14) state table entries.
 *
 * Original disassembly:
 *   15dc: mov dptr, #0x0465
 *   15df: movx a, @dptr       ; A = G_SYS_STATUS_SECONDARY
 *   15e0: mov 0xf0, #0x14     ; B = 20
 *   15e3: mul ab              ; A = (index * 20) low, B = high
 *   15e4: add a, #0x3d        ; A = A + 0x3D
 *   15e6: mov 0x82, a         ; DPL = A
 *   15e8: clr a
 *   15e9: addc a, #0x05       ; DPH = 0x05 + carry
 *   15eb: mov 0x83, a
 *   15ed: movx a, @dptr       ; read value
 *   15ee: ret
 */
uint8_t state_get_table_entry(void)
{
    uint8_t index = G_SYS_STATUS_SECONDARY;
    uint16_t addr = 0x053D + ((uint16_t)index * 0x14);
    return *(__xdata uint8_t *)addr;
}

/*
 * state_calc_addr_ce40 - Calculate address in 0xCE40+ region
 * Address: 0x15ef-0x15f9 (11 bytes)
 *
 * Computes: 0xCE40 + R7
 * Used for accessing SCSI/hardware register array.
 *
 * Original disassembly:
 *   15ef: mov a, #0x40
 *   15f1: add a, r7           ; A = 0x40 + R7
 *   15f2: mov 0x82, a         ; DPL = A
 *   15f4: clr a
 *   15f5: addc a, #0xce       ; DPH = 0xCE + carry
 *   15f7: mov 0x83, a
 *   15f9: ret
 */
__xdata uint8_t *state_calc_addr_ce40(uint8_t offset)
{
    return (__xdata uint8_t *)(0xCE40 + offset);
}

/*
 * state_load_from_0007 - Load triple from XDATA[0x0007]
 * Address: 0x15fa-0x1601 (8 bytes)
 *
 * Loads 3 bytes from 0x0007 using xdata_load_triple, returns R1.
 *
 * Original disassembly:
 *   15fa: mov dptr, #0x0007
 *   15fd: lcall 0x0ddd        ; xdata_load_triple
 *   1600: mov a, r1
 *   1601: ret
 */
uint8_t state_load_from_0007(void)
{
    __xdata uint8_t *ptr = (__xdata uint8_t *)0x0007;
    /* Load 3 bytes, return middle byte (R1 in original) */
    return ptr[1];
}

/*
 * state_calc_difference - Calculate 3 - IDATA[0x40], return ptr to 0xCE40+result
 * Address: 0x1602-0x161a (25 bytes)
 *
 * Computes R7 = 3 - IDATA[0x40], R6 = 0 (16-bit subtraction)
 * Then calculates 0xCE40 + R7 as DPTR.
 *
 * Original disassembly:
 *   1602: clr c
 *   1603: mov a, #0x03
 *   1605: subb a, 0x40        ; A = 3 - IDATA[0x40]
 *   1607: mov r7, a           ; R7 = result
 *   1608: clr a
 *   1609: subb a, #0x00       ; A = 0 - borrow
 *   160b: mov r6, a           ; R6 = sign extension
 *   160c: mov a, #0x40
 *   160e: add a, r7           ; A = 0x40 + R7
 *   160f: mov 0x82, a         ; DPL
 *   1611: mov a, #0xce
 *   1613: addc a, r6          ; DPH = 0xCE + R6 + carry
 *   1615: mov 0x83, a
 *   1617: movx a, @dptr       ; read value
 *   1618: mov r7, a           ; return in R7
 *   1619: inc dptr
 *   161a: ret
 */
uint8_t state_calc_difference(void)
{
    int8_t diff = 3 - *(__idata uint8_t *)0x40;
    uint16_t addr = 0xCE40 + (uint8_t)diff;
    return *(__xdata uint8_t *)addr;
}

/*
 * state_store_and_calc_addr - Store to DPTR, calculate 0x04xx address
 * Address: 0x1659-0x1667 (15 bytes)
 *
 * Writes A to @DPTR (assumed pre-set), then calculates:
 * DPTR = 0x0400 + XDATA[0x0464] + 0x4E
 *
 * Original disassembly:
 *   1659: movx @dptr, a       ; store A to caller's DPTR
 *   165a: mov dptr, #0x0464
 *   165d: movx a, @dptr       ; A = G_SYS_STATUS_PRIMARY
 *   165e: add a, #0x4e        ; A = A + 0x4E
 *   1660: mov 0x82, a         ; DPL
 *   1662: clr a
 *   1663: addc a, #0x04       ; DPH = 0x04 + carry
 *   1665: mov 0x83, a
 *   1667: ret
 */
__xdata uint8_t *state_calc_addr_044e(void)
{
    uint8_t val = G_SYS_STATUS_PRIMARY;
    return (__xdata uint8_t *)(0x0400 + val + 0x4E);
}

/*
 * state_write_0474_and_calc - Write IDATA[0x41] to 0x0474, calculate offset
 * Address: 0x1586-0x15ab (38 bytes)
 *
 * Stores IDATA[0x41] to 0x0474, then calculates:
 * (IDATA[0x41] + IDATA[0x3F]) & 0x1F -> 0x0475
 * Then calculates: 0x0000 + 0x59 + IDATA[0x43] as DPTR
 * Then writes IDATA[0x41] to DPTR
 * Finally calculates: 0x014E + IDATA[0x43] as DPTR
 *
 * Original disassembly:
 *   1586: mov dptr, #0x0474
 *   1589: mov a, 0x41         ; A = IDATA[0x41]
 *   158b: movx @dptr, a       ; [0x0474] = A
 *   158c: add a, 0x3f         ; A = A + IDATA[0x3F]
 *   158e: anl a, #0x1f        ; A = A & 0x1F
 *   1590: inc dptr            ; DPTR = 0x0475
 *   1591: movx @dptr, a       ; [0x0475] = A
 *   1592: mov a, #0x59
 *   1594: add a, 0x43         ; A = 0x59 + IDATA[0x43]
 *   1596: mov 0x82, a         ; DPL
 *   1598: clr a
 *   1599: addc a, #0x00       ; DPH = carry
 *   159b: mov 0x83, a
 *   159d: mov a, 0x41         ; A = IDATA[0x41]
 *   159f: movx @dptr, a       ; write to 0x00xx
 *   15a0: mov a, #0x4e
 *   15a2: add a, 0x43         ; A = 0x4E + IDATA[0x43]
 *   15a4: mov 0x82, a
 *   15a6: clr a
 *   15a7: addc a, #0x01       ; DPH = 0x01 + carry
 *   15a9: mov 0x83, a
 *   15ab: ret
 */
void state_write_0474_and_calc(void)
{
    uint8_t val_41 = *(__idata uint8_t *)0x41;
    uint8_t val_3f = *(__idata uint8_t *)0x3F;
    uint8_t val_43 = *(__idata uint8_t *)0x43;
    uint8_t masked;
    __xdata uint8_t *ptr;

    /* Write to state helper storage */
    G_STATE_HELPER_41 = val_41;

    /* Calculate masked value and write to state helper 42 */
    masked = (val_41 + val_3f) & 0x1F;
    G_STATE_HELPER_42 = masked;

    /* Write val_41 to 0x0059 + IDATA[0x43] */
    ptr = (__xdata uint8_t *)(0x0059 + val_43);
    *ptr = val_41;

    /* Final DPTR would be 0x014E + IDATA[0x43] for caller's use */
}

/*
 * state_calc_addr_0171 - Calculate address 0x0171 + IDATA[0x43]
 * Address: 0x15b6-0x15c2 (13 bytes)
 *
 * Writes A to DPTR (pre-set), then calculates 0x0171 + IDATA[0x43].
 *
 * Original disassembly:
 *   15b6: movx @dptr, a       ; store to caller's DPTR
 *   15b7: mov a, #0x71
 *   15b9: add a, 0x43         ; A = 0x71 + IDATA[0x43]
 *   15bb: mov 0x82, a         ; DPL
 *   15bd: clr a
 *   15be: addc a, #0x01       ; DPH = 0x01 + carry
 *   15c0: mov 0x83, a
 *   15c2: ret
 */
__xdata uint8_t *state_calc_addr_0171(void)
{
    uint8_t val_43 = *(__idata uint8_t *)0x43;
    return (__xdata uint8_t *)(0x0171 + val_43);
}

/*
 * state_calc_addr_00c2 - Calculate address from IDATA[0x52] region
 * Address: 0x15c3-0x15db (25 bytes)
 *
 * Calculates two addresses from IDATA[0x52] base:
 * First: 0x00C2 + IDATA[0x52] -> read value to R6
 * Second: 0x009F + IDATA[0x52] -> return DPTR
 *
 * Original disassembly:
 *   15c3: mov a, #0xc2
 *   15c5: add a, 0x52         ; A = 0xC2 + IDATA[0x52]
 *   15c7: mov 0x82, a         ; DPL
 *   15c9: clr a
 *   15ca: addc a, #0x00       ; DPH = carry
 *   15cc: mov 0x83, a
 *   15ce: movx a, @dptr       ; read from 0x00C2+offset
 *   15cf: mov r6, a           ; R6 = value
 *   15d0: mov a, #0x9f
 *   15d2: add a, 0x52         ; A = 0x9F + IDATA[0x52]
 *   15d4: mov 0x82, a
 *   15d6: clr a
 *   15d7: addc a, #0x00       ; DPH = carry
 *   15d9: mov 0x83, a
 *   15db: ret
 */
uint8_t state_read_and_calc_00xx(uint8_t *out_ptr_addr)
{
    uint8_t val_52 = *(__idata uint8_t *)0x52;
    uint8_t val;

    /* Read from 0x00C2 + offset */
    val = *(__xdata uint8_t *)(0x00C2 + val_52);

    /* Return second address for caller */
    if (out_ptr_addr) {
        *out_ptr_addr = 0x9F + val_52;
    }

    return val;
}

/*
 * state_calc_addr_05b4 - Calculate 0x05B4 + index * 0x22
 * Address: 0x1579-0x1585 (13 bytes)
 *
 * Reads value from 0x05A6, uses as index to calculate:
 * 0x05B4 + (index * 0x22)
 * Then jumps to dptr_index_mul at 0x0dd1.
 *
 * Original disassembly:
 *   1579: mov dptr, #0x05a6
 *   157c: movx a, @dptr       ; A = G_PCIE_TXN_COUNT_LO
 *   157d: mov dptr, #0x05b4   ; base address
 *   1580: mov 0xf0, #0x22     ; B = 34 (element size)
 *   1583: ljmp 0x0dd1         ; dptr_index_mul
 */
__xdata uint8_t *state_calc_addr_05b4_indexed(void)
{
    uint8_t index = G_PCIE_TXN_COUNT_LO;
    return (__xdata uint8_t *)(0x05B4 + (uint16_t)index * 0x22);
}

/*
 * state_load_triple_to_045e - Load triple to 0x045E region
 * Address: 0x1567-0x156e (8 bytes)
 *
 * Sets DPTR to 0x045E and calls xdata_load_triple, returns R1.
 *
 * Original disassembly:
 *   1567: mov dptr, #0x045e
 *   156a: lcall 0x0ddd        ; xdata_load_triple
 *   156d: mov a, r1           ; return middle byte
 *   156e: ret
 */
uint8_t state_load_triple_045e(void)
{
    __xdata uint8_t *ptr = (__xdata uint8_t *)0x045E;
    /* The xdata_load_triple returns R3:R2:R1, we want R1 */
    return ptr[1];
}

/*
 * state_add_and_store - Add offset and store via generic memory access
 * Address: 0x156f-0x1578 (10 bytes)
 *
 * Adds 0x28 to A, stores in R1, clears A, adds carry to R2,
 * moves R7 to A, then jumps to generic memory store at 0x0be6.
 *
 * Original disassembly:
 *   156f: add a, #0x28
 *   1571: mov r1, a
 *   1572: clr a
 *   1573: addc a, r2          ; propagate carry
 *   1574: mov r2, a
 *   1575: mov a, r7
 *   1576: ljmp 0x0be6         ; generic memory access
 */
void state_add_offset_0x28(uint8_t val, uint8_t hi_byte)
{
    uint16_t addr = ((uint16_t)hi_byte << 8) + val + 0x28;
    /* Generic memory access at 0x0be6 would use R1:R2 as address */
    (void)addr;  /* Actual implementation depends on 0x0be6 */
}

/*
 * state_add_and_access_0x0e - Add 0x0E offset and access memory
 * Address: 0x15ac-0x15b5 (10 bytes)
 *
 * Similar pattern: adds 0x0E to R1, propagates carry to R2,
 * then jumps to 0x0bc8 for memory access.
 *
 * Original disassembly:
 *   15ac: mov a, r1
 *   15ad: add a, #0x0e
 *   15af: mov r1, a
 *   15b0: clr a
 *   15b1: addc a, r2
 *   15b2: mov r2, a
 *   15b3: ljmp 0x0bc8
 */
void state_add_offset_0x0e(uint8_t *lo, uint8_t *hi)
{
    uint16_t addr = ((*hi) << 8) | (*lo);
    addr += 0x0E;
    *lo = addr & 0xFF;
    *hi = addr >> 8;
}

/*
 * ============================================================================
 * PROTOCOL STATE MACHINE HELPERS (for protocol.c)
 * ============================================================================
 */

/* External functions called by state_action_dispatch */
extern uint8_t helper_3f4a(void);  /* 0x3f4a - Returns status */
extern void helper_1d1d(void);     /* 0x1d1d - Setup helper */
extern uint8_t helper_1c9f(void);  /* 0x1c9f - Check status, returns NZ on success */
extern void helper_4f77(uint8_t param);  /* 0x4f77 - Processing helper */
extern uint8_t helper_11a2(uint8_t param);  /* 0x11a2 - Transfer helper, returns status */
extern void helper_5359(void);     /* 0x5359 - Buffer setup */
extern uint8_t helper_1cd4(void);  /* 0x1cd4 - Returns status with bit 1 flag */
extern void helper_1cc8(void);     /* 0x1cc8 - Register setup */
extern void helper_1c22(void);     /* 0x1c22 - Carry flag helper */

/*
 * state_action_dispatch - Dispatch state action
 * Address: 0x2bea-0x2f66 (893 bytes)
 *
 * This is a complex state machine dispatcher that:
 * 1. Stores action_code to 0x0A83
 * 2. Calls helper_3f4a to get initial status
 * 3. Based on status and flags, performs various state operations
 * 4. Returns status codes: 0 (success), 1, 3, 4, 5, or 0x80 (error/pending)
 *
 * Return values:
 * - 0: Action completed successfully
 * - 1: Pending, more processing needed
 * - 3: Error with bit 1 set
 * - 4: Error without bit 1
 * - 5: Transfer error
 * - 0x80: Error flag set
 *
 * Used by protocol_state_machine() in protocol.c.
 */
uint8_t state_action_dispatch(uint8_t action_code)
{
    uint8_t status;
    uint8_t action_flags;

    /* Store action code to global */
    G_ACTION_CODE_0A83 = action_code;

    /* Call initial status check - returns 0 on failure */
    status = helper_3f4a();
    *(__idata uint8_t *)0x3b = status;

    /* If status is 0, return immediately */
    if (status == 0) {
        return 0;  /* ret at 0x2bf8 */
    }

    /* Setup helper */
    helper_1d1d();

    /* Read back action code and check bit 1 */
    action_flags = G_ACTION_CODE_0A83;
    if (!(action_flags & ACTION_CODE_EXTENDED)) {
        /* Bit 1 not set - write 1 to 0x07EA */
        *(__xdata uint8_t *)0x07EA = 0x01;
    }

    /* Call status check helper */
    status = helper_1c9f();
    if (status == 0) {
        return 5;  /* ret at 0x2c11 with r7=5 */
    }

    /* Write R4:R5 to 0xC426:0xC427 (from helper_1c9f) */
    /* Read 4 bytes from 0xC4CC using helper_0d84 */
    /* Store to IDATA 0x09, then to IDATA 0x6B-0x6E */
    /* Call helper_0cab to compute */
    /* Store R4-R7 to IDATA 0x6F-0x72 */

    /* Check bit 1 of action code again */
    action_flags = G_ACTION_CODE_0A83;
    if (action_flags & ACTION_CODE_EXTENDED) {
        /* Bit 1 set - r7 = 0x80 */
        status = 0x80;
    } else {
        status = 0;
    }

    /* Call 4f77 with status parameter */
    helper_4f77(status);

    /* If helper returns 0 */
    action_flags = G_ACTION_CODE_0A83;
    if (action_flags & ACTION_CODE_EXTENDED) {
        /* Bit 1 set */
        return 3;  /* 0x2c55: r7=3 then ret */
    } else {
        return 4;  /* 0x2c50: r7=4 then ret */
    }

    /* Note: The actual function continues with much more complex logic
     * involving NVMe register manipulation, buffer management, and
     * state transitions. For now, this captures the key entry/exit behavior.
     *
     * The remaining code (0x2c58-0x2f66) handles:
     * - Write 0x0E to 0x0470
     * - Complex DMA and buffer setup via 0x1cc8
     * - Transfer operations via 0x11a2
     * - Status tracking in 0x0108, 0x012B
     * - NVMe register writes to 0xC4xx
     * - CE register operations (CE89, CE6C, CE3A, CE6E, CE00, CE01, CE60)
     * - State loop management with IDATA 0x3a-0x3e
     * - Final cleanup via 0x1d1d and return
     */
}

/*
 * transfer_func_16a2 - Read value and calculate address in 0x04XX region
 * Address: 0x16a2-0x16ad (12 bytes)
 *
 * Reads from current DPTR, calculates: DPTR = 0x0400 + value + 0x52
 * Sets R7 to the original value read.
 *
 * Original disassembly:
 *   16a2: movx a, @dptr      ; read from DPTR
 *   16a3: mov r7, a          ; save to R7
 *   16a4: add a, #0x52       ; add offset
 *   16a6: mov 0x82, a        ; DPL = result
 *   16a8: clr a
 *   16a9: addc a, #0x04      ; DPH = 0x04 + carry
 *   16ab: mov 0x83, a
 *   16ad: ret
 */
void transfer_func_16a2(void)
{
    /* This function reads value from 0x0AA4 (state counter low) and computes
     * an address in the 0x04xx region: DPTR = 0x0452 + value
     * This address points to state table entries.
     * The result is then used by subsequent operations.
     * Since we can't return DPTR in C, the caller must handle this directly. */
    uint8_t val = G_STATE_COUNTER_LO;
    /* Address would be 0x0452 + val, which points to state table at 0x0452-0x04FF */
    (void)val;
}

/*
 * transfer_func_16b7 - Write to DPTR and calculate address in 0x046X region
 * Address: 0x16b7-0x16c2 (12 bytes)
 *
 * Writes A to @DPTR, then calculates: DPTR = 0x046A + R7
 *
 * Original disassembly:
 *   16b7: movx @dptr, a      ; write A to current DPTR
 *   16b8: mov a, #0x6a       ; base offset
 *   16ba: add a, r7          ; add parameter
 *   16bb: mov 0x82, a        ; DPL = result
 *   16bd: clr a
 *   16be: addc a, #0x04      ; DPH = 0x04 + carry
 *   16c0: mov 0x83, a
 *   16c2: ret
 */
void transfer_func_16b7(uint8_t param)
{
    /* The function writes value to DPTR (set by previous call)
     * then computes address 0x046A + param for next operation.
     * This is typically used to write state to computed addresses.
     * param is typically the state counter value. */
    /* Write param value to state address at 0x046A + something */
    uint16_t addr = 0x046A + param;
    /* The caller chain uses this address for state tracking */
    (void)addr;
}

/*
 * transfer_func_17ed - Read 3 bytes from 0x0461
 * Address: 0x17ed-0x17f2 (6 bytes)
 *
 * Sets DPTR to 0x0461 and calls xdata_load_triple.
 * Returns R1:R2:R3 with triple value (we just use the result).
 *
 * Original disassembly:
 *   17ed: mov dptr, #0x0461
 *   17f0: ljmp 0x0ddd        ; xdata_load_triple
 */
void transfer_func_17ed(void)
{
    /* Reads 3 bytes from 0x0461-0x0463 (state wait counter area)
     * Using the xdata_load_triple pattern (R3, R2, R1).
     * The result is used by caller for state calculations.
     * In C, we can read the values but can't return in registers.
     * The caller chain in protocol.c handles these values. */
    __xdata uint8_t *ptr = (__xdata uint8_t *)0x0461;
    /* R3 = ptr[0], R2 = ptr[1], R1 = ptr[2] */
    (void)ptr;
}

/*
 * state_helper_15ac - Add 0x0E offset and jump to 0x0bc8
 * Address: 0x15ac-0x15b5 (10 bytes)
 *
 * Adds 0x0E to R1, propagates carry to R2, then jumps to 0x0bc8.
 * This modifies the address in R1:R2 by adding 0x0E.
 *
 * Original disassembly:
 *   15ac: mov a, r1
 *   15ad: add a, #0x0e
 *   15af: mov r1, a
 *   15b0: clr a
 *   15b1: addc a, r2
 *   15b2: mov r2, a
 *   15b3: ljmp 0x0bc8
 *
 * Returns bit 0 status (from the function at 0x0bc8).
 */
uint8_t state_helper_15ac(void)
{
    /* This function reads from computed address and returns status bit */
    /* The value is used to check state transitions */
    return 0;  /* Placeholder - actual value from state machine */
}

/*
 * state_helper_15af - Same as 15ac but entry at R1 assignment
 * Address: 0x15af (entry point within 15ac-15b5)
 *
 * Entry at 15af: mov r1, a (takes value in A, adds carry to R2)
 * Returns computed state value.
 */
uint8_t state_helper_15af(void)
{
    /* Entry point within state_helper_15ac */
    return 0;  /* Placeholder */
}

/*
 * flash_func_1679 - Address calculation helper
 * Address: 0x1679-0x1686 (14 bytes)
 *
 * Computes an address in the 0x04xx region:
 * DPTR = 0x0477 + (A * 4)
 * where A is the input value from accumulator.
 *
 * Original disassembly:
 *   1679: add a, 0xe0       ; A = A + A (multiply by 2)
 *   167b: add a, 0xe0       ; A = A + A (multiply by 4)
 *   167d: add a, #0x77      ; A = A + 0x77
 *   167f: mov 0x82, a       ; DPL = A
 *   1681: clr a
 *   1682: addc a, #0x04     ; DPH = 0x04 + carry
 *   1684: mov 0x83, a
 *   1686: ret
 *
 * Called during state transitions - returns pointer to
 * state table entry at 0x0477 + (index * 4).
 */
__xdata uint8_t *flash_func_1679_ptr(uint8_t index)
{
    /* Compute address: 0x0477 + (index * 4) */
    uint16_t addr = 0x0477 + ((uint16_t)index * 4);
    return (__xdata uint8_t *)addr;
}

/* Stub for compatibility with existing externs */
void flash_func_1679(void)
{
    /* This function is typically called via assembly with A set.
     * In our C implementation, callers should use flash_func_1679_ptr() instead.
     * This stub exists for link compatibility. */
}

/*
 * flash_func_0bc8 - Flash operation (does not return)
 * Address: 0x0bc8
 *
 * Called at end of state transition, never returns.
 */
void flash_func_0bc8(void)
{
    /* This function is marked noreturn - it likely does a reset
     * or jumps to a different execution context */
    while (1) {
        /* Halt - TODO: implement actual behavior */
    }
}

/*
 * reg_wait_bit_clear - Wait for register bit to clear
 * Address: 0x0461 region
 *
 * Waits for specified bit in register to clear with timeout.
 */
void reg_wait_bit_clear(uint16_t addr, uint8_t mask, uint8_t flags, uint8_t timeout)
{
    (void)addr;
    (void)mask;
    (void)flags;
    (void)timeout;
    /* TODO: Implement register polling with timeout */
}

/*
 * nvme_func_04da - Dispatch to bank 1 function via 0xE3B7
 * Address: 0x04da-0x04de (5 bytes)
 *
 * Sets DPTR to 0xE3B7 and jumps to bank switch handler at 0x0300.
 * This is a dispatch stub for bank 1 error/event handling.
 *
 * Original disassembly:
 *   04da: mov dptr, #0xe3b7
 *   04dd: ajmp 0x0300         ; bank switch handler
 */
void nvme_func_04da(uint8_t param)
{
    (void)param;
    /* Dispatch to bank 1 function at 0xE3B7 */
    /* TODO: Implement bank 1 call when bank1.c is complete */
}

/*
 * reg_wait_bit_set - Load 3 bytes from address to R3,R2,R1
 * Address: 0x0ddd-0x0de5 (9 bytes)
 *
 * This is a triple-byte load function that reads 3 consecutive
 * bytes from the address pointed to by DPTR into R3, R2, R1.
 * Despite the name, it's a load function not a wait function.
 *
 * Original disassembly:
 *   0ddd: movx a, @dptr       ; read byte 0
 *   0dde: mov r3, a           ; R3 = byte 0
 *   0ddf: inc dptr
 *   0de0: movx a, @dptr       ; read byte 1
 *   0de1: mov r2, a           ; R2 = byte 1
 *   0de2: inc dptr
 *   0de3: movx a, @dptr       ; read byte 2
 *   0de4: mov r1, a           ; R1 = byte 2
 *   0de5: ret
 *
 * Note: The return values are in R1-R3 which SDCC uses for
 * function return values in --model-large mode.
 */
void reg_wait_bit_set(uint16_t addr)
{
    /* Load 3 bytes from address - return values go to R1-R3 */
    volatile uint8_t b0 = *(__xdata uint8_t *)addr;
    volatile uint8_t b1 = *(__xdata uint8_t *)(addr + 1);
    volatile uint8_t b2 = *(__xdata uint8_t *)(addr + 2);
    (void)b0;
    (void)b1;
    (void)b2;
}

/*
 * usb_func_1b14 - USB address helper function
 * Address: 0x1b14-0x1b1f (12 bytes, falls through to 0x1b20)
 *
 * Takes param in A, computes DPTR from param + R2*256, reads 4 bytes
 * from that address, then writes to IDATA[0x12] and returns value at 0x0009.
 *
 * Original disassembly:
 *   1b14: mov r1, a            ; R1 = param (low byte)
 *   1b15: clr a
 *   1b16: addc a, r2           ; A = R2 + carry (high byte)
 *   1b17: mov 0x82, r1         ; DPL = R1 (param)
 *   1b19: mov 0x83, a          ; DPH = R2
 *   1b1b: lcall 0x0d84         ; Read 4 bytes from DPTR into R4-R7
 *   1b1e: mov r0, #0x12        ; R0 = 0x12
 *   1b20: lcall 0x0db9         ; Write R4-R7 to IDATA[0x12]
 *   (continues to 0x1b23)
 *
 * Used by protocol.c core_handler_4ff2.
 */
uint8_t usb_func_1b14(uint8_t param)
{
    /* Read 4 bytes from XDATA address (param as low byte, R2 as high)
     * For simplicity, assuming R2=0, so address is just param */
    __xdata uint8_t *src = (__xdata uint8_t *)(uint16_t)param;
    __idata uint8_t *dst = (__idata uint8_t *)0x12;

    /* Copy 4 bytes from XDATA to IDATA[0x12-0x15] */
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];

    /* Return value at 0x0009 (like usb_func_1b23) */
    return *(__xdata uint8_t *)0x0009;
}

/*
 * usb_func_1b20 - USB helper function / IDATA write
 * Address: 0x1b20-0x1b22 (3 bytes)
 *
 * Calls 0x0db9 which writes R4-R7 to 4 bytes at IDATA[R0].
 * The param is passed in R0 as the IDATA address.
 * Since this function just calls 0x0db9 and falls through to 0x1b23,
 * it writes to IDATA[param] and then returns the value at 0x0009.
 *
 * Original disassembly:
 *   1b20: lcall 0x0db9      ; Write R4-R7 to IDATA[@R0]
 *   1b23: (continues to usb_func_1b23)
 *
 * Note: The actual implementation continues into 1b23, so it:
 * 1. Writes 4 bytes to IDATA[param]
 * 2. Returns the value at XDATA 0x0009
 */
uint8_t usb_func_1b20(uint8_t param)
{
    /* Write 4 bytes to IDATA address specified by param */
    /* The values to write would be in R4-R7 from the caller */
    /* This is a pass-through to 0x0db9 - falls through to 1b23 */
    __idata uint8_t *dst = (__idata uint8_t *)param;

    /* For now, the write is stubbed as we don't have R4-R7 values */
    /* The function continues to usb_func_1b23 logic */
    (void)dst;

    /* Return the value at 0x0009 (like usb_func_1b23) */
    return *(__xdata uint8_t *)0x0009;
}

/*
 * usb_func_1b23 - USB helper function
 * Address: 0x1b23-0x1b2a (8 bytes)
 *
 * Reads 3 bytes from 0x0007-0x0009 and returns the third byte (0x0009).
 * This appears to read a stored address/value structure.
 *
 * Original disassembly:
 *   1b23: mov dptr, #0x0007
 *   1b26: lcall 0x0ddd      ; Read 3 bytes from DPTR into R3, R2, R1
 *   1b29: mov a, r1         ; Return R1 (third byte at 0x0009)
 *   1b2a: ret
 */
uint8_t usb_func_1b23(void)
{
    /* Read the third byte of the 3-byte value at 0x0007 */
    return *(__xdata uint8_t *)0x0009;
}

/*
 * usb_reset_interface - Set DPTR from param
 * Address: 0x1bc3-0x1bca (8 bytes)
 *
 * Takes param in A, computes DPTR from param (low) + R2 (high).
 * This is used to set up a pointer for subsequent operations.
 *
 * Original disassembly:
 *   1bc3: mov r1, a          ; R1 = param
 *   1bc4: clr a
 *   1bc5: addc a, r2         ; A = R2 + carry
 *   1bc6: mov 0x82, r1       ; DPL = param
 *   1bc8: mov 0x83, a        ; DPH = R2
 *   1bca: ret
 *
 * Used by protocol.c core_handler_4ff2.
 */
void usb_reset_interface(uint8_t param)
{
    /* This function sets up DPTR for subsequent operations.
     * In C we can't directly manipulate DPTR, but the effect
     * is to prepare for reading from address (R2:param).
     * The caller uses the result via DPTR reads. */
    (void)param;
    /* No actual state change needed - DPTR setup is implicit */
}

/*
 * xdata_load_dword_noarg - Load 32-bit value from current DPTR
 * Address: 0x0d84
 *
 * This is the void version called by protocol.c where DPTR is
 * set by the previous function call. In the original firmware,
 * this reads into R4-R7 registers.
 */
void xdata_load_dword_noarg(void)
{
    /* Original reads 4 bytes from DPTR into R4-R7.
     * Since we can't access DPTR directly in C, this is a stub.
     * The caller (core_handler_4ff2) doesn't use the return value. */
}

/* reg_wait_bit_set and nvme_func_04da are defined earlier in this file */

/*
 * handler_d07f - USB/NVMe command initialization handler
 * Address: 0xd07f
 *
 * Initializes command registers based on parameter.
 * Called during USB power initialization.
 *
 * From ghidra (simplified):
 *   if (param == 0) DAT_INTMEM_3e = 0xff; else DAT_INTMEM_3e = 0;
 *   Calls FUN_CODE_bb47 multiple times with register addresses
 *   Sets DAT_EXTMEM_9018 = (param == 0) ? 3 : 2
 *   Sets REG_USB_DATA_L = (param == 0) ? 0xfe : 0
 */
void handler_d07f(uint8_t param)
{
    uint8_t mode_val;
    uint8_t data_val;

    /* d07f: Set IDATA[0x3E] based on param */
    if (param != 0) {
        *(__idata uint8_t *)0x3E = 0;
    } else {
        *(__idata uint8_t *)0x3E = 0xFF;
    }

    /* d08a: Write 0xFF to various registers via helper_bb47 */
    /* These calls write 0xFF then read back with mask operations */
    /* 0xC430, 0xC440, 0x9096, 0x9097 all get 0xFF */
    *(__xdata uint8_t *)0xC430 = 0xFF;
    *(__xdata uint8_t *)0xC440 = 0xFF;
    *(__xdata uint8_t *)0x9096 = 0xFF;
    *(__xdata uint8_t *)0x9097 = 0xFF;

    /* d0a3: Write 3 to 0x9098 */
    *(__xdata uint8_t *)0x9098 = 3;

    /* d0a6: Write IDATA[0x3E] (0xFF or 0) via helper_bb44 */
    /* This writes to a register based on the mode */

    /* d0ab: More register writes */
    *(__xdata uint8_t *)0xC448 = 0xFF;
    *(__xdata uint8_t *)0x9011 = 0xFF;

    /* d0ba: Set mode based on param */
    if (param == 0) {
        mode_val = 3;
        data_val = 0xFE;
    } else {
        mode_val = 2;
        data_val = 0;
    }

    /* d0c1: Write mode to 0x9018 */
    REG_USB_MODE_9018 = mode_val;

    /* d0cd: Write data to 0x9010 */
    REG_USB_DATA_L = data_val;
}

/*
 * handler_e214 - NVMe queue configuration handler
 * Address: 0xe214
 *
 * Configures NVMe queue and related settings.
 * Called during USB power initialization.
 *
 * From ghidra:
 *   REG_NVME_QUEUE_CFG &= 0xf7 (clear bit 3)
 *   Calls FUN_CODE_bba8, FUN_CODE_bbaf, FUN_CODE_bb7e, etc.
 */
void handler_e214(void)
{
    uint8_t val;

    /* e214: Clear bit 3 of 0xC428 */
    val = *(__xdata uint8_t *)0xC428;
    *(__xdata uint8_t *)0xC428 = val & 0xF7;

    /* e21b: Call helpers for 0xC473 setup */
    /* These are register config helpers */
    /* helper_bba8, helper_bbaf, helper_bb7e, helper_bb47 */

    /* e22a: Read 0xC473, clear bit 5, set bit 5, write back via helper_bb6d */
    val = *(__xdata uint8_t *)0xC473;
    val = (val & 0xDF) | 0x20;
    *(__xdata uint8_t *)0xC473 = val;

    /* e235: Call helper_bb37 - additional cleanup */
}

/*
 * handler_e8ef - Power initialization completion handler
 * Address: 0xe8ef
 *
 * Handles completion of power initialization sequence.
 * Called after PHY polling completes.
 *
 * From ghidra: Complex state handling based on param value.
 */
void handler_e8ef(uint8_t param)
{
    (void)param;
    /* e8ef: Write 4 then 2 to 0xCC11 */
    *(__xdata uint8_t *)0xCC11 = 4;
    *(__xdata uint8_t *)0xCC11 = 2;
}

/*
 * Helper functions used by handler_2608
 */

/*
 * helper_1687 - Get queue entry DPTR from G_SYS_STATUS_PRIMARY
 * Address: 0x1687-0x1695 (15 bytes)
 *
 * Computes DPTR = 0x045A + G_SYS_STATUS_PRIMARY
 */
static __xdata uint8_t *helper_1687(void)
{
    uint8_t val = G_SYS_STATUS_PRIMARY;
    return (__xdata uint8_t *)(0x045A + val);
}

/*
 * helper_16de - Get queue data DPTR from IDATA[0x53]
 * Address: 0x16de-0x16e8 (11 bytes)
 *
 * Computes DPTR = 0x0466 + IDATA[0x53]
 */
static __xdata uint8_t *helper_16de(uint8_t idx)
{
    return (__xdata uint8_t *)(0x0466 + idx);
}

/*
 * helper_1633 - Set bit 0 of DMA status register
 * Address: 0x1633-0x1639 (7 bytes)
 */
static void helper_1633(void)
{
    uint8_t val = REG_DMA_STATUS;
    REG_DMA_STATUS = (val & ~DMA_STATUS_TRIGGER) | DMA_STATUS_TRIGGER;
}

/*
 * helper_15d0 - Get address 0x009F + IDATA[0x52]
 * Address: 0x15d0-0x15db (12 bytes)
 */
static __xdata uint8_t *helper_15d0(uint8_t idx)
{
    return (__xdata uint8_t *)(0x009F + idx);
}

/*
 * helper_179d - Get address 0x00C2 + IDATA[0x52]
 * Address: 0x179d-0x17a8 (12 bytes)
 */
static __xdata uint8_t *helper_179d(uint8_t idx)
{
    return (__xdata uint8_t *)(0x00C2 + idx);
}

/*
 * helper_1696 - Get address 0x04B7 + IDATA[0x55]
 * Address: 0x1696-0x16a1 (12 bytes)
 */
static __xdata uint8_t *helper_1696(uint8_t idx)
{
    return (__xdata uint8_t *)(0x04B7 + idx);
}

/*
 * helper_15c3 - Read from 0x00C2 + offset, return DPTR to 0x009F + offset
 * Address: 0x15c3-0x15db
 */
static uint8_t helper_15c3(uint8_t idx)
{
    /* Reads 0x00C2 + idx, sets DPTR to 0x009F + idx */
    return *(__xdata uint8_t *)(0x00C2 + idx);
}

/*
 * helper_15bb - Store to DPTR then compute 0x0171 + IDATA[0x52]
 * Address: 0x15bb-0x15c2
 */
static __xdata uint8_t *helper_15bb(uint8_t idx)
{
    return (__xdata uint8_t *)(0x0171 + idx);
}

/*
 * handler_280a - Queue processing helper
 * Address: 0x280a-0x2813 (10 bytes)
 *
 * Calls helper_523c with r3=3, r5=0x47, r7=0x0b
 */
extern void helper_523c(uint8_t r3, uint8_t r5, uint8_t r7);

static void handler_280a(void)
{
    helper_523c(0x03, 0x47, 0x0B);
}

/*
 * helper_53a7 - Setup DMA/buffer configuration
 * Address: 0x53a7
 */
extern void helper_53a7(void);

/*
 * helper_53c0 - Alternate DMA/buffer configuration
 * Address: 0x53c0
 */
extern void helper_53c0(void);

/*
 * helper_0206 - Generic helper function
 * Address: 0x0206
 */
extern void helper_0206(uint8_t r5, uint8_t r7);

/*
 * helper_45d0 - Transfer control helper
 * Address: 0x45d0
 */
extern void helper_45d0(uint8_t param);

/*
 * helper_0421 - Endpoint configuration
 * Address: 0x0421
 */
extern void helper_0421(uint8_t param);

/*
 * handler_2608 - DMA/buffer queue state handler
 * Address: 0x2608-0x2809 (513 bytes)
 *
 * This is a complex state machine handler that manages:
 * - DMA queue entries
 * - Buffer state tracking
 * - Endpoint configuration
 * - Queue synchronization
 *
 * Uses IDATA locations:
 * - 0x51: Queue index low (B80C)
 * - 0x52: Queue index high (B80D)
 * - 0x53: Entry index (5-bit, wraps at 0x1F)
 * - 0x54: Buffer status flags
 * - 0x55: Loop counter
 * - 0x56: Computed queue position
 */
void handler_2608(void)
{
    __idata uint8_t *i_entry_idx = (__idata uint8_t *)0x53;
    __idata uint8_t *i_queue_lo = (__idata uint8_t *)0x51;
    __idata uint8_t *i_queue_hi = (__idata uint8_t *)0x52;
    __idata uint8_t *i_buf_flags = (__idata uint8_t *)0x54;
    __idata uint8_t *i_loop_cnt = (__idata uint8_t *)0x55;
    __idata uint8_t *i_queue_pos = (__idata uint8_t *)0x56;
    __idata uint8_t *i_state_6a = (__idata uint8_t *)0x6A;

    uint8_t entry_idx, queue_flags_lo, queue_flags_hi;
    uint8_t buf_flags, sys_status;
    uint8_t r7 = 0;
    __xdata uint8_t *ptr;

handler_loop:
    /* Get entry index from 0x045A + G_SYS_STATUS_PRIMARY */
    ptr = helper_1687();
    entry_idx = *ptr;
    *i_entry_idx = entry_idx;

    /* Get buffer state value from helper_16de and store */
    ptr = helper_16de(entry_idx);
    G_BUFFER_STATE_0AA7 = *ptr;

    /* Check G_SYS_STATUS_PRIMARY */
    sys_status = G_SYS_STATUS_PRIMARY;
    if (sys_status != 0) {
        /* Add 0x20 to entry_idx for position */
        *i_queue_pos = entry_idx + 0x20;
    } else {
        *i_queue_pos = entry_idx;
    }

    /* Set bit 0 in DMA status register */
    helper_1633();

    /* Write queue position to DMA queue index */
    REG_DMA_QUEUE_IDX = *i_queue_pos;

    /* Read queue flags from B80E */
    queue_flags_lo = REG_PCIE_QUEUE_FLAGS_LO & PCIE_QUEUE_FLAG_VALID;

    /* Check if buffer state matches flags */
    if (G_BUFFER_STATE_0AA7 == queue_flags_lo) {
        /* Clear bit 0 in DMA status */
        REG_DMA_STATUS = REG_DMA_STATUS & ~DMA_STATUS_TRIGGER;
        goto handler_epilogue;
    }

    /* Read queue index bytes */
    *i_queue_lo = REG_PCIE_QUEUE_INDEX_LO;
    *i_queue_hi = REG_PCIE_QUEUE_INDEX_HI;

    /* Clear buffer state 0AA6 */
    G_BUFFER_STATE_0AA6 = 0;

    /* Check flags combination */
    queue_flags_lo = REG_PCIE_QUEUE_FLAGS_LO & ~PCIE_QUEUE_FLAG_VALID;
    queue_flags_hi = REG_PCIE_QUEUE_FLAGS_HI;

    if ((queue_flags_lo | queue_flags_hi) != 0) {
        /* Process queue entry - extract queue ID */
        uint8_t queue_id = (queue_flags_hi >> 1) & 0x07;

        /* Write to 0x04D7 + queue_lo */
        ptr = (__xdata uint8_t *)(0x04D7 + *i_queue_lo);
        *ptr = queue_id;

        /* Extract bit 7 flag from B80F */
        uint8_t bit7_flag = ((REG_PCIE_QUEUE_FLAGS_HI << 4) >> 4) & 0x80;
        bit7_flag |= (REG_PCIE_QUEUE_FLAGS_LO >> 1);

        /* Write to 0x04F7 + queue_lo */
        ptr = (__xdata uint8_t *)(0x04F7 + *i_queue_lo);
        *ptr = bit7_flag;

        /* Update buffer state */
        G_BUFFER_STATE_0AA6 = bit7_flag;
    }

    /* Check 0x0B3E state */
    if (G_STATE_CTRL_0B3E == 0x01) {
        G_STATE_CTRL_0B3F++;
    }

    /* Read buffer status from 0x0108 + queue_hi */
    ptr = (__xdata uint8_t *)(0x0108 + *i_queue_hi);
    buf_flags = *ptr;
    *i_buf_flags = buf_flags;

    /* Check for IDATA flag result */
    ptr = helper_15d0(*i_queue_hi);

    if (*ptr == 0x01) {
        /* Check bit 4 of buf_flags */
        if (buf_flags & 0x10) {
            r7 = 1;
            goto handler_final_check;
        }
        /* Call helper_179d and write 1 */
        ptr = helper_179d(*i_queue_hi);
        *ptr = 0x01;
        goto handler_final_check;
    } else {
        /* Different path - increment via helper_179d */
        ptr = helper_179d(*i_queue_hi);
        (*ptr)++;

        /* Read from 0x00E5 + queue_hi to temp */
        ptr = (__xdata uint8_t *)(0x00E5 + *i_queue_hi);
        /* Push/pop DPTR pattern - read value, OR with 0AA6, write back */
        uint8_t temp_val = *ptr;
        temp_val |= G_BUFFER_STATE_0AA6;
        *ptr = temp_val;
        G_BUFFER_STATE_0AA6 = temp_val;

        /* Check bit 4 of buf_flags */
        if (buf_flags & 0x10) {
            /* Bit 6 check */
            if (buf_flags & 0x40) {
                /* Write endpoint 0x0578 */
                G_DMA_ENDPOINT_0578 = *i_queue_lo;

                /* Call helper_15c3 and compare */
                uint8_t r6_val = helper_15c3(*i_queue_hi);
                ptr = helper_15d0(*i_queue_hi);
                if (*ptr != r6_val) {
                    r7 = 1;
                    goto handler_final_check;
                }
            } else {
                /* Check G_SCSI_CTRL (0x0171) */
                if (G_SCSI_CTRL > 0) {
                    /* Complex loop checking queue slots */
                    *i_loop_cnt = 0;

                    while (*i_loop_cnt < 0x20) {
                        ptr = helper_1696(*i_loop_cnt);
                        if (*ptr == 0xFF) {
                            /* Write queue_lo to helper_1696 slot */
                            ptr = helper_1696(*i_loop_cnt);
                            *ptr = *i_queue_lo;

                            /* Write loop_cnt to 0x053B */
                            G_NVME_STATE_053B = *i_loop_cnt;

                            /* Compare with helper_15c3 */
                            uint8_t r6_val = helper_15c3(*i_queue_hi);
                            if (*i_loop_cnt >= r6_val) {
                                goto handler_final_check;
                            }

                            /* Write loop_cnt to 0x053B */
                            G_NVME_STATE_053B = *i_loop_cnt;
                            break;
                        }
                        (*i_loop_cnt)++;
                    }
                } else {
                    /* Call helper_15c3 and compare */
                    uint8_t r6_val = helper_15c3(*i_queue_hi);
                    ptr = helper_15d0(*i_queue_hi);
                    if (*ptr != r6_val) {
                        r7 = 1;
                        goto handler_final_check;
                    }
                }
            }
        }
    }

handler_final_check:
    if (r7 == 0) {
        goto handler_advance;
    }

    /* r7 != 0 path - buffer state handling */
    buf_flags = *i_buf_flags;

    if (buf_flags & 0x40) {
        /* Bit 6 set - check 0AA6 */
        if (G_BUFFER_STATE_0AA6 == 0) {
            /* Write to C508 buffer config */
            REG_NVME_BUF_CFG = (REG_NVME_BUF_CFG & 0xC0) | *i_queue_hi;

            /* Write to 0x0AF5 */
            G_EP_DISPATCH_OFFSET = *i_queue_hi;

            /* Call helper_53a7 */
            helper_53a7();
        } else {
            /* Call handler_280a */
            handler_280a();

            /* Clear r5, set r7 = queue_hi, call helper_0206 */
            helper_0206(0, *i_queue_hi);
        }

        /* Write 0xFF to 0x0171 + queue_hi slot */
        ptr = helper_15bb(*i_queue_hi);
        *ptr = 0xFF;

        /* Write 0 to 0x0517 + queue_hi */
        ptr = (__xdata uint8_t *)(0x0517 + *i_queue_hi);
        *ptr = 0;
    } else {
        /* Bit 6 not set - check IDATA[0x6A] == 4 */
        if (*i_state_6a == 0x04) {
            if (G_BUFFER_STATE_0AA6 != 0) {
                handler_280a();
            }

            /* Call helper_53c0 */
            helper_53c0();

            /* Write 0x01 to 0x90A1 (USB signal) */
            REG_USB_SIGNAL_90A1 = 0x01;

            /* Set IDATA[0x6A] = 5 */
            *i_state_6a = 0x05;

            /* Clear loop counter */
            *i_loop_cnt = 0;

            /* Loop while loop_cnt < G_NVME_STATE_053B */
            while (*i_loop_cnt < G_NVME_STATE_053B) {
                ptr = helper_1696(*i_loop_cnt);
                *ptr = 0xFF;
                (*i_loop_cnt)++;
            }
        }
    }

    /* Check bit 2 of buf_flags */
    if (*i_buf_flags & 0x04) {
        helper_45d0(*i_queue_hi);
    }

handler_advance:
    /* Increment entry_idx, mask to 5 bits */
    entry_idx = (*i_entry_idx + 1) & 0x1F;
    *i_entry_idx = entry_idx;

    if (entry_idx != 0) {
        /* Store updated entry_idx and continue loop */
        goto handler_loop;
    }

    /* Entry wrapped - toggle buffer state 0AA7 */
    G_BUFFER_STATE_0AA7 ^= 0x01;
    goto handler_loop;

handler_epilogue:
    /* Check if entry_idx matches current */
    sys_status = G_SYS_STATUS_PRIMARY;
    ptr = (__xdata uint8_t *)(0x045A + sys_status);
    if (*ptr != *i_entry_idx) {
        /* Call helper_0421 with entry_idx */
        helper_0421(*i_entry_idx);

        /* Update pointer with new entry */
        ptr = helper_1687();
        *ptr = *i_entry_idx;

        /* Update buffer state from 0AA7 */
        uint8_t r6_val = G_BUFFER_STATE_0AA7;
        ptr = helper_16de(*i_entry_idx);
        *ptr = r6_val;
    }
}
