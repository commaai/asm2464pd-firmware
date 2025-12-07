/*
 * ASM2464PD Firmware - PCIe Driver
 *
 * PCIe interface controller for USB4/Thunderbolt to NVMe bridge.
 * This driver handles PCIe Transaction Layer Packet (TLP) operations
 * for communicating with the downstream NVMe device.
 *
 * ============================================================================
 * ARCHITECTURE OVERVIEW
 * ============================================================================
 *
 * The ASM2464PD acts as a USB4/Thunderbolt to PCIe bridge. The 8051 firmware
 * controls PCIe transactions through memory-mapped registers at 0xB200-0xB4FF.
 *
 * Data Flow:
 *   USB Host <-> USB Controller <-> DMA Engine <-> PCIe Controller <-> NVMe SSD
 *
 * The PCIe controller handles:
 *   1. Configuration space access (device enumeration, BAR setup)
 *   2. Memory read/write TLPs (NVMe register access, doorbell writes)
 *   3. Completion handling (receiving data from NVMe)
 *
 * ============================================================================
 * TLP (TRANSACTION LAYER PACKET) TYPES
 * ============================================================================
 *
 * The driver supports these PCIe TLP format/type codes:
 *
 *   Config Space Access:
 *     0x04 - Type 0 Configuration Read  (local device)
 *     0x05 - Type 1 Configuration Read  (downstream device)
 *     0x44 - Type 0 Configuration Write (local device)
 *     0x45 - Type 1 Configuration Write (downstream device)
 *
 *   Memory Access:
 *     0x00 - Memory Read Request (32-bit address)
 *     0x40 - Memory Write Request (32-bit address)
 *
 * ============================================================================
 * REGISTER MAP (0xB200-0xB4FF)
 * ============================================================================
 *
 *   0xB210 - REG_PCIE_FMT_TYPE    : TLP format/type byte
 *   0xB213 - REG_PCIE_TLP_CTRL    : TLP control (set to 0x01 to enable)
 *   0xB216 - REG_PCIE_TLP_LENGTH  : TLP length/mode (usually 0x20)
 *   0xB217 - REG_PCIE_BYTE_EN     : Byte enable mask (0x0F for all bytes)
 *   0xB218 - REG_PCIE_ADDR_0      : Address byte 0 (bits 7:0)
 *   0xB219 - REG_PCIE_ADDR_1      : Address byte 1 (bits 15:8)
 *   0xB21A - REG_PCIE_ADDR_2      : Address byte 2 (bits 23:16)
 *   0xB21B - REG_PCIE_ADDR_3      : Address byte 3 (bits 31:24)
 *   0xB220 - REG_PCIE_DATA        : Data register (4 bytes)
 *   0xB22A - REG_PCIE_LINK_STATUS : Link status (speed in bits 7:5)
 *   0xB22B - REG_PCIE_CPL_STATUS  : Completion status code
 *   0xB22C - REG_PCIE_CPL_DATA    : Completion data
 *   0xB254 - REG_PCIE_TRIGGER     : Transaction trigger (write 0x0F)
 *   0xB296 - REG_PCIE_STATUS      : Status register
 *                                   Bit 0: Error flag
 *                                   Bit 1: Complete flag
 *                                   Bit 2: Busy flag
 *
 * ============================================================================
 * TRANSACTION SEQUENCE
 * ============================================================================
 *
 * A typical PCIe transaction follows this sequence:
 *
 *   1. Setup TLP:
 *      - Write format/type to REG_PCIE_FMT_TYPE
 *      - Write 0x01 to REG_PCIE_TLP_CTRL (enable)
 *      - Write byte enables to REG_PCIE_BYTE_EN
 *      - Write address to REG_PCIE_ADDR_0..3
 *      - For writes: write data to REG_PCIE_DATA
 *
 *   2. Trigger:
 *      - Call pcie_clear_and_trigger() which:
 *        - Writes 0x01, 0x02, 0x04 to REG_PCIE_STATUS (clear flags)
 *        - Writes 0x0F to REG_PCIE_TRIGGER (start transaction)
 *
 *   3. Poll for completion:
 *      - Loop calling pcie_get_completion_status() until non-zero
 *      - Call pcie_write_status_complete()
 *
 *   4. Check result:
 *      - Check REG_PCIE_STATUS bit 1 for completion
 *      - Check REG_PCIE_STATUS bit 0 for errors
 *      - For reads: read data from REG_PCIE_CPL_DATA
 *
 * ============================================================================
 * GLOBAL VARIABLES USED
 * ============================================================================
 *
 *   G_PCIE_DIRECTION (0x05AE)  : Bit 0 = 0 for read, 1 for write
 *   G_PCIE_ADDR_0..3 (0x05AF)  : Target PCIe address (4 bytes)
 *   G_PCIE_TXN_COUNT (0x05A6)  : Transaction counter (for debugging)
 *   G_STATE_FLAG_06E6          : Error flag
 *   G_ERROR_CODE_06EA          : Error code (0xFE = PCIe error)
 *
 * ============================================================================
 * IMPLEMENTATION STATUS
 * ============================================================================
 *
 * Core functions implemented:
 *   - Transaction control (trigger, poll, status check)
 *   - TLP setup (config space, memory read/write)
 *   - Completion handling
 *   - Link speed query
 *
 * ============================================================================
 * DISPATCH TABLE (0x0570-0x0650)
 * ============================================================================
 *
 * The PCIe handler dispatch table at 0x0570 maps event indices to handlers:
 *
 *   Index  Address  Target      Description
 *   -----  -------  ------      -----------
 *   0      0x0570   0xE911      Error handler (Bank 1)
 *   1      0x0575   0xEDBD      Bank 1 handler (padding in original)
 *   2      0x057A   0xE0D9      Bank 1 handler
 *   3      0x057F   0xB8DB      Link handler
 *   4      0x0584   0xEF24      Bank 1 handler (padding in original)
 *   5      0x0589   0xD894      Bank 1 handler (ajmp 0x0300)
 *   6      0x058E   0xE0C7      Bank 1 handler (ajmp 0x0300)
 *   7      0x0593   0xC105      PCIe event handler (handler_c105)
 *   8      0x0598   0xE06B      Bank 1 handler
 *   9      0x059D   0xE545      Bank 1 handler
 *   10     0x05A2   0xC523      Handler
 *   11     0x05A7   0xD1CC      Handler
 *   12     0x05AC   0xE74E      Bank 1 handler
 *   13     0x05B1   0xD30B      Handler
 *   14     0x05B6   0xE561      Bank 1 handler
 *   15     0x05BB   0xD5A1      Handler
 *   16     0x05C0   0xC593      Handler
 *   17     0x05C5   0xE7FB      Bank 1 handler
 *   18     0x05CA   0xE890      Bank 1 handler
 *   19     0x05CF   0xC17F      Handler
 *   20     0x05D4   0xB031      Handler
 *   21     0x05D9   0xE175      Bank 1 handler
 *   22     0x05DE   0xE282      Bank 1 handler
 *   23     0x05E3   0xDB80      Handler
 *   24     0x05E8   0x9D90      Handler (ajmp 0x0311)
 *   25     0x05ED   0xD556      Handler (ajmp 0x0311)
 *   ...    ...      ...         (continues)
 *   32     0x0610   0xED02      Bank 1 PCIe handler (handler_ed02 - NOP/padding)
 *   36     0x061A   0xA066      Error handler (error_handler_a066)
 *
 * Note: Entries marked "Bank 1" use DPX=1 for extended code addressing.
 * Handler addresses 0xED02 and 0xEEF9 are padding (NOPs) in the original.
 *
 * ============================================================================
 */

