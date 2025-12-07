/*
 * ASM2464PD Firmware - Error Logging Driver
 *
 * Manages error log entries for debugging and diagnostics.
 * Error logs are stored in XRAM at 0x0584-0x05FF region as an array
 * of 10-byte entries.
 *
 * ============================================================================
 * ERROR LOG STRUCTURE
 * ============================================================================
 *
 * Log Entry Array: 0x0584 - 0x05FF (stored in 10-byte entries)
 * Each entry: 10 bytes total
 *   +0: Entry type/status
 *   +1-9: Entry-specific data (error codes, addresses, etc.)
 *
 * Global Variables:
 *   IDATA[0x51]: Current log entry index (0-based)
 *   IDATA[0x52]: Temporary storage during processing
 *   0x0464: Log index storage (G_SYS_STATUS_PRIMARY)
 *   0x0574: Log processing state
 *   0x0575: Log entry value
 *   0x06E5: Max log entries count
 *   0x0AA1: Current processed entry index
 *
 * ============================================================================
 * IMPLEMENTATION STATUS
 * ============================================================================
 *
 * [x] error_log_calc_entry_addr (0xC47F)
 * [x] error_log_get_array_ptr (0xC445)
 * [x] error_log_get_array_ptr_2 (0xC496)
 * [ ] error_log_process (0xC2F4) - complex loop, partial implementation
 *
 */

#include "../types.h"
#include "../sfr.h"
#include "../registers.h"
#include "../globals.h"

/* IDATA locations used by error logging */
#define IDATA_LOG_INDEX     0x51    /* Current log entry index */
#define IDATA_LOG_TEMP      0x52    /* Temporary storage */

/* Error log entry size */
#define ERROR_LOG_ENTRY_SIZE    10

/* Error log array base addresses */
#define ERROR_LOG_BASE_0x84     0x0584  /* Entry calculation base (+0x84) */
#define ERROR_LOG_BASE_0x87     0x0587  /* Entry calculation base (+0x87) */

/*
 * error_log_calc_entry_addr - Calculate address of error log entry field
 * Address: 0xC47F-0xC48E (16 bytes)
 *
 * Calculates: DPTR = 0x0500 + (IDATA[0x51] * 10) + 0x87
 * This returns a pointer to byte 3 of the current log entry.
 *
 * Original disassembly:
 *   c47f: mov a, 0x51         ; A = IDATA[0x51] (log index)
 *   c481: mov 0xf0, #0x0a     ; B = 10 (entry size)
 *   c484: mul ab              ; A = low(index * 10), B = high
 *   c485: add a, #0x87        ; A = A + 0x87
 *   c487: mov 0x82, a         ; DPL = A
 *   c489: clr a
 *   c48a: addc a, #0x05       ; A = 0x05 + carry
 *   c48c: mov 0x83, a         ; DPH = A
 *   c48e: ret                 ; returns DPTR = 0x05xx
 */
__xdata uint8_t *error_log_calc_entry_addr(void)
{
    uint8_t index = *(__idata uint8_t *)IDATA_LOG_INDEX;
    uint16_t offset = (uint16_t)index * ERROR_LOG_ENTRY_SIZE;
    return (__xdata uint8_t *)(0x0500 + offset + 0x87);
}

/*
 * error_log_get_array_ptr - Get pointer to error log array entry
 * Address: 0xC445-0xC44B (7 bytes)
 *
 * Sets DPTR to 0x05B4 and B to 0x22, then jumps to dptr_index_mul.
 * This computes: 0x05B4 + index * 0x22
 *
 * Original disassembly:
 *   c445: mov dptr, #0x05b4   ; base address
 *   c448: mov 0xf0, #0x22     ; B = 0x22 (34 bytes element size)
 *   c44b: ljmp 0x0dd1         ; dptr_index_mul
 */
__xdata uint8_t *error_log_get_array_ptr(uint8_t index)
{
    return (__xdata uint8_t *)(0x05B4 + (uint16_t)index * 0x22);
}

/*
 * error_log_get_array_ptr_2 - Get pointer to log entry field with offset
 * Address: 0xC496-0xC4A2 (13 bytes)
 *
 * Takes A as offset, computes address and reads value, stores to 0x05A6.
 * Address = 0x0500 + A, reads value, stores to G_PCIE_TXN_COUNT_LO.
 *
 * Original disassembly:
 *   c496: mov 0x82, a         ; DPL = A
 *   c498: clr a
 *   c499: addc a, #0x05       ; DPH = 0x05 + carry
 *   c49b: mov 0x83, a
 *   c49d: movx a, @dptr       ; read [0x05xx]
 *   c49e: mov dptr, #0x05a6   ; G_PCIE_TXN_COUNT_LO
 *   c4a1: movx @dptr, a       ; store value
 *   c4a2: ret
 */
void error_log_get_array_ptr_2(uint8_t offset)
{
    uint8_t val = XDATA8(0x0500 + offset);
    G_PCIE_TXN_COUNT_LO = val;
}

/*
 * error_log_calc_entry_addr_offset - Calculate log entry address with offset
 * Address: 0xC44F-0xC45E (16 bytes)
 *
 * Takes IDATA[0x21] as index, computes: 0x0500 + (index * 10) + 0x7E
 *
 * Original disassembly:
 *   c44f: mov a, 0x21         ; A = IDATA[0x21]
 *   c451: mov 0xf0, #0x0a     ; B = 10
 *   c454: mul ab              ; multiply
 *   c455: add a, #0x7e        ; A = A + 0x7E
 *   c457: mov 0x82, a         ; DPL = A
 *   c459: clr a
 *   c45a: addc a, #0x05       ; DPH = 0x05 + carry
 *   c45c: mov 0x83, a
 *   c45e: ret
 */
