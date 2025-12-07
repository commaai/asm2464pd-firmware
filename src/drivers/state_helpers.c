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

/*
 * state_action_dispatch - Dispatch state action
 * Address: 0x2bea
 *
 * Takes action code in R7 and dispatches to appropriate handler.
 * Used by protocol_state_machine().
 */
void state_action_dispatch(uint8_t action_code)
{
    /* TODO: Implement actual dispatch logic from 0x2bea */
    (void)action_code;
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
    /* This function reads from DPTR (set by caller) and computes
     * a new address. In C we can't directly manipulate DPTR,
     * so we implement the semantics using globals. */
    /* The caller (handler_3adb) expects DPTR to point to state counter area */
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
    /* The function writes value to DPTR (set by caller)
     * then computes address 0x046A + param */
    (void)param;
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
    /* Reads 3 bytes from REG_WAIT_BIT location at 0x0461 */
    /* The result is used by caller for state calculations */
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
 * flash_func_1679 - Flash operation helper
 * Address: 0x1679
 *
 * Called during state transitions in handler_3adb.
 */
void flash_func_1679(void)
{
    /* TODO: Implement from address 0x1679 */
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
 * usb_func_1b14 - USB address helper function
 * Address: 0x1b14
 *
 * Takes single param, returns computed value.
 * Used by protocol.c core_handler_4ff2.
 */
uint8_t usb_func_1b14(uint8_t param)
{
    (void)param;
    /* TODO: Implement from address 0x1b14 */
    return 0;
}

/*
 * usb_func_1b20 - USB helper function
 * Address: 0x1b20
 *
 * Takes parameter (register number), returns value.
 */
uint8_t usb_func_1b20(uint8_t param)
{
    (void)param;
    /* TODO: Implement from address 0x1b20 */
    return 0;
}

/*
 * usb_func_1b23 - USB helper function
 * Address: 0x1b23
 *
 * Returns USB status value.
 */
uint8_t usb_func_1b23(void)
{
    /* TODO: Implement from address 0x1b23 */
    return 0;
}

/*
 * usb_reset_interface - Reset USB interface
 * Address: 0x1bc3
 *
 * Resets USB interface with given parameter.
 * Used by protocol.c core_handler_4ff2.
 */
void usb_reset_interface(uint8_t param)
{
    (void)param;
    /* TODO: Implement from address 0x1bc3 */
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