#include "../types.h"
#include "../sfr.h"
#include "../registers.h"
#include "../globals.h"

/* External functions from utils.c */
extern void idata_store_dword(__idata uint8_t *ptr, uint32_t val);

/* Forward declarations */
uint8_t pcie_poll_and_read_completion(void);

/*
 * pcie_clear_and_trigger - Clear status flags and trigger transaction
 * Address: 0x999d-0x99ae (18 bytes)
 *
 * Sequence:
 *   1. Write 1 to status (clear error flag)
 *   2. Write 2 to status (clear complete flag)
 *   3. Write 4 to status (clear busy flag)
 *   4. Write 0x0F to trigger register to start transaction
 *
 * Original disassembly:
 *   999d: mov dptr, #0xb296     ; REG_PCIE_STATUS
 *   99a0: mov a, #0x01
 *   99a2: movx @dptr, a         ; write 1
 *   99a3: inc a                 ; a = 2
 *   99a4: movx @dptr, a         ; write 2
 *   99a5: mov a, #0x04
 *   99a7: movx @dptr, a         ; write 4
 *   99a8: mov dptr, #0xb254     ; REG_PCIE_TRIGGER
 *   99ab: mov a, #0x0f
 *   99ad: movx @dptr, a         ; write 0x0F
 *   99ae: ret
 */
void pcie_clear_and_trigger(void)
{
    REG_PCIE_STATUS = 0x01;  /* Clear error flag */
    REG_PCIE_STATUS = 0x02;  /* Clear complete flag */
    REG_PCIE_STATUS = 0x04;  /* Clear busy flag */
    REG_PCIE_TRIGGER = 0x0F; /* Trigger all lanes */
}

/*
 * pcie_get_completion_status - Check if transaction completed
 * Address: 0x99eb-0x99f5 (11 bytes)
 *
 * Returns bit 2 of status register shifted to position 0.
 * Returns 1 if busy/complete, 0 otherwise.
 *
 * Original disassembly:
 *   99eb: mov dptr, #0xb296     ; REG_PCIE_STATUS
 *   99ee: movx a, @dptr         ; read status
 *   99ef: anl a, #0x04          ; mask bit 2
 *   99f1: rrc a                 ; rotate right
 *   99f2: rrc a                 ; rotate right (now bit 0)
 *   99f3: anl a, #0x3f          ; mask upper bits
 *   99f5: ret
 */
uint8_t pcie_get_completion_status(void)
{
    return (REG_PCIE_STATUS & 0x04) >> 2;
}

/*
 * pcie_get_link_speed - Get PCIe link speed from status
 * Address: 0x9a60-0x9a6b (12 bytes)
 *
 * Extracts bits 7:5 from link status register.
 * Returns link speed encoding (0-7).
 *
 * Original disassembly:
 *   9a60: mov dptr, #0xb22a     ; REG_PCIE_LINK_STATUS
 *   9a63: movx a, @dptr         ; read link status
 *   9a64: anl a, #0xe0          ; mask bits 7:5
 *   9a66: swap a                ; swap nibbles
 *   9a67: rrc a                 ; rotate right
 *   9a68: anl a, #0x07          ; mask to 3 bits
 *   9a6a: mov r7, a             ; return value
 *   9a6b: ret
 */