__xdata uint8_t *error_log_calc_entry_addr_offset(void)
{
    uint8_t index = *(__idata uint8_t *)0x21;
    uint16_t offset = (uint16_t)index * ERROR_LOG_ENTRY_SIZE;
    return (__xdata uint8_t *)(0x0500 + offset + 0x7E);
}

/*
 * error_log_set_status - Set error log processing status
 * Address: 0xC48F-0xC495 (7 bytes)
 *
 * Writes 0x04 to REG_PCIE_STATUS (0xB296).
 *
 * Original disassembly:
 *   c48f: mov dptr, #0xb296
 *   c492: mov a, #0x04
 *   c494: movx @dptr, a
 *   c495: ret
 */
void error_log_set_status(void)
{
    REG_PCIE_STATUS = 0x04;
}

/*
 * error_log_process - Process error log entries
 * Address: 0xC2F4-0xC35A (103 bytes)
 *
 * Iterates through error log entries and processes them.
 * Loop continues while IDATA[0x51] < XDATA[0x06E5].
 *
 * Algorithm:
 *   1. Read max entries from 0x06E5 into R7
 *   2. Compare IDATA[0x51] with R7
 *   3. If index >= max, return (done processing)
 *   4. Call error_log_calc_entry_addr() to get entry ptr
 *   5. Read entry type, compare with 0x0AA1
 *   6. If different, process entry (copy to 0x0464, set 0x0574 to 2, etc.)
 *   7. If same, just update 0x0AA1 and loop
 *   8. Increment IDATA[0x51] and continue loop
 *
 * Original disassembly:
 *   c2f4: mov dptr, #0x06e5   ; max entries count
 *   c2f7: movx a, @dptr       ; R7 = max
 *   c2f8: mov r7, a
 *   c2f9: mov a, 0x51         ; A = current index
 *   c2fb: clr c
 *   c2fc: subb a, r7          ; compare: index - max
 *   c2fd: jnc 0xc35a          ; if index >= max, return
 *   c2ff: lcall 0xc47f        ; error_log_calc_entry_addr
 *   c302: movx a, @dptr       ; read entry type
 *   c303: mov r7, a
 *   c304: mov dptr, #0x0aa1   ; current processed index
 *   c307: movx a, @dptr
 *   c308: mov r6, a
 *   c309: xrl a, r7           ; compare
 *   c30a: jz 0xc356           ; if same, skip to increment
 *   ... (complex processing logic)
 *   c356: inc 0x51            ; increment index
 *   c358: sjmp 0xc2f4         ; loop
 *   c35a: ret
 */
void error_log_process(void)
{
    uint8_t max_entries;
    uint8_t current_index;
    uint8_t entry_type;
    uint8_t processed_index;
    uint8_t temp;
    __xdata uint8_t *entry_ptr;

    /* Main processing loop */
    while (1) {
        /* Read max entries count */
        max_entries = G_MAX_LOG_ENTRIES;

        /* Get current index */
        current_index = *(__idata uint8_t *)IDATA_LOG_INDEX;

        /* Check if done */
        if (current_index >= max_entries) {
            return;
        }

        /* Get pointer to current entry's field (+3 bytes in) */
        entry_ptr = error_log_calc_entry_addr();

        /* Read entry type from array */
        entry_type = *entry_ptr;

        /* Read currently processed entry index */
        processed_index = G_LOG_PROCESSED_INDEX;

        /* Compare entry type with processed index */
        if (entry_type != processed_index) {
            /* Entry needs processing */

            /* Calculate address: 0x0500 + (index * 10) + 0x84 */
            temp = 0xA8 + current_index;  /* 0xA8 is offset adjustment */

            /* Call helper to get entry data and store to 0x05A6 */
            error_log_get_array_ptr_2(temp);

            /* Get array pointer and check if entry type == 0x04 */
            entry_ptr = error_log_get_array_ptr(current_index);
            temp = *entry_ptr;

            if (temp == 0x04) {
                /* Entry type 0x04: special processing */
                uint8_t log_entry_value;

                /* Calculate: DPTR = 0x0500 + (index * 10) + 0x84 */
                entry_ptr = (__xdata uint8_t *)(0x0500 +
                    (uint16_t)current_index * ERROR_LOG_ENTRY_SIZE + 0x84);

                /* Read and store value */
                log_entry_value = *entry_ptr;
                *(__idata uint8_t *)IDATA_LOG_TEMP = log_entry_value;

                /* Check if non-zero - if so, skip some processing */
                if (log_entry_value != 0) {
                    /* Store index to system status primary */
                    G_SYS_STATUS_PRIMARY = current_index;

                    /* Set processing state to 2 */
                    G_LOG_PROCESS_STATE = 0x02;

                    /* Read R6 (processed_index) or temp value */
                    temp = processed_index;

                    /* If entry type is 0, use 0; else use IDATA_LOG_TEMP */
                    if (temp == 0) {
                        temp = 0;
                    } else {
                        temp = *(__idata uint8_t *)IDATA_LOG_TEMP;
                    }

                    /* Store to log entry value */
                    G_LOG_ENTRY_VALUE = temp;

                    /* Call complex helper at 0x23F7 with R7=0x09 */
                    /* This helper does extensive state machine processing */
                    /* For now, we skip the call as it requires full implementation */
                }
            }

            /* Update the processed entry index */
            processed_index = G_LOG_PROCESSED_INDEX;
            /* Call error_log_calc_entry_addr again and write back */
            entry_ptr = error_log_calc_entry_addr();
            *entry_ptr = processed_index;
        }

        /* Increment log index */
        (*(__idata uint8_t *)IDATA_LOG_INDEX)++;
    }
}