uint8_t pcie_get_link_speed(void)
{
    return (REG_PCIE_LINK_STATUS >> 5) & 0x07;
}

/*
 * pcie_set_byte_enables - Set TLP byte enables and length mode
 * Address: 0x9a30-0x9a3a (11 bytes)
 *
 * Sets byte enable mask for TLP and configures length to 0x20.
 *
 * Original disassembly:
 *   9a30: mov dptr, #0xb217     ; REG_PCIE_BYTE_EN
 *   9a33: movx @dptr, a         ; write byte enables from A
 *   9a34: mov dptr, #0xb216     ; REG_PCIE_TLP_LENGTH
 *   9a37: mov a, #0x20
 *   9a39: movx @dptr, a         ; write 0x20 (32 dwords)
 *   9a3a: ret
 */
void pcie_set_byte_enables(uint8_t byte_en)
{
    REG_PCIE_BYTE_EN = byte_en;
    REG_PCIE_TLP_LENGTH = 0x20;
}

/*
 * pcie_read_completion_data - Write status and read completion data
 * Address: 0x9a74-0x9a7e (11 bytes)
 *
 * Sets status to 0x02 (complete) then reads completion data register.
 *
 * Original disassembly:
 *   9a74: mov dptr, #0xb296     ; REG_PCIE_STATUS
 *   9a77: mov a, #0x02
 *   9a79: movx @dptr, a         ; write 2 to status
 *   9a7a: mov dptr, #0xb22c     ; REG_PCIE_CPL_DATA
 *   9a7d: movx a, @dptr         ; read completion data
 *   9a7e: ret
 */
uint8_t pcie_read_completion_data(void)
{
    REG_PCIE_STATUS = 0x02;
    return REG_PCIE_CPL_DATA;
}

/*
 * pcie_write_status_complete - Write completion status flag
 * Address: 0x9a95-0x9a9b (7 bytes)
 *
 * Writes 0x04 to status register to indicate completion/busy clear.
 *
 * Original disassembly:
 *   9a95: mov dptr, #0xb296     ; REG_PCIE_STATUS
 *   9a98: mov a, #0x04
 *   9a9a: movx @dptr, a         ; write 4
 *   9a9b: ret
 */
void pcie_write_status_complete(void)
{
    REG_PCIE_STATUS = 0x04;
}

/*
 * pcie_init - Initialize PCIe interface
 * Address: 0x9902-0x990b (10 bytes)
 *
 * Initializes PCIe controller by clearing bit configuration
 * and calling initialization routine.
 *
 * Original disassembly:
 *   9902: mov r0, #0x66
 *   9904: lcall 0x0db9       ; reg_clear_bit type function
 *   9907: lcall 0xde7e       ; initialization
 *   990a: mov a, r7
 *   990b: ret
 */
uint8_t pcie_init(void)
{
    /* Store R4-R7 (cleared) to IDATA[0x66..0x69] */
    /* Original uses lcall 0x0db9 (idata_store_dword) with R0=0x66 */
    idata_store_dword((__idata uint8_t *)0x66, 0);

    /* Call main PCIe initialization at 0xde7e */
    /* This is in Bank 1 - returns status in R7 */
    /* For now, just return success */
    return 0;
}

/*
 * pcie_init_alt - Alternative PCIe initialization
 * Address: 0x990c-0x9915 (10 bytes)
 *
 * Same pattern as pcie_init, possibly for different link mode.
 *
 * Original disassembly:
 *   990c: mov r0, #0x66
 *   990e: lcall 0x0db9
 *   9911: lcall 0xde7e
 *   9914: mov a, r7
 *   9915: ret
 */
uint8_t pcie_init_alt(void)
{
    /* Same as pcie_init - may be called in different contexts */
    return 0;
}

/*
 * pcie_set_idata_params - Set IDATA parameters for transaction
 * Address: 0x99f6-0x99ff (10 bytes)
 *
 * Sets IDATA location 0x65 to 0x0F and 0x63 to 0x00.
 * Used to configure byte enables and address offset.
 *
 * Original disassembly:
 *   99f6: mov r0, #0x65
 *   99f8: mov @r0, #0x0f        ; IDATA[0x65] = 0x0F
 *   99fa: mov r0, #0x63
 *   99fc: mov @r0, #0x00        ; IDATA[0x63] = 0x00
 *   99fe: inc r0                ; r0 = 0x64
 *   99ff: ret
 */
void pcie_set_idata_params(void)
{
    __asm
        mov     r0, #0x65
        mov     @r0, #0x0f
        mov     r0, #0x63
        mov     @r0, #0x00
        inc     r0
    __endasm;
}

/*
 * pcie_clear_address_regs - Clear address offset registers
 * Address: 0x9a9c-0x9aa2 (7 bytes)
 *
 * Clears IDATA locations 0x63 and 0x64 (address offset).
 *
 * Original disassembly:
 *   9a9c: clr a
 *   9a9d: mov r0, #0x63
 *   9a9f: mov @r0, a            ; IDATA[0x63] = 0
 *   9aa0: inc r0
 *   9aa1: mov @r0, a            ; IDATA[0x64] = 0
 *   9aa2: ret
 */
void pcie_clear_address_regs(void)
{
    __asm
        clr     a
        mov     r0, #0x63
        mov     @r0, a
        inc     r0
        mov     @r0, a
    __endasm;
}

/*
 * pcie_inc_txn_counters - Increment PCIe transaction counters
 * Address: 0x9a8a-0x9a94 (11 bytes)
 *
 * Increments both transaction count bytes at 0x05a6 and 0x05a7.
 * Used for tracking PCIe transactions for debugging/statistics.
 *
 * Original disassembly:
 *   9a8a: mov dptr, #0x05a6
 *   9a8d: movx a, @dptr         ; read low byte
 *   9a8e: inc a
 *   9a8f: movx @dptr, a         ; write low byte
 *   9a90: inc dptr              ; dptr = 0x05a7
 *   9a91: movx a, @dptr         ; read high byte
 *   9a92: inc a
 *   9a93: movx @dptr, a         ; write high byte
 *   9a94: ret
 */
void pcie_inc_txn_counters(void)
{
    G_PCIE_TXN_COUNT_LO++;
    G_PCIE_TXN_COUNT_HI++;
}

/*
 * pcie_get_txn_count_hi - Get high byte of transaction count
 * Address: 0x9aa9-0x9ab2 (10 bytes)
 *
 * Reads transaction count high byte and compares with IDATA[0x25].
 * Returns difference (used for transaction tracking).
 *
 * Original disassembly:
 *   9aa9: mov dptr, #0x05a7
 *   9aac: movx a, @dptr         ; read high count
 *   9aad: mov r7, a             ; save to r7
 *   9aae: mov a, 0x25           ; get IDATA[0x25]
 *   9ab0: clr c
 *   9ab1: subb a, r7            ; a = IDATA[0x25] - count_hi
 *   9ab2: ret
 */
uint8_t pcie_get_txn_count_hi(void)
{
    return G_PCIE_TXN_COUNT_HI;
}

/*
 * pcie_write_status_error - Clear error status flag
 * Address: inline pattern
 *
 * Writes 0x01 to status register to clear error flag.
 */
void pcie_write_status_error(void)
{
    REG_PCIE_STATUS = 0x01;
}

/*
 * pcie_write_status_done - Clear completion status flag
 * Address: inline pattern
 *
 * Writes 0x02 to status register to clear completion flag.
 */
void pcie_write_status_done(void)
{
    REG_PCIE_STATUS = 0x02;
}

/*
 * pcie_check_status_complete - Check if transaction complete bit set
 * Address: inline from pattern in pcie_set_address
 *
 * Returns non-zero if status bit 1 (complete) is set.
 */
uint8_t pcie_check_status_complete(void)
{
    return REG_PCIE_STATUS & 0x02;
}

/*
 * pcie_check_status_error - Check if error bit set
 * Address: inline from pattern in pcie_set_address
 *
 * Returns non-zero if status bit 0 (error) is set.
 */
uint8_t pcie_check_status_error(void)
{
    return REG_PCIE_STATUS & 0x01;
}

/*
 * pcie_setup_buffer_params - Setup buffer parameters for TLP
 * Address: 0x9a18-0x9a1f (8 bytes)
 *
 * Writes TLP buffer size parameters (0x34, 0x04) to consecutive
 * locations pointed to by DPTR. Called with DPTR pointing to
 * PCIe buffer descriptor area (0xB234, 0xB240, 0xB244).
 *
 * Original disassembly:
 *   9a18: mov a, #0x34
 *   9a1a: movx @dptr, a         ; [DPTR] = 0x34
 *   9a1b: inc dptr
 *   9a1c: mov a, #0x04
 *   9a1e: movx @dptr, a         ; [DPTR+1] = 0x04
 *   9a1f: ret
 */
void pcie_setup_buffer_params(uint16_t addr)
{
    XDATA8(addr) = 0x34;
    XDATA8(addr + 1) = 0x04;
}

/*
 * pcie_clear_reg_at_offset - Clear PCIe register at given offset
 * Address: 0x9a53-0x9a5f (13 bytes)
 *
 * Computes PCIe register address from offset (0xB2xx) and clears it.
 * offset + 0x10 forms the low byte, 0xB2 is the high byte.
 *
 * Original disassembly:
 *   9a53: add a, #0x10          ; a = offset + 0x10
 *   9a55: mov r7, a
 *   9a56: clr a
 *   9a57: addc a, #0xb2         ; a = 0xB2 (with carry)
 *   9a59: mov DPL, r7           ; DPL = offset + 0x10
 *   9a5b: mov DPH, a            ; DPH = 0xB2
 *   9a5d: clr a
 *   9a5e: movx @dptr, a         ; [0xB2xx] = 0
 *   9a5f: ret
 */
void pcie_clear_reg_at_offset(uint8_t offset)
{
    uint16_t addr = 0xB200 | (uint16_t)(offset + 0x10);
    XDATA8(addr) = 0;
}

/*
 * pcie_wait_for_completion - Wait for PCIe transaction to complete
 * Address: polling loop pattern from pcie_set_address
 *
 * Polls status register until completion or error bit is set.
 * Returns 0 on success, non-zero on error.
 */
uint8_t pcie_wait_for_completion(void)
{
    uint8_t status;

    /* First wait for busy bit to clear */
    do {
        status = pcie_get_completion_status();
    } while (status == 0);

    /* Write completion status */
    pcie_write_status_complete();

    /* Now wait for completion or error */
    while (1) {
        status = REG_PCIE_STATUS;
        if (status & 0x02) {
            /* Transaction complete */
            return 0;
        }
        if (status & 0x01) {
            /* Error occurred */
            REG_PCIE_STATUS = 0x01;  /* Clear error */
            return 0xFE;  /* Error code */
        }
    }
}

/*
 * pcie_setup_memory_tlp - Setup PCIe memory read/write TLP
 * Address: 0xc20c-0xc244 (57 bytes)
 *
 * Sets up a PCIe memory TLP based on direction from global 0x05AE bit 0:
 *   bit 0 = 1: Memory write (fmt_type = 0x40)
 *   bit 0 = 0: Memory read  (fmt_type = 0x00)
 *
 * Reads address from 0x05AF (4 bytes), calls helper functions to set up
 * TLP registers, triggers transaction, and polls for completion.
 * Returns 0 on success (if 0x05AE bit 0 clear), otherwise calls
 * pcie_poll_and_read_completion.
 *
 * Original disassembly at 0xc20c:
 *   c20c: mov dptr, #0xb210     ; FMT_TYPE register
 *   c20f: jnb acc.0, 0xc217     ; if read, jump
 *   c212: mov a, #0x40          ; write fmt_type
 *   c214: movx @dptr, a
 *   c215: sjmp 0xc219
 *   c217: clr a                 ; read fmt_type = 0
 *   c218: movx @dptr, a
 *   c219: mov dptr, #0xb213
 *   c21c: mov a, #0x01
 *   c21e: movx @dptr, a         ; enable bit
 *   c21f: mov a, #0x0f
 *   c221: lcall 0x9a30          ; pcie_set_byte_enables
 *   c224: mov dptr, #0x05af     ; address source
 *   c227: lcall 0x0d84          ; load 32-bit value
 *   c22a: mov dptr, #0xb218     ; address target
 *   c22d: lcall 0x0dc5          ; store 32-bit value
 *   c230: lcall 0x999d          ; pcie_clear_and_trigger
 *   c233: lcall 0x99eb          ; poll loop
 *   c236: jz 0xc233
 *   c238: lcall 0x9a95          ; pcie_write_status_complete
 *   c23b: mov dptr, #0x05ae
 *   c23e: movx a, @dptr
 *   c23f: jnb acc.0, 0xc245     ; if read, go to poll
 *   c242: mov r7, #0x00
 *   c244: ret
 */
uint8_t pcie_setup_memory_tlp(void)
{
    uint8_t direction;
    uint8_t status;

    /* Read direction from global */
    direction = G_PCIE_DIRECTION;

    /* Set format/type based on direction */
    if (direction & 0x01) {
        REG_PCIE_FMT_TYPE = 0x40;  /* Memory write */
    } else {
        REG_PCIE_FMT_TYPE = 0x00;  /* Memory read */
    }

    /* Enable TLP control */
    REG_PCIE_TLP_CTRL = 0x01;

    /* Set byte enables to 0x0F */
    pcie_set_byte_enables(0x0F);

    /* Copy 32-bit address from globals to PCIe address registers */
    REG_PCIE_ADDR_0 = G_PCIE_ADDR_0;
    REG_PCIE_ADDR_1 = G_PCIE_ADDR_1;
    REG_PCIE_ADDR_2 = G_PCIE_ADDR_2;
    REG_PCIE_ADDR_3 = G_PCIE_ADDR_3;

    /* Trigger transaction */
    pcie_clear_and_trigger();

    /* Poll for completion */
    do {
        status = pcie_get_completion_status();
    } while (status == 0);

    /* Write completion status */
    pcie_write_status_complete();

    /* Check direction again */
    direction = G_PCIE_DIRECTION;
    if (direction & 0x01) {
        /* Write - return success immediately */
        return 0;
    }

    /* Read - call poll and read completion */
    return pcie_poll_and_read_completion();
}

/*
 * pcie_poll_and_read_completion - Poll for completion and read result
 * Address: 0xc245-0xc26f (43 bytes)
 *
 * Polls PCIe status until complete or error. On completion, reads
 * completion data and verifies status. Returns:
 *   Link speed (0-7) on success (if completion status == 0x04)
 *   0xFE on error (bit 0 set)
 *   0xFF on completion error (non-zero completion data or wrong status)
 *
 * Original disassembly:
 *   c245: mov dptr, #0xb296     ; poll loop start
 *   c248: movx a, @dptr         ; read status
 *   c249: anl a, #0x02          ; check complete bit
 *   c24b: clr c
 *   c24c: rrc a                 ; shift to bit 0
 *   c24d: jnz 0xc259            ; if complete, read data
 *   c24f: movx a, @dptr
 *   c250: jnb acc.0, 0xc245     ; if no error, keep polling
 *   c253: mov a, #0x01          ; clear error
 *   c255: movx @dptr, a
 *   c256: mov r7, #0xfe         ; return 0xFE (error)
 *   c258: ret
 *   c259: lcall 0x9a74          ; pcie_read_completion_data
 *   c25c: jnz 0xc26d            ; if non-zero, error
 *   c25e: inc dptr              ; dptr = 0xB22D
 *   c25f: movx a, @dptr
 *   c260: jnz 0xc26d            ; if non-zero, error
 *   c262: mov dptr, #0xb22b
 *   c265: movx a, @dptr
 *   c266: cjne a, #0x04, 0xc26d ; check status == 4
 *   c269: lcall 0x9a60          ; pcie_get_link_speed
 *   c26c: ret
 *   c26d: mov r7, #0xff         ; return 0xFF (completion error)
 *   c26f: ret
 */
uint8_t pcie_poll_and_read_completion(void)
{
    uint8_t status;
    uint8_t cpl_data;

    /* Poll for complete or error */
    while (1) {
        status = REG_PCIE_STATUS;

        /* Check completion bit (bit 1) */
        if (status & 0x02) {
            /* Complete - read completion data */
            cpl_data = pcie_read_completion_data();
            if (cpl_data != 0) {
                return 0xFF;  /* Completion error */
            }

            /* Check 0xB22D (next byte after CPL_DATA) */
            if (REG_PCIE_CPL_DATA_ALT != 0) {
                return 0xFF;  /* Completion error */
            }

            /* Check completion status code */
            if (REG_PCIE_CPL_STATUS != 0x04) {
                return 0xFF;  /* Completion error */
            }

            /* Success - return link speed */
            return pcie_get_link_speed();
        }

        /* Check error bit (bit 0) */
        if (status & 0x01) {
            REG_PCIE_STATUS = 0x01;  /* Clear error */
            return 0xFE;  /* Error code */
        }
    }
}

/*
 * pcie_write_tlp_addr_low - Write A to 0xB21B and set TLP length
 * Address: 0x9a33-0x9a3a (8 bytes)
 *
 * Called with address low byte in A, writes to 0xB21B then sets
 * TLP length register to 0x20.
 *
 * Original disassembly:
 *   9a33: movx @dptr, a         ; [B21B] = A
 *   9a34: mov dptr, #0xb216
 *   9a37: mov a, #0x20
 *   9a39: movx @dptr, a         ; [B216] = 0x20
 *   9a3a: ret
 */
void pcie_write_tlp_addr_low(uint8_t val)
{
    REG_PCIE_ADDR_3 = val;
    REG_PCIE_TLP_LENGTH = 0x20;
}

/*
 * pcie_setup_config_tlp - Setup PCIe configuration space TLP
 * Address: 0xadc3-0xae54 (approximately 145 bytes)
 *
 * Sets up a PCIe configuration space read or write TLP based on
 * parameters in IDATA:
 *   IDATA[0x60] bit 0: 0=read, 1=write
 *   IDATA[0x61]: Type (0=type0, non-0=type1)
 *   IDATA[0x62-0x64]: Address bytes
 *   IDATA[0x65]: Byte enables
 *
 * PCIe Config TLP format types:
 *   0x04: Type 0 Config Read
 *   0x05: Type 1 Config Read
 *   0x44: Type 0 Config Write
 *   0x45: Type 1 Config Write
 *
 * PCIe Config Address format (in 0xB218-0xB21B):
 *   The address bytes encode Bus/Device/Function/Register
 *   Bit manipulation is required to place these in the correct positions
 *
 * Original disassembly starts at 0xadc3:
 *   adc3: mov r0, #0x60
 *   adc5: mov a, @r0           ; check direction
 *   adc6: jnb acc.0, 0xadd3    ; if read, jump
 *   adc9: inc r0
 *   adca: mov a, @r0           ; check type
 *   adcb: mov r7, #0x44        ; type 0 write
 *   adcd: jz 0xadd1
 *   adcf: mov r7, #0x45        ; type 1 write
 *   add1: sjmp 0xaddc
 *   (read path)
 *   add3: mov r0, #0x61
 *   add5: mov a, @r0           ; check type
 *   add6: mov r7, #0x04        ; type 0 read
 *   add8: jz 0xaddc
 *   adda: mov r7, #0x05        ; type 1 read
 *   addc: mov dptr, #0xb210
 *   addf: mov a, r7
 *   ade0: movx @dptr, a        ; write fmt_type
 *   ... (continues with address setup and polling)
 */
void pcie_setup_config_tlp(void)
{
    uint8_t fmt_type;
    uint8_t direction;
    uint8_t type;
    uint8_t byte_en;
    uint8_t addr1, addr2, addr3;
    uint8_t shifted_hi, shifted_lo;
    uint8_t tmp;
    uint8_t status;
    uint8_t i;

    /* Read parameters from IDATA */
    direction = ((__idata uint8_t *)0x60)[0];
    type = ((__idata uint8_t *)0x61)[0];

    /* Determine format/type based on direction and type */
    if (direction & 0x01) {
        /* Config write */
        fmt_type = (type != 0) ? 0x45 : 0x44;  /* Type 1 or Type 0 */
    } else {
        /* Config read */
        fmt_type = (type != 0) ? 0x05 : 0x04;  /* Type 1 or Type 0 */
    }

    /* Setup PCIe TLP registers */
    REG_PCIE_FMT_TYPE = fmt_type;
    REG_PCIE_TLP_CTRL = 0x01;  /* Enable TLP control */

    /* Read byte enables and mask to lower 4 bits */
    byte_en = ((__idata uint8_t *)0x65)[0];
    REG_PCIE_BYTE_EN = byte_en & 0x0F;

    /* Read address bytes from IDATA[0x61-0x64] */
    addr1 = ((__idata uint8_t *)0x61)[0];
    REG_PCIE_ADDR_0 = addr1;

    addr1 = ((__idata uint8_t *)0x62)[0];
    REG_PCIE_ADDR_1 = addr1;

    /* Complex bit manipulation for config address format */
    /* addr2 = IDATA[0x63], addr3 = IDATA[0x64] */
    addr2 = ((__idata uint8_t *)0x63)[0];
    addr3 = ((__idata uint8_t *)0x64)[0];

    /* Extract bits: shifted_hi = addr2 & 0x03, tmp = addr3 & 0xC0 */
    shifted_hi = addr2 & 0x03;
    tmp = addr3 & 0xC0;

    /* Rotate 16-bit value (shifted_hi:tmp) right by 6 bits */
    /* This is: (shifted_hi << 8 | tmp) >> 6 = (shifted_hi << 2) | (tmp >> 6) */
    for (i = 0; i < 6; i++) {
        /* 16-bit right rotate through carry */
        uint8_t carry = shifted_hi & 0x01;
        shifted_hi >>= 1;
        tmp = (tmp >> 1) | (carry ? 0x80 : 0);
    }
    shifted_lo = tmp;

    /* Update address byte 2: preserve upper nibble, set lower nibble from shifted result */
    tmp = REG_PCIE_ADDR_2;
    tmp = (tmp & 0xF0) | (shifted_lo & 0x0F);
    REG_PCIE_ADDR_2 = tmp;

    /* Compute value for address byte 3 */
    /* Take lower 6 bits of addr3, multiply by 4 (shift left 2) */
    tmp = (addr3 & 0x3F) << 2;

    /* Read address byte 3, preserve low 2 bits, OR with shifted value */
    shifted_lo = REG_PCIE_ADDR_3;
    shifted_lo = (shifted_lo & 0x03) | tmp;

    /* Write to 0xB21B and set TLP length */
    pcie_write_tlp_addr_low(shifted_lo);

    /* Trigger the transaction */
    pcie_clear_and_trigger();

    /* Poll for completion */
    do {
        status = pcie_get_completion_status();
    } while (status == 0);

    /* Write completion status */
    pcie_write_status_complete();

    /* Check for errors - poll until complete or error */
    while (1) {
        status = REG_PCIE_STATUS;
        if (status & 0x02) {
            /* Transaction complete - success */
            return;
        }
        if (status & 0x01) {
            /* Error occurred */
            REG_PCIE_STATUS = 0x01;  /* Clear error flag */
            /* Set error indicators in globals */
            G_ERROR_CODE_06EA = 0xFE;   /* Error code */
            G_STATE_FLAG_06E6 = 0x01;   /* Error flag */
            /* Error handler at 0xc00d clears state and recovery:
             * - Clears 0x06E6..0x06E8 (state flags)
             * - Clears 0x05A7, 0x06EB, 0x05AC, 0x05AD (transaction params)
             * - Calls 0x545c (USB reset helper)
             * - Calls 0x99e4 with DPTR=0xB401 (PCIe config write)
             * - Additional state machine reset logic
             */
            return;
        }
    }
}

/*
 * pcie_event_handler - PCIe event handler (handler_c105)
 * Address: 0xC105-0xC17E
 *
 * This is a PCIe event/interrupt handler that processes PCIe-related events.
 * It checks various status bits and dispatches to appropriate sub-handlers.
 *
 * The function:
 * 1. Calls bank-switching helper 0xBCDE (reads reg with DPX=1)
 * 2. If bit 0 set: calls 0xA522 (PCIe interrupt handler)
 * 3. Calls bank-switching helper 0xBCAF
 * 4. If bit 0 set and G_EVENT_CTRL_09FA bit 1 set:
 *    - Checks 0x92C2 (power status) bit 6
 *    - If set: writes 0x01 to 0x0AE2 and calls 0xCA0D
 *    - Calls 0xE74E and writes 0x69 to 0x07FF
 * 5. If bit 0 clear: handles alternate path based on event state
 *
 * Original disassembly at 0xC105:
 *   c105: lcall 0xbcde        ; Bank-switch read
 *   c108: jnb acc.0, 0xc10e   ; Skip if bit 0 clear
 *   c10b: lcall 0xa522        ; PCIe interrupt sub-handler
 *   c10e: lcall 0xbcde        ; Bank-switch read again
 *   c111: jnb acc.3, 0xc117   ; Skip if bit 3 clear
 *   c114: lcall 0x0543        ; Another handler
 *   c117: lcall 0xbcaf        ; Different bank read
 *   c11a: jnb acc.0, 0xc143   ; Check bit 0
 *   ... (continues with state machine logic)
 */
void pcie_event_handler(void)
{
    uint8_t status;
    uint8_t event_ctrl;

    /*
     * Note: The original function uses complex bank-switching via DPX
     * register to access code bank 1 registers. In our implementation,
     * we'll use simpler register access patterns.
     *
     * The actual hardware behavior depends on:
     * - REG_BANK_CONFIG at 0xB214 for bank switching
     * - Event control at 0x09FA for state machine
     * - Power status at 0x92C2 for power events
     * - System state at 0x0AE2 for handler dispatch
     */

    /* First bank read and check (0xBCDE pattern) */
    /* Original reads from bank 1 address loaded via DPTR */
    status = REG_PCIE_STATUS;  /* Simplified - actual reads bank 1 reg */

    if (status & 0x01) {
        /* Bit 0 set - call PCIe interrupt sub-handler (0xA522) */
        /* This handles PCIe link events */
    }

    /* Second check - bit 3 */
    if (status & 0x08) {
        /* Call handler at 0x0543 - likely another PCIe event type */
    }

    /* Different bank read (0xBCAF pattern) */
    /* Check event control flags */
    event_ctrl = G_EVENT_CTRL_09FA;

    if (status & 0x01) {
        /* Path 1: Event bit set */
        /* Write 0x01 to event handler params */
        /* This is func at 0x0BE6 with A=0x01 */

        if (event_ctrl & 0x02) {
            /* Event control bit 1 set */
            /* Check power status bit 6 */
            if (REG_POWER_STATUS & POWER_STATUS_SUSPENDED) {
                /* Power event - set system state and call handler */
                G_SYSTEM_STATE_0AE2 = 0x01;
                /* Call 0xCA0D - system state handler */
            }

            /* Call 0xE74E - bank 1 event handler */
            /* Write marker 0x69 to 0x07FF */
            XDATA8(0x07FF) = 0x69;
            return;
        }
    }

    /* Path 2: Alternate event handling */
    /* Write 0x02 to event handler params */

    if (event_ctrl & 0x02) {
        /* Read 0xBC88 pattern - another bank helper */
        /* Mask with 0xC0, OR with 0x04 */
        /* Write back via 0xBC9F */

        /* Read again, mask with 0x3F, OR with 0x40 */
        /* Call 0x0BE6 with result */

        /* Write 0x09 via 0xBC63 */
        /* Call 0xE890 - bank 1 handler */

        /* Call 0xBC98 with R1=0x43 */
        /* Check bit 6 of result */
        if (REG_POWER_STATUS & POWER_STATUS_SUSPENDED) {
            /* Call 0xD916 with R7=0 */
            /* Call 0xBF8E */
        }
    }
}

/*
 * pcie_error_handler - PCIe error handler and recovery
 * Address: 0xC00D-0xC087
 *
 * Called when a PCIe error is detected. This function:
 * 1. Checks if error flag (0x06E6) is set - returns if not
 * 2. Clears error flags at 0x06E6-0x06E8
 * 3. Clears transaction parameters at 0x05A7, 0x06EB, 0x05AC-0x05AD
 * 4. Calls USB reset helper at 0x545C
 * 5. Configures PCIe reset via 0xB401 register
 * 6. Clears state machine variables
 *
 * Original disassembly:
 *   c00d: mov dptr, #0x06e6     ; Error flag
 *   c010: movx a, @dptr         ; Read flag
 *   c011: jz 0xc088             ; Return if no error
 *   c013: clr a                 ; Clear A
 *   c014: movx @dptr, a         ; Clear 0x06E6
 *   c015: inc dptr              ; 0x06E7
 *   c016: inc a                 ; A = 1
 *   c017: movx @dptr, a         ; Write 1 to 0x06E7
 *   c018: inc dptr              ; 0x06E8
 *   c019: movx @dptr, a         ; Write 1 to 0x06E8
 *   c01a: clr a
 *   c01b: mov dptr, #0x05a7     ; Clear PCIe TXN count hi
 *   c01e: movx @dptr, a
 *   c01f: mov dptr, #0x06eb
 *   c022: movx @dptr, a
 *   c023: mov dptr, #0x05ac
 *   c026: movx @dptr, a         ; Clear 0x05AC
 *   c027: inc dptr
 *   c028: movx @dptr, a         ; Clear 0x05AD
 *   c029: lcall 0x545c          ; USB reset helper
 *   c02c: mov dptr, #0xb401     ; PCIe config
 *   c02f: lcall 0x99e4          ; Write config
 *   c032: movx a, @dptr
 *   c033: anl a, #0xfe          ; Clear bit 0
 *   c035: movx @dptr, a
 *   ... (continues with more cleanup)
 */
void pcie_error_handler(void)
{
    uint8_t tmp;

    /* Check if error flag is set */
    if (G_STATE_FLAG_06E6 == 0) {
        return;
    }

    /* Clear error flags */
    G_STATE_FLAG_06E6 = 0x00;
    XDATA8(0x06E7) = 0x01;  /* Set recovery state */
    XDATA8(0x06E8) = 0x01;

    /* Clear PCIe transaction parameters */
    G_PCIE_TXN_COUNT_HI = 0x00;
    XDATA8(0x06EB) = 0x00;
    XDATA8(0x05AC) = 0x00;
    XDATA8(0x05AD) = 0x00;

    /* Call USB reset helper (0x545C) */
    /* This clears USB endpoint state during PCIe error recovery */

    /* Configure PCIe reset via 0xB401 */
    /* Call 0x99E4 to write PCIe config, then clear bit 0 */
    tmp = XDATA8(0xB401);
    tmp &= 0xFE;  /* Clear bit 0 - PCIe reset bit */
    XDATA8(0xB401) = tmp;

    /* Call 0xCD6C - additional error cleanup */

    /* Configure 0xCA06 - clear bit 4 */
    tmp = XDATA8(0xCA06);
    tmp &= 0xEF;  /* Clear bit 4 */
    /* Call 0x99E0 to write back */

    /* Call bank 1 handlers for recovery */
    /* 0xE8A9 with R7=0x01 */
    /* 0xD436 with R7=0x0F */

    /* Clear IDATA[0x62] */
    __asm
        clr     a
        mov     r0, #0x62
        mov     @r0, a
    __endasm;

    /* Clear max log entries */
    G_MAX_LOG_ENTRIES = 0x00;
}
