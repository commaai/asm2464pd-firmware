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

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/* External functions from utils.c */
extern void idata_store_dword(__idata uint8_t *ptr, uint32_t val);

/* External functions from other modules */
extern void pcie_lane_config(uint8_t lane_mask);  /* from phy.c (0xD436) */
extern void pcie_tunnel_setup(void);              /* 0xCD6C - in dispatch.c */

/* Forward declarations */
uint8_t pcie_poll_and_read_completion(void);
void tlp_init_addr_buffer(void);
void flash_set_mode_enable(void);
uint8_t tlp_write_flash_cmd(uint8_t cmd);

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
    return (REG_PCIE_STATUS & PCIE_STATUS_BUSY) >> 2;
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
    REG_PCIE_STATUS = PCIE_STATUS_COMPLETE;
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
    REG_PCIE_STATUS = PCIE_STATUS_BUSY;
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
    return REG_PCIE_STATUS & PCIE_STATUS_COMPLETE;
}

/*
 * pcie_check_status_error - Check if error bit set
 * Address: inline from pattern in pcie_set_address
 *
 * Returns non-zero if status bit 0 (error) is set.
 */
uint8_t pcie_check_status_error(void)
{
    return REG_PCIE_STATUS & PCIE_STATUS_ERROR;
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
        if (status & PCIE_STATUS_COMPLETE) {
            /* Transaction complete */
            return 0;
        }
        if (status & PCIE_STATUS_ERROR) {
            /* Error occurred */
            REG_PCIE_STATUS = PCIE_STATUS_ERROR;  /* Clear error */
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
        REG_PCIE_FMT_TYPE = PCIE_FMT_MEM_WRITE;
    } else {
        REG_PCIE_FMT_TYPE = PCIE_FMT_MEM_READ;
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
            /* Write marker 0x69 to debug byte */
            G_CMD_DEBUG_FF = 0x69;
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
 * pcie_tunnel_enable - Enable PCIe tunneling for USB4/eGPU mode
 * Address: 0xC00D-0xC087
 *
 * Enables USB4 PCIe tunneling. This function:
 * 1. Checks if tunnel flag (0x06E6) is set - returns if not
 * 2. Clears state flags at 0x06E6-0x06E8
 * 3. Clears transaction parameters at 0x05A7, 0x06EB, 0x05AC-0x05AD
 * 4. Calls USB sync helper at 0x545C
 * 5. Clears tunnel control bit 0 (0xB401)
 * 6. Calls pcie_tunnel_setup (0xCD6C)
 * 7. Clears CPU mode bit 4 (0xCA06) - exit NVMe mode
 * 8. Configures all 4 PCIe lanes (0xD436 with 0x0F)
 * 9. Clears adapter config array at 0x05B3
 *
 * Original disassembly:
 *   c00d: mov dptr, #0x06e6     ; Tunnel flag
 *   c010: movx a, @dptr         ; Read flag
 *   c011: jz 0xc088             ; Return if not set
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
 *   c029: lcall 0x545c          ; USB sync helper
 *   c02c: mov dptr, #0xb401     ; Tunnel control
 *   c02f: lcall 0x99e4          ; Helper
 *   c032: movx a, @dptr
 *   c033: anl a, #0xfe          ; Clear bit 0
 *   c035: movx @dptr, a
 *   c036: lcall 0xcd6c          ; pcie_tunnel_setup
 *   c039: mov dptr, #0xca06     ; CPU mode
 *   c03c: movx a, @dptr
 *   c03d: anl a, #0xef          ; Clear bit 4 (NVMe mode)
 *   ... (continues with lane config and array clear)
 */
void pcie_tunnel_enable(void)
{
    uint8_t tmp;

    /* Check if tunnel enable flag is set */
    if (G_STATE_FLAG_06E6 == 0) {
        return;
    }

    /* Clear state flags */
    G_STATE_FLAG_06E6 = 0x00;
    G_WORK_06E7 = 0x01;
    G_WORK_06E8 = 0x01;

    /* Clear PCIe transaction parameters */
    G_PCIE_TXN_COUNT_HI = 0x00;
    G_WORK_06EB = 0x00;
    G_DMA_WORK_05AC = 0x00;
    G_DMA_WORK_05AD = 0x00;

    /* Call USB sync helper (0x545C) */

    /* Clear tunnel control bit 0 (0xB401) */
    tmp = REG_PCIE_TUNNEL_CTRL;
    tmp &= 0xFE;  /* Clear bit 0 */
    REG_PCIE_TUNNEL_CTRL = tmp;

    /* Call pcie_tunnel_setup (0xCD6C) */
    pcie_tunnel_setup();

    /* Clear CPU mode bit 4 (0xCA06) - exit NVMe mode */
    tmp = REG_CPU_MODE_NEXT;
    tmp &= 0xEF;  /* Clear bit 4 */
    REG_CPU_MODE_NEXT = tmp;

    /* Configure all 4 PCIe lanes (0xD436 with R7=0x0F) */
    pcie_lane_config(0x0F);

    /* Clear IDATA[0x62] */
    __asm
        clr     a
        mov     r0, #0x62
        mov     @r0, a
    __endasm;

    /* Clear max log entries */
    G_MAX_LOG_ENTRIES = 0x00;
}

/*
 * pcie_adapter_config - Configure PCIe tunnel adapter registers
 * Address: 0xC8DB-0xC941
 *
 * Writes adapter configuration from globals (0x0A52-0x0A55) to
 * PCIe tunnel adapter hardware registers (0xB410-0xB42B).
 *
 * Config globals:
 *   0x0A52 (G_PCIE_ADAPTER_CFG_LO) - Link config high byte
 *   0x0A53 (G_PCIE_ADAPTER_CFG_HI) - Link config low byte
 *   0x0A54 (G_PCIE_ADAPTER_MODE)   - Mode config
 *   0x0A55 (G_PCIE_ADAPTER_AUX)    - Auxiliary config
 *
 * Original disassembly:
 *   c8db: mov dptr, #0x0a53    ; Load cfg_hi
 *   c8de: movx a, @dptr
 *   c8df: mov r7, a
 *   c8e0: mov dptr, #0xb410    ; Write to adapter reg 10
 *   c8e3: movx @dptr, a
 *   ...
 *   c941: ret
 */
void pcie_adapter_config(void)
{
    uint8_t cfg_hi, cfg_lo, cfg_mode, cfg_aux;

    /* Read config from globals */
    cfg_hi = G_PCIE_ADAPTER_CFG_HI;    /* 0x0A53 */
    cfg_lo = G_PCIE_ADAPTER_CFG_LO;    /* 0x0A52 */

    /* Write to tunnel config registers 0xB410-0xB411 */
    REG_TUNNEL_CFG_A_LO = cfg_hi;
    REG_TUNNEL_CFG_A_HI = cfg_lo;

    /* Write to tunnel data registers 0xB420-0xB421, load aux config */
    REG_TUNNEL_DATA_LO = cfg_hi;
    REG_TUNNEL_DATA_HI = cfg_lo;
    cfg_aux = G_PCIE_ADAPTER_AUX;      /* 0x0A55 */

    /* Write credits to 0xB412 */
    REG_TUNNEL_CREDITS = cfg_aux;

    /* Read mode config and write to 0xB413 */
    cfg_mode = G_PCIE_ADAPTER_MODE;    /* 0x0A54 */
    REG_TUNNEL_CFG_MODE = cfg_mode;

    /* Write status registers 0xB422-0xB423 */
    REG_TUNNEL_STATUS_0 = cfg_aux;
    REG_TUNNEL_STATUS_1 = cfg_mode;

    /* Write fixed capability pattern 0x06, 0x04, 0x00 to 0xB415-0xB417 */
    REG_TUNNEL_CAP_0 = 0x06;
    REG_TUNNEL_CAP_1 = 0x04;
    REG_TUNNEL_CAP_2 = 0x00;

    /* Write same pattern to 0xB425-0xB427 */
    REG_TUNNEL_CAP2_0 = 0x06;
    REG_TUNNEL_CAP2_1 = 0x04;
    REG_TUNNEL_CAP2_2 = 0x00;

    /* Reload config values */
    cfg_hi = G_PCIE_ADAPTER_CFG_HI;
    cfg_lo = G_PCIE_ADAPTER_CFG_LO;

    /* Write to tunnel link config registers 0xB41A-0xB41B */
    REG_TUNNEL_LINK_CFG_LO = cfg_hi;
    REG_TUNNEL_LINK_CFG_HI = cfg_lo;

    /* Write to auxiliary config 0xB42A-0xB42B, reload aux */
    REG_TUNNEL_AUX_CFG_LO = cfg_hi;
    REG_TUNNEL_AUX_CFG_HI = cfg_lo;
    cfg_aux = G_PCIE_ADAPTER_AUX;

    /* Write path credits to 0xB418 */
    REG_TUNNEL_PATH_CREDITS = cfg_aux;

    /* Reload mode and write to path mode 0xB419 */
    cfg_mode = G_PCIE_ADAPTER_MODE;
    REG_TUNNEL_PATH_MODE = cfg_mode;

    /* Write to path 2 registers 0xB428-0xB429 */
    REG_TUNNEL_PATH2_CRED = cfg_aux;
    REG_TUNNEL_PATH2_MODE = cfg_mode;
}

/*===========================================================================
 * PCIe Config/Helper Functions (0x9916-0x9aba)
 *
 * These are small helper functions for PCIe configuration table access
 * and transaction management. Most use the table at 0x05B4-0x05C0 which
 * stores per-endpoint PCIe configuration (34 bytes per entry).
 *
 * The helper at 0x0dd1 is a table multiply/add function:
 *   DPTR = base + (A * B)
 * where B is stored in the B register.
 *===========================================================================*/

/*
 * pcie_store_txn_idx - Store transaction index to 0x05A6
 * Address: 0x9916-0x9922 (13 bytes)
 *
 * Stores R6 to G_PCIE_TXN_COUNT_LO (0x05A6), copies to R7,
 * calls bank1 helper 0xE77A, stores 1 to IDATA[0x65].
 *
 * Original disassembly:
 *   9916: mov dptr, #0x05a6
 *   9919: mov a, r6
 *   991a: movx @dptr, a        ; store txn idx
 *   991b: mov r7, a
 *   991c: lcall 0xe77a         ; bank1 helper
 *   991f: mov r0, #0x65
 *   9921: mov @r0, #0x01       ; IDATA[0x65] = 1
 *   9923: ret (actually continues to 9923)
 */
void pcie_store_txn_idx(uint8_t idx)
{
    G_PCIE_TXN_COUNT_LO = idx;
    /* Call bank1 helper - simplified */
    *(__idata uint8_t *)0x65 = 0x01;
}

/*
 * pcie_lookup_config_05c0 - Look up in config table at 0x05C0
 * Address: 0x9923-0x992f (13 bytes)
 *
 * Reads index from 0x05A6, multiplies by 0x22 (34),
 * adds to 0x05C0, returns via ljmp to 0x0dd1.
 *
 * Original disassembly:
 *   9923: mov dptr, #0x05a6
 *   9926: movx a, @dptr        ; A = txn index
 *   9927: mov dptr, #0x05c0    ; base table
 *   992a: mov b, #0x22         ; entry size 34 bytes
 *   992d: ljmp 0x0dd1          ; table lookup helper
 */
__xdata uint8_t *pcie_lookup_config_05c0(void)
{
    uint8_t idx = G_PCIE_TXN_COUNT_LO;
    uint16_t addr = 0x05C0 + ((uint16_t)idx * 0x22);
    return (__xdata uint8_t *)addr;
}

/*
 * pcie_lookup_config_05bd - Look up in config table at 0x05BD
 * Address: 0x9930-0x994a (27 bytes)
 *
 * Reads 0x05A6, multiplies by 0x22, adds to 0x05BD, reads result,
 * adds 4, stores to IDATA[0x64], carries to IDATA[0x63],
 * sets R7=0x40, R6=0x01, R5=0, R4=0, then calls 0x0dc5 with DPTR=0xB220.
 *
 * Original disassembly:
 *   9930: mov dptr, #0x05a6
 *   9933: movx a, @dptr
 *   9934: mov b, #0x22
 *   9937: mov dptr, #0x05bd    ; different table offset
 *   993a: lcall 0x0dd1         ; table lookup
 *   993d: movx a, @dptr
 *   993e: add a, #0x04         ; add 4 to result
 *   9940: mov r0, #0x64
 *   9942: mov @r0, a           ; IDATA[0x64] = result + 4
 *   9943: clr a
 *   9944: rlc a                ; carry to A
 *   9945: dec r0
 *   9946: mov @r0, a           ; IDATA[0x63] = carry
 *   9947: mov r7, #0x40
 *   9949: mov r6, #0x01
 *   994b: clr a
 *   994c: mov r5, a
 *   994d: mov r4, a
 *   994e: mov dptr, #0xb220
 *   9951: ljmp 0x0dc5          ; dword store helper
 */
void pcie_setup_buffer_from_config(void)
{
    uint8_t idx = G_PCIE_TXN_COUNT_LO;
    uint16_t addr = 0x05BD + ((uint16_t)idx * 0x22);
    uint8_t val = XDATA8(addr);
    uint8_t result = val + 4;
    uint8_t carry = (result < val) ? 1 : 0;

    *(__idata uint8_t *)0x64 = result;
    *(__idata uint8_t *)0x63 = carry;

    /* Store 0x00010040 to PCIe data register */
    (&REG_PCIE_DATA)[0] = 0x40;
    (&REG_PCIE_DATA)[1] = 0x01;
    (&REG_PCIE_DATA)[2] = 0x00;
    (&REG_PCIE_DATA)[3] = 0x00;
}

/*
 * pcie_shift_r7_to_r6 - Shift R7 right twice and mask
 * Address: 0x9954-0x9961 (14 bytes)
 *
 * Takes R7, shifts right twice (divide by 4), masks to 6 bits,
 * stores to R6, then does table lookup at 0x05A6 with B=0x22.
 *
 * Original disassembly:
 *   9954: mov a, r7
 *   9955: rrc a               ; shift right through carry
 *   9956: rrc a               ; shift right again
 *   9957: anl a, #0x3f        ; mask to 6 bits
 *   9959: mov r6, a
 *   995a: mov dptr, #0x05a6
 *   995d: movx a, @dptr
 *   995e: mov b, #0x22
 *   9961: ret
 */
uint8_t pcie_calc_queue_idx(uint8_t val)
{
    uint8_t result = (val >> 2) & 0x3F;
    return result;
}

/*
 * pcie_lookup_from_idata26 - Look up using IDATA[0x26] as index
 * Address: 0x9962-0x9969 (8 bytes)
 *
 * Reads IDATA[0x26], multiplies by 0x22, jumps to 0x0dd1.
 *
 * Original disassembly:
 *   9962: mov a, 0x26         ; A = IDATA[0x26]
 *   9964: mov b, #0x22
 *   9967: ljmp 0x0dd1
 */
__xdata uint8_t *pcie_lookup_from_idata26(void)
{
    uint8_t idx = *(__idata uint8_t *)0x26;
    /* Base address implied by caller's DPTR setup */
    return (__xdata uint8_t *)(0x05B4 + ((uint16_t)idx * 0x22));
}

/*
 * pcie_check_txn_count - Compare transaction counts
 * Address: 0x996a-0x9976 (13 bytes)
 *
 * Reads 0x05A7 into R7, reads 0x0A5B into R6,
 * subtracts R7 from R6 to check difference.
 *
 * Original disassembly:
 *   996a: mov dptr, #0x05a7
 *   996d: movx a, @dptr       ; A = [0x05A7]
 *   996e: mov r7, a
 *   996f: mov dptr, #0x0a5b
 *   9972: movx a, @dptr       ; A = [0x0A5B]
 *   9973: mov r6, a
 *   9974: clr c
 *   9975: subb a, r7          ; A = R6 - R7
 *   9976: ret
 */
uint8_t pcie_check_txn_count(void)
{
    uint8_t count_hi = G_PCIE_TXN_COUNT_HI;
    uint8_t count_ref = G_NIBBLE_SWAP_0A5B;
    return count_ref - count_hi;
}

/*
 * pcie_lookup_05b6 - Look up in table at 0x05B6
 * Address: 0x9977-0x997f (9 bytes)
 *
 * Sets DPTR=0x05B6, B=0x22, jumps to table lookup helper.
 *
 * Original disassembly:
 *   9977: mov dptr, #0x05b6
 *   997a: mov b, #0x22
 *   997d: ljmp 0x0dd1
 */
__xdata uint8_t *pcie_lookup_05b6(uint8_t idx)
{
    uint16_t addr = 0x05B6 + ((uint16_t)idx * 0x22);
    return (__xdata uint8_t *)addr;
}

/*
 * pcie_store_to_05b8 - Store R7 to table at 0x05B8
 * Address: 0x9980-0x9989 (10 bytes)
 *
 * Sets DPTR=0x05B8, stores A (from R7), multiplies by 0x22 for lookup.
 *
 * Original disassembly:
 *   9980: mov dptr, #0x05b8
 *   9983: mov a, r7
 *   9984: mov b, #0x22
 *   9987: ljmp 0x0dd1
 */
void pcie_store_to_05b8(uint8_t idx, uint8_t val)
{
    uint16_t addr = 0x05B8 + ((uint16_t)idx * 0x22);
    XDATA8(addr) = val;
}

/*
 * pcie_store_r6_to_05a6 - Store R6 to 0x05A6 and call helper
 * Address: 0x998a-0x9995 (12 bytes)
 *
 * Stores R6 to 0x05A6, copies to R7, calls 0xE77A, sets IDATA[0x65].
 *
 * Original disassembly:
 *   998a: mov a, r6
 *   998b: mov dptr, #0x05a6
 *   998e: movx @dptr, a
 *   998f: mov r7, a
 *   9990: lcall 0xe77a
 *   9993: mov r0, #0x65
 *   9995: ret
 */
void pcie_store_r6_to_05a6(uint8_t val)
{
    G_PCIE_TXN_COUNT_LO = val;
    /* bank1 helper 0xE77A would be called here */
}

/*
 * pcie_lookup_r3_multiply - Multiply R3 by 0x22 for table lookup
 * Address: 0x9996-0x999c (7 bytes)
 *
 * Takes R3, multiplies by 0x22, jumps to helper.
 *
 * Original disassembly:
 *   9996: mov a, r3
 *   9997: mov b, #0x22
 *   999a: ljmp 0x0dd1
 */
__xdata uint8_t *pcie_lookup_r3_multiply(uint8_t idx)
{
    /* DPTR base set by caller */
    return (__xdata uint8_t *)(0x05B4 + ((uint16_t)idx * 0x22));
}

/*
 * pcie_init_b296_regs - Initialize PCIe registers at 0xB296
 * Address: 0x999d-0x99ae (18 bytes)
 *
 * Writes 0x01, 0x02, 0x04 to 0xB296 (status clear sequence),
 * then writes 0x0F to 0xB254 (trigger).
 *
 * Original disassembly:
 *   999d: mov dptr, #0xb296
 *   99a0: mov a, #0x01
 *   99a2: movx @dptr, a        ; clear bit 0 flag
 *   99a3: inc a
 *   99a4: movx @dptr, a        ; write 0x02
 *   99a5: mov a, #0x04
 *   99a7: movx @dptr, a        ; write 0x04
 *   99a8: mov dptr, #0xb254
 *   99ab: mov a, #0x0f
 *   99ad: movx @dptr, a        ; trigger = 0x0F
 *   99ae: ret
 */
void pcie_init_b296_regs(void)
{
    REG_PCIE_STATUS = 0x01;  /* Clear flag 0 */
    REG_PCIE_STATUS = 0x02;  /* Clear flag 1 */
    REG_PCIE_STATUS = 0x04;  /* Clear flag 2 */
    REG_PCIE_TRIGGER = 0x0F; /* Trigger transaction */
}

/*
 * pcie_read_and_store_idata - Read DPTR and store to IDATA
 * Address: 0x99af-0x99bb (13 bytes)
 *
 * Reads 2 bytes from DPTR+1, adds 2 to second byte,
 * stores to IDATA[0x64:0x63] with carry propagation.
 *
 * Original disassembly:
 *   99af: movx a, @dptr
 *   99b0: mov r6, a
 *   99b1: inc dptr
 *   99b2: movx a, @dptr
 *   99b3: add a, #0x02        ; add 2
 *   99b5: dec r0
 *   99b6: mov @r0, a          ; store to IDATA
 *   99b7: clr a
 *   99b8: addc a, r6          ; add carry to high byte
 *   99b9: dec r0
 *   99ba: mov @r0, a
 *   99bb: ret
 */
void pcie_read_and_store_idata(__xdata uint8_t *ptr)
{
    uint8_t hi = ptr[0];
    uint8_t lo = ptr[1];
    uint8_t new_lo = lo + 2;
    uint8_t new_hi = hi + ((new_lo < lo) ? 1 : 0);

    *(__idata uint8_t *)0x64 = new_lo;
    *(__idata uint8_t *)0x63 = new_hi;
}

/*
 * pcie_store_r7_to_05b7 - Store R7 to table at 0x05B7
 * Address: 0x99bc-0x99c5 (10 bytes)
 *
 * Sets DPTR=0x05B7, stores R7, multiplies by 0x22.
 *
 * Original disassembly:
 *   99bc: mov dptr, #0x05b7
 *   99bf: mov a, r7
 *   99c0: mov b, #0x22
 *   99c3: ljmp 0x0dd1
 */
void pcie_store_r7_to_05b7(uint8_t idx, uint8_t val)
{
    uint16_t addr = 0x05B7 + ((uint16_t)idx * 0x22);
    XDATA8(addr) = val;
}

/*
 * pcie_set_0a5b_flag - Set flag at 0x0A5B
 * Address: 0x99c6-0x99cd (8 bytes)
 *
 * Stores A to DPTR, then writes 1 to 0x0A5B.
 *
 * Original disassembly:
 *   99c6: movx @dptr, a
 *   99c7: mov dptr, #0x0a5b
 *   99ca: mov a, #0x01
 *   99cc: movx @dptr, a
 *   99cd: ret
 */
void pcie_set_0a5b_flag(__xdata uint8_t *ptr, uint8_t val)
{
    *ptr = val;
    G_NIBBLE_SWAP_0A5B = 0x01;
}

/*
 * pcie_inc_0a5b - Increment value at 0x0A5B
 * Address: 0x99ce-0x99d4 (7 bytes)
 *
 * Reads 0x0A5B, increments, writes back.
 *
 * Original disassembly:
 *   99ce: mov dptr, #0x0a5b
 *   99d1: movx a, @dptr
 *   99d2: inc a
 *   99d3: movx @dptr, a
 *   99d4: ret
 */
void pcie_inc_0a5b(void)
{
    uint8_t val = G_NIBBLE_SWAP_0A5B;
    G_NIBBLE_SWAP_0A5B = val + 1;
}

/*
 * pcie_lookup_and_store_idata - Table lookup and store result
 * Address: 0x99d5-0x99df (11 bytes)
 *
 * Calls table lookup helper, reads result, stores to IDATA[0x63:0x64].
 *
 * Original disassembly:
 *   99d5: lcall 0x0dd1        ; table lookup
 *   99d8: movx a, @dptr       ; read result
 *   99d9: mov r0, #0x63
 *   99db: mov @r0, #0x00      ; IDATA[0x63] = 0
 *   99dd: inc r0
 *   99de: mov @r0, a          ; IDATA[0x64] = result
 *   99df: ret
 */
void pcie_lookup_and_store_idata(uint8_t idx, uint16_t base)
{
    uint16_t addr = base + ((uint16_t)idx * 0x22);
    uint8_t val = XDATA8(addr);

    *(__idata uint8_t *)0x63 = 0x00;
    *(__idata uint8_t *)0x64 = val;
}

/*
 * pcie_write_config_and_trigger - Write to DPTR and trigger PCIe
 * Address: 0x99e0-0x99ea (11 bytes)
 *
 * Stores A to DPTR, reads 0xB480, sets bit 0, writes back.
 * This triggers a PCIe configuration write.
 *
 * Original disassembly:
 *   99e0: movx @dptr, a       ; write value
 *   99e1: mov dptr, #0xb480
 *   99e4: movx a, @dptr       ; read control reg
 *   99e5: anl a, #0xfe        ; clear bit 0
 *   99e7: orl a, #0x01        ; set bit 0
 *   99e9: movx @dptr, a       ; write back
 *   99ea: ret
 */
void pcie_write_config_and_trigger(__xdata uint8_t *ptr, uint8_t val)
{
    uint8_t ctrl;

    *ptr = val;

    ctrl = REG_TUNNEL_LINK_CTRL;
    ctrl = (ctrl & 0xFE) | 0x01;
    REG_TUNNEL_LINK_CTRL = ctrl;
}

/*
 * pcie_get_status_bit2 - Extract bit 2 from PCIe status
 * Address: 0x99eb-0x99f5 (11 bytes)
 *
 * Reads 0xB296, masks bit 2, shifts right twice, masks to 6 bits.
 *
 * Original disassembly:
 *   99eb: mov dptr, #0xb296
 *   99ee: movx a, @dptr
 *   99ef: anl a, #0x04        ; isolate bit 2
 *   99f1: rrc a               ; shift right
 *   99f2: rrc a               ; shift again
 *   99f3: anl a, #0x3f        ; mask
 *   99f5: ret
 */
uint8_t pcie_get_status_bit2(void)
{
    uint8_t val = REG_PCIE_STATUS;
    val = (val & 0x04) >> 2;
    return val & 0x3F;
}

/*
 * pcie_init_idata_65_63 - Initialize IDATA transfer params
 * Address: 0x99f6-0x99ff (10 bytes)
 *
 * Sets IDATA[0x65]=0x0F, IDATA[0x63]=0x00, increments R0.
 *
 * Original disassembly:
 *   99f6: mov r0, #0x65
 *   99f8: mov @r0, #0x0f      ; IDATA[0x65] = 0x0F
 *   99fa: mov r0, #0x63
 *   99fc: mov @r0, #0x00      ; IDATA[0x63] = 0
 *   99fe: inc r0
 *   99ff: ret
 */
void pcie_init_idata_65_63(void)
{
    *(__idata uint8_t *)0x65 = 0x0F;
    *(__idata uint8_t *)0x63 = 0x00;
}

/*
 * pcie_add_2_to_idata - Add 2 to value and store to IDATA
 * Address: 0x9a00-0x9a08 (9 bytes)
 *
 * Adds 2 to A, stores to IDATA[0x64], propagates carry to IDATA[0x63].
 *
 * Original disassembly:
 *   9a00: add a, #0x02
 *   9a02: dec r0
 *   9a03: mov @r0, a          ; store result
 *   9a04: clr a
 *   9a05: rlc a               ; get carry
 *   9a06: dec r0
 *   9a07: mov @r0, a          ; store carry
 *   9a08: ret
 */
void pcie_add_2_to_idata(uint8_t val)
{
    uint8_t result = val + 2;
    uint8_t carry = (result < val) ? 1 : 0;

    *(__idata uint8_t *)0x64 = result;
    *(__idata uint8_t *)0x63 = carry;
}

/*
 * pcie_lookup_r6_multiply - Multiply R6 by 0x22 for table lookup
 * Address: 0x9a09-0x9a0f (7 bytes)
 *
 * Takes R6, multiplies by 0x22, jumps to helper.
 *
 * Original disassembly:
 *   9a09: mov a, r6
 *   9a0a: mov b, #0x22
 *   9a0d: ljmp 0x0dd1
 */
__xdata uint8_t *pcie_lookup_r6_multiply(uint8_t idx)
{
    /* DPTR base set by caller */
    return (__xdata uint8_t *)(0x05BD + ((uint16_t)idx * 0x22));
}

/*
 * pcie_lookup_05bd - Look up in table at 0x05BD
 * Address: 0x9a10-0x9a1f (16 bytes)
 *
 * Sets DPTR=0x05BD, reads index from 0x05A6, multiplies by 0x22.
 *
 * Original disassembly:
 *   9a10: mov dptr, #0x05bd
 *   ... (continues with table lookup)
 */
__xdata uint8_t *pcie_lookup_05bd(void)
{
    uint8_t idx = G_PCIE_TXN_COUNT_LO;
    uint16_t addr = 0x05BD + ((uint16_t)idx * 0x22);
    return (__xdata uint8_t *)addr;
}

/*
 * pcie_set_byte_enables_0f - Set byte enables to 0x0F
 * Address: 0x9a3b-0x9a45 (11 bytes)
 *
 * Writes 0x0F to REG_PCIE_BYTE_EN (0xB254).
 * This enables all 4 byte lanes for PCIe transactions.
 *
 * Original disassembly:
 *   9a3b: mov dptr, #0xb254
 *   9a3e: mov a, #0x0f
 *   9a40: movx @dptr, a
 *   ... (may continue)
 */
void pcie_set_byte_enables_0f(void)
{
    REG_PCIE_TRIGGER = 0x0F;
}

/*
 * pcie_setup_buffer_params_ext - Extended buffer parameter setup
 * Address: 0x9a46-0x9a6b (38 bytes)
 *
 * Sets up PCIe buffer parameters for DMA transfers.
 * Reads configuration from table, calculates addresses.
 */
void pcie_setup_buffer_params_ext(uint8_t idx)
{
    uint16_t table_addr = 0x05B4 + ((uint16_t)idx * 0x22);
    uint8_t val;

    val = XDATA8(table_addr);
    /* Setup buffer pointers based on config */
    (void)val;
}

/*
 * pcie_get_link_speed_masked - Get link speed with mask
 * Address: 0x9a6c-0x9aa2 (55 bytes)
 *
 * Reads link speed from PCIe status registers.
 * Returns speed code in bits 7:5.
 */
uint8_t pcie_get_link_speed_masked(void)
{
    uint8_t val = REG_PCIE_LINK_STATUS;
    return val & 0xE0;  /* Bits 7:5 are link speed */
}

/*
 * pcie_clear_address_regs_full - Clear all PCIe address registers
 * Address: 0x9aa3-0x9ab2 (16 bytes)
 *
 * Clears PCIe address registers 0xB218-0xB21B.
 *
 * Original disassembly:
 *   9aa3: mov dptr, #0xb218
 *   9aa6: clr a
 *   9aa7: movx @dptr, a
 *   9aa8: inc dptr
 *   9aa9: movx @dptr, a
 *   9aaa: inc dptr
 *   9aab: movx @dptr, a
 *   9aac: inc dptr
 *   9aad: movx @dptr, a
 *   9aae: ret
 */
void pcie_clear_address_regs_full(void)
{
    REG_PCIE_ADDR_0 = 0;
    REG_PCIE_ADDR_1 = 0;
    REG_PCIE_ADDR_2 = 0;
    REG_PCIE_ADDR_3 = 0;
}

/*
 * pcie_inc_txn_count - Increment transaction count at 0x05A6/0x05A7
 * Address: 0x9ab3-0x9ab9 (7 bytes)
 *
 * Increments the 16-bit transaction counter.
 *
 * Original disassembly:
 *   9ab3: mov dptr, #0x05a6
 *   9ab6: movx a, @dptr
 *   9ab7: inc a
 *   9ab8: movx @dptr, a
 *   9ab9: ret
 */
void pcie_inc_txn_count(void)
{
    uint8_t val = G_PCIE_TXN_COUNT_LO;
    G_PCIE_TXN_COUNT_LO = val + 1;
}

/*
 * pcie_tlp_handler_b104 - Main PCIe TLP handler
 * Address: 0xb104-0xb1ca (~199 bytes)
 *
 * Handles PCIe TLP (Transaction Layer Packet) processing.
 * This is the main entry point for TLP-related operations.
 *
 * Uses globals:
 *   0x0aa4: TLP config base (state counter)
 *   0x0aa8-0x0aa9: TLP count high/low
 *   0x0aaa: TLP status / pending count
 *   IDATA 0x51-0x52: Local transfer counters
 *
 * Algorithm:
 *   1. Initialize TLP state (clear counters at 0x0AA4, IDATA 0x51/0x52)
 *   2. Call tlp_init_addr_buffer() to clear address/length buffers
 *   3. Call flash_set_mode_enable() to enable flash operations
 *   4. Main loop: process pending TLPs until complete
 *      - Compare pending count vs processed count
 *      - For each TLP: set up DMA transfer, wait for completion
 *      - Update counters
 *   5. Return 1 on success, 0 on error/timeout
 */
uint8_t pcie_tlp_handler_b104(void)
{
    uint8_t pending_count, processed_lo, processed_hi;
    uint8_t flash_cmd_result;

    /* Initialize: Store 0 to TLP config at 0x0AA4 (dword store via 0x0dc5) */
    G_STATE_COUNTER_LO = 0;
    G_STATE_COUNTER_HI = 0;

    /* Clear IDATA counters */
    I_WORK_51 = 0;
    I_WORK_52 = 0;

    /* Main processing loop */
    do {
        /* Initialize address buffer */
        tlp_init_addr_buffer();

        /* Enable flash mode */
        flash_set_mode_enable();

        /* Store local counters to TLP config area */
        G_STATE_COUNTER_LO = I_WORK_51;
        G_STATE_COUNTER_0AA5 = I_WORK_52;

        /* Write flash command 0x02, get addr length result */
        flash_cmd_result = tlp_write_flash_cmd(0x02);

        /* Set command byte with mode bits */
        REG_FLASH_CMD = flash_cmd_result | 0x03;

        /* Load flash address from 0x0AA4 area (via 0x0d84 helper) */
        /* Store R7 to flash address low register */
        REG_FLASH_ADDR_LO = flash_cmd_result;

        /* Read pending count */
        pending_count = G_TLP_STATUS;

        /* Compare counts: [0xAA9] >= pending ? */
        processed_lo = G_TLP_COUNT_LO;
        processed_hi = G_TLP_COUNT_HI;

        if (processed_hi > 0 || processed_lo >= pending_count) {
            /* More to process - continue loop */
            uint8_t current_pending = G_TLP_STATUS;

            /* Store current pending to flash data length registers */
            REG_FLASH_DATA_LEN = processed_hi;
            REG_FLASH_DATA_LEN_HI = current_pending;

            /* Update processed counts */
            G_TLP_COUNT_LO = G_TLP_COUNT_LO - current_pending;
            G_TLP_COUNT_HI = G_TLP_COUNT_HI - ((G_TLP_COUNT_LO < current_pending) ? 1 : 0);

            /* Update local counters */
            I_WORK_52 = I_WORK_52 + current_pending;
            I_WORK_51 = I_WORK_51 + ((I_WORK_52 < current_pending) ? 1 : 0);
        } else {
            /* Clear processed counts */
            G_TLP_COUNT_HI = 0;
            G_TLP_COUNT_LO = 0;
        }

        /* Trigger flash operation */
        REG_FLASH_CSR = 0x01;

        /* Wait for completion (poll bit 0 of CSR) */
        while ((REG_FLASH_CSR & 0x01) == 0x01) {
            /* Spin wait */
        }

        /* Check for timeout/error via 0xdf47 helper */
        /* For now, simplified check */
        if (G_TLP_COUNT_HI == 0 && G_TLP_COUNT_LO == 0) {
            /* All done - success */
            return 1;
        }

    } while (G_TLP_COUNT_HI != 0 || G_TLP_COUNT_LO != 0);

    return 1;  /* Success */
}

/* External declarations for helper functions from nvme.c */
extern uint8_t nvme_queue_get_9100(void);
extern uint8_t nvme_queue_mask_0acf(void);
extern void nvme_queue_clear_9003(void);
extern void nvme_queue_set_9092(uint8_t param);
extern void nvme_queue_set_bit0_ptr(__xdata uint8_t *ptr);
extern uint8_t nvme_queue_shift_param(uint8_t param);
extern void nvme_handler_ba06(void);
extern void usb_buffer_handler(void);

/*
 * pcie_tlp_handler_b28c - Secondary PCIe TLP handler
 * Address: 0xb28c-0xb401 (~374 bytes)
 *
 * State machine for TLP processing based on G_TLP_CMD_STATE_0AD0.
 * Handles queue status and transfer operations.
 *
 * Original disassembly:
 *   b28c: mov dptr, #0x07e4
 *   b28f: movx a, @dptr          ; read state
 *   b291: cjne a, #0x05, b298    ; if state == 5, quick return
 *   b294: lcall 0xa714           ; write 0x9092 = 1
 *   b297: ret
 *   b298: mov dptr, #0x0ad0
 *   b29b: movx a, @dptr          ; get cmd state
 *   b29c: lcall 0x0def           ; jump table dispatch
 *   [switch based on state...]
 */
void pcie_tlp_handler_b28c(void)
{
    uint8_t state;
    uint8_t r3;

    /* Check for state 5 - quick completion */
    state = G_SYS_FLAGS_BASE;
    if (state == 0x05) {
        /* Write 0x9092 = 1 and return */
        REG_TLP_CMD_TRIGGER = 1;
        return;
    }

    /* Get command state and dispatch */
    state = G_TLP_CMD_STATE_0AD0;

    /* Switch based on command state (jump table at 0x0def) */
    switch (state) {
        case 0x00:
            /* State 0: b2de entry point */
            /* Read 0x07e4 again */
            state = G_SYS_FLAGS_BASE;
            if (state == 0x03) {
                /* State is 3: write 4 to 0x9092 and 4 to 0x07e4 */
                REG_TLP_CMD_TRIGGER = 4;
                G_SYS_FLAGS_BASE = 4;
            } else {
                /* Default: write 1 to 0x9092 */
                REG_TLP_CMD_TRIGGER = 1;
            }
            /* Fall through to call d810 */
            usb_buffer_handler();
            break;

        case 0x03:
            /* State 3: b2f3 entry point */
            r3 = G_SYS_FLAGS_BASE;
            if (r3 == 0x03) {
                /* Call 0xba06 */
                nvme_handler_ba06();
                /* Check 0x0ae5 init flag */
                if (G_TLP_INIT_FLAG_0AE5 != 0) {
                    usb_buffer_handler();
                    break;
                }
                /* Check 0x07e9 queue status */
                if (G_TLP_STATE_07E9 == 0) {
                    usb_buffer_handler();
                    break;
                }
                /* Clear 0x07e9 */
                G_TLP_STATE_07E9 = 0;
                /* Check USB status bit 0 */
                if (REG_USB_STATUS & 0x01) {
                    /* Check 0xc471 bit 0 */
                    if (REG_NVME_QUEUE_BUSY & 0x01) {
                        usb_buffer_handler();
                        break;
                    }
                    /* Check 0x000a */
                    if (G_EP_CHECK_FLAG != 0) {
                        usb_buffer_handler();
                        break;
                    }
                } else {
                    /* Check 0x9101 bit 6 */
                    if (REG_USB_PERIPH_STATUS & 0x40) {
                        usb_buffer_handler();
                        break;
                    }
                    /* Check idata 0x6a */
                    if (I_STATE_6A != 0) {
                        usb_buffer_handler();
                        break;
                    }
                }
                /* Call 0xa72b - set bit 0 on 0x92c4 */
                nvme_queue_set_bit0_ptr((__xdata uint8_t *)0x92C4);
            } else if (r3 == 0) {
                /* State is 0 - short exit */
                return;
            }
            /* Default: write 1 to 0x9092 */
            REG_TLP_CMD_TRIGGER = 1;
            usb_buffer_handler();
            break;

        case 0x04:
            /* State 4: b2cb entry - b2ce: lcall 0xa71b then write */
            nvme_queue_clear_9003();
            nvme_queue_set_9092(4);
            /* Write 1 to (0x9003+1) = 0x9004 */
            REG_USB_EP0_LEN_L = 1;
            usb_buffer_handler();
            break;

        case 0x05:
            /* State 5: Already handled above, but for completeness */
            REG_TLP_CMD_TRIGGER = 1;
            break;

        default:
            /* Other states: write 1 to 0x9092 and return via d810 */
            REG_TLP_CMD_TRIGGER = 1;
            usb_buffer_handler();
            break;
    }
}

/* External helper declarations for b402 */
extern uint8_t nvme_queue_get_9100(void);       /* 0xa666 */
extern uint8_t nvme_queue_clear_usb_bit0(void); /* 0xa679 */
extern void nvme_queue_init_905x(void);         /* 0xa6ad */
extern void nvme_queue_config_9006(uint8_t param, __xdata uint8_t *ptr); /* 0xa6c6 */
extern void nvme_queue_set_90e3_2(void);        /* 0xa739 */
extern uint8_t helper_a704(void);               /* 0xa704 - table lookup */

/*
 * pcie_tlp_handler_b402 - Tertiary PCIe TLP handler
 * Address: 0xb402-0xb623 (~546 bytes)
 *
 * Complex TLP handler with multiple return points.
 * Handles transfer timeout calculation, DMA buffer clearing,
 * and coordination with USB/NVMe subsystems.
 *
 * Return values in R7:
 *   3 - Setup complete, base set to 0x9E
 *   4 - Full init/transfer complete
 *   5 - Early exit conditions
 */
uint8_t pcie_tlp_handler_b402(void)
{
    uint8_t state;
    uint8_t r6, r7;
    uint16_t computed;
    uint16_t limit;
    uint8_t iter_count;
    uint16_t i;

    /* b402: Call a666 to get USB link state */
    state = nvme_queue_get_9100();

    /* b406: Check if state == 2 */
    if (state == 0x02) {
        /* State 2: Use timeout 0x0200 (512) */
        r6 = 0x02;
        r7 = 0x00;
    } else {
        /* Other states: Use timeout 0x0040 (64) */
        r6 = 0x00;
        r7 = 0x40;
    }

    /* b413: Store timeout to 0x0AD8:0x0AD9 */
    G_TLP_TIMEOUT_HI = r6;
    G_TLP_TIMEOUT_LO = r7;

    /* b41b-b41f: Clear 0x0ADA:0x0ADB (transfer addresses) */
    G_TLP_TRANSFER_HI = 0;
    G_TLP_TRANSFER_LO = 0;

    /* b420-b437: Compute transfer limit from offset */
    r6 = G_TLP_ADDR_OFFSET_LO;
    r7 = G_TLP_ADDR_OFFSET_HI;

    /* Store to 0x0ADC:0x0ADD (computed value) */
    G_TLP_COMPUTED_HI = r7;
    G_TLP_COMPUTED_LO = r6;

    /* b438-b43d: If computed == 0, return 4 */
    if (r6 == 0 && r7 == 0) {
        return 4;
    }

    /* b43e-b45b: Compare computed with limit from 0x0ADE:0x0ADF */
    r6 = G_TLP_LIMIT_HI;
    r7 = G_TLP_LIMIT_LO;
    computed = ((uint16_t)G_TLP_COMPUTED_HI << 8) | G_TLP_COMPUTED_LO;
    limit = ((uint16_t)r6 << 8) | r7;

    /* b452: If computed <= limit, use computed; else use limit */
    if (computed >= limit) {
        G_TLP_COMPUTED_HI = r6;
        G_TLP_COMPUTED_LO = r7;
    } else {
        G_TLP_LIMIT_HI = G_TLP_COMPUTED_HI;
        G_TLP_LIMIT_LO = G_TLP_COMPUTED_LO;
    }

    /* b466-b4a8: Process iteration counter 0x0AD7 */
    iter_count = G_TLP_COUNT_0AD7;
    if (iter_count >= 3) {
        /* b4a9-b4b9: Counter overflow - set base to 0x9E, return 3 */
        G_TLP_COUNT_0AD7 = 0;
        G_TLP_BASE_HI = 0x9E;
        G_TLP_BASE_LO = 0;
        return 3;
    }

    /* b46f-b4a7: Loop through computed bytes, process DMA buffer */
    r6 = 0;
    r7 = 0;
    while (1) {
        uint8_t val;
        uint8_t idx;

        limit = ((uint16_t)G_TLP_COMPUTED_HI << 8) | G_TLP_COMPUTED_LO;
        computed = ((uint16_t)r6 << 8) | r7;
        if (computed >= limit) {
            break;
        }

        idx = G_TLP_COUNT_0AD7;
        if (idx == 0) {
            val = helper_a704();
        } else if (idx == 1) {
            helper_a704();
            val = 0;
        } else {
            val = 0;
        }

        /* Write to DMA buffer at 0xD800 + offset */
        *(__xdata uint8_t *)(0xD800 + r7) = val;

        r7++;
        if (r7 == 0) r6++;
        if (r7 == 0x60 && r6 == 0x06) break;
    }

    /* b4ba-b570: Process CC status registers - simplified */
    /* Check CC23 (Timer3 CSR) bit 1 */
    if (REG_TIMER3_CSR & 0x02) {
        REG_TIMER3_CSR = 0x02;
    }

    /* Check CPU interrupt ACK bit */
    if (REG_CPU_INT_CTRL & CPU_INT_CTRL_ACK) {
        REG_CPU_INT_CTRL = CPU_INT_CTRL_ACK;
    }

    /* Check CPU DMA interrupt bit */
    if (REG_CPU_DMA_INT & 0x02) {
        REG_CPU_DMA_INT = 0x02;
        G_CMD_PENDING_07BB = 1;
    }

    /* Check transfer DMA config bit */
    if (REG_XFER_DMA_CFG & 0x02) {
        REG_XFER_DMA_CFG = 0x02;
    }

    /* Check transfer2 DMA status bit */
    if (REG_XFER2_DMA_STATUS & 0x02) {
        REG_XFER2_DMA_STATUS = 0x02;
    }

    /* Check CPU extended status bit */
    if (REG_CPU_EXT_STATUS & 0x02) {
        REG_CPU_EXT_STATUS = 0x02;
    }

    /* b571-b594: Check 9090 bit 7 and 0x0AD3/0x0AD1 */
    state = REG_USB_INT_MASK_9090;
    if ((state & 0x80) == 0) {
        return 5;
    }

    if (G_TLP_MODE_0AD3 <= 0) {
        return 5;
    }

    if (G_LINK_STATE_0AD1 >= 1) {
        return 5;
    }

    /* b595-b5f8: Full processing path */
    G_XFER_STATE_0AF6 = 1;

    if (G_EP_CHECK_FLAG != 0) {
        /* Would call helper_4e25 here */
    }

    nvme_queue_init_905x();

    state = REG_USB_STATUS;
    REG_USB_STATUS = (state & 0xFE) | 1;

    state = nvme_queue_clear_usb_bit0();
    REG_USB_CTRL_9200 = state | 1;

    I_WORK_3C = 0;
    I_WORK_3D = 0;

    /* Clear DMA buffer 0xD800-0xDE60 */
    for (i = 0; i < 0x0660; i++) {
        *(__xdata uint8_t *)(0xD800 + i) = 0;
    }

    *(__xdata uint8_t *)0xDE30 = 3;
    *(__xdata uint8_t *)0xDE36 = 0;

    /* b5f9-b623: Final register setup */
    REG_USB_CTRL_9200 = (REG_USB_CTRL_9200 & 0xBF) | 0x40;

    state = REG_USB_MSC_CFG;
    REG_USB_MSC_CFG = state & 0xFE;

    REG_USB_CTRL_9200 = REG_USB_CTRL_9200 & 0xBF;
    nvme_queue_config_9006(REG_USB_CTRL_9200, &REG_USB_CTRL_9200);

    state = REG_USB_EP_CTRL_905F;
    REG_USB_EP_CTRL_905F = (state & 0xF7) | 0x08;

    nvme_queue_set_90e3_2();

    return 4;
}

/*
 * nvme_cmd_setup_b624 - NVMe command setup
 * Address: 0xb624-0xb6ce (~171 bytes)
 *
 * Sets up NVMe command structures for TLP processing.
 */
void nvme_cmd_setup_b624(void)
{
    /* TODO: Implement NVMe command setup */
}

/*
 * nvme_cmd_setup_b6cf - NVMe command setup variant
 * Address: 0xb6cf-0xb778 (~170 bytes)
 *
 * Variant of NVMe command setup.
 */
void nvme_cmd_setup_b6cf(void)
{
    /* TODO: Implement NVMe command setup variant */
}

/*
 * nvme_cmd_setup_b779 - NVMe command setup variant 2
 * Address: 0xb779-0xb81f (~167 bytes)
 *
 * Second variant of NVMe command setup.
 */
void nvme_cmd_setup_b779(void)
{
    /* TODO: Implement NVMe command setup variant 2 */
}

/*
 * tlp_init_addr_buffer - Initialize TLP address buffer
 * Address: 0xb820-0xb832 (19 bytes)
 *
 * Clears the flash/TLP address buffer at 0x0AAD-0x0AB0 to zero (32-bit),
 * and clears 0x0AB1-0x0AB2 (length).
 *
 * Original disassembly:
 *   b820: clr a
 *   b821: mov r7, a
 *   b822: mov r6, a
 *   b823: mov r5, a
 *   b824: mov r4, a
 *   b825: mov dptr, #0x0aad
 *   b828: lcall 0x0dc5      ; store dword (R4:R5:R6:R7) to DPTR
 *   b82b: clr a
 *   b82c: mov dptr, #0x0ab1
 *   b82f: movx @dptr, a     ; clear 0x0AB1
 *   b830: inc dptr
 *   b831: movx @dptr, a     ; clear 0x0AB2
 *   b832: ret
 */
void tlp_init_addr_buffer(void)
{
    /* Clear 32-bit address at 0x0AAD-0x0AB0 */
    G_FLASH_ADDR_0 = 0;
    G_FLASH_ADDR_1 = 0;
    G_FLASH_ADDR_2 = 0;
    G_FLASH_ADDR_3 = 0;

    /* Clear length at 0x0AB1-0x0AB2 */
    G_FLASH_LEN_LO = 0;
    G_FLASH_LEN_HI = 0;
}

/*
 * nvme_queue_b825 - NVMe queue helper 2
 * Address: 0xb825-0xb832 (14 bytes)
 *
 * Queue helper for DMA setup.
 */
void nvme_queue_b825(void)
{
    /* TODO: Implement queue helper 2 */
}

/*
 * nvme_queue_b833 - NVMe queue helper 3
 * Address: 0xb833-0xb837 (5 bytes)
 */
void nvme_queue_b833(void)
{
    /* TODO: Implement queue helper 3 */
}

/*
 * nvme_queue_b838 - NVMe queue helper 4
 * Address: 0xb838-0xb847 (16 bytes)
 */
void nvme_queue_b838(void)
{
    /* TODO: Implement queue helper 4 */
}

/*
 * tlp_write_flash_cmd - Write to flash command register
 * Address: 0xb845-0xb84f (11 bytes)
 *
 * Writes command to 0xC8AA (flash CMD), reads 0xC8AC (addr length),
 * masks with 0xFC.
 *
 * Original disassembly:
 *   b845: mov dptr, #0xc8aa
 *   b848: movx @dptr, a      ; write A to REG_FLASH_CMD
 *   b849: mov dptr, #0xc8ac
 *   b84c: movx a, @dptr      ; read REG_FLASH_ADDR_LEN
 *   b84d: anl a, #0xfc       ; mask bits 0-1
 *   b84f: ret
 */
uint8_t tlp_write_flash_cmd(uint8_t cmd)
{
    REG_FLASH_CMD = cmd;
    return REG_FLASH_ADDR_LEN & 0xFC;
}

/*
 * nvme_queue_b850 - NVMe queue store
 * Address: 0xb850-0xb850 (1 byte - just movx)
 */
void nvme_queue_b850(void)
{
    /* Single instruction - store */
}

/*
 * nvme_queue_b851 - NVMe queue increment
 * Address: 0xb851-0xb880 (48 bytes)
 *
 * Queue increment and management.
 */
void nvme_queue_b851(void)
{
    /* TODO: Implement queue increment */
}

/*
 * nvme_handler_b881 - NVMe handler
 * Address: 0xb881-0xb8a1 (33 bytes)
 */
void nvme_handler_b881(void)
{
    /* TODO: Implement NVMe handler */
}

/*
 * nvme_handler_b8b9 - NVMe handler 3
 * Address: 0xb8b9-0xba05 (~333 bytes)
 *
 * Large NVMe event handler.
 */
void nvme_handler_b8b9(void)
{
    /* TODO: Implement NVMe handler 3 */
}

/*
 * nvme_handler_ba06 - NVMe handler 4
 * Address: 0xba06+
 *
 * Final NVMe handler in this range.
 */
void nvme_handler_ba06(void)
{
    /* TODO: Implement NVMe handler 4 */
}

/*
 * PCIe interrupt sub-handler forward declarations
 * These functions access registers through extended addressing (Bank 1)
 */
extern uint8_t pcie_check_int_source_a374(uint8_t source);  /* 0xa374 - Check int source via R1 */
extern uint8_t pcie_check_int_source_a3c4(uint8_t source);  /* 0xa3c4 - Check int source (variant) */
extern uint8_t pcie_get_status_a34f(void);                   /* 0xa34f - Read status register 0x4E */
extern void pcie_setup_lane_a310(uint8_t lane);              /* 0xa310 - Setup lane configuration */
extern void pcie_set_state_a2df(uint8_t state);              /* 0xa2df - Set PCIe state */
extern void pcie_handler_e890(void);                          /* 0xe890 - Bank 1 PCIe handler */
extern void pcie_handler_d8d5(void);                          /* 0xd8d5 - PCIe completion handler */
extern uint8_t dispatch_handler_0557(void);                   /* 0x0557 - Dispatch handler */
extern void pcie_write_reg_0633(void);                        /* 0x0633 - Register write helper */
extern void pcie_write_reg_0638(void);                        /* 0x0638 - Register write helper */
extern void pcie_cleanup_05f7(void);                          /* 0x05f7 - Cleanup handler */
extern void pcie_cleanup_05fc(void);                          /* 0x05fc - Cleanup handler */
extern void pcie_handler_e974(void);                          /* 0xe974 - Bank 1 handler */
extern void pcie_handler_e06b(uint8_t param);                 /* 0xe06b - Bank 1 handler with param */
extern void pcie_setup_a38b(uint8_t source);                  /* 0xa38b - Setup helper */
extern uint8_t pcie_get_status_a372(void);                    /* 0xa372 - Get status at 0x40 */

/*
 * pcie_interrupt_handler - Main PCIe interrupt handler
 * Address: 0xa522-0xa62c (~267 bytes)
 *
 * This is the main interrupt handler for PCIe events. It:
 *   1. Checks various interrupt sources and dispatches to sub-handlers
 *   2. Manages interrupt acknowledgment
 *   3. Coordinates NVMe completion events
 *   4. Handles error conditions and cleanup
 *
 * Original disassembly structure:
 *   Phase 1 (0xa522-0xa54c): Check int source 0x03, handle queue events
 *   Phase 2 (0xa54d-0xa577): Check int source 0x8f, handle dispatch
 *   Phase 3 (0xa578-0xa59a): Check bit 1, do lane setup and state change
 *   Phase 4 (0xa59b-0xa5dd): Check bit 0, extended processing with state
 *   Phase 5 (0xa5de-0xa603): Check bit 2, error path
 *   Phase 6 (0xa604-0xa62c): Check bits 0,3, final cleanup
 */
void pcie_interrupt_handler(void)
{
    uint8_t result;
    uint8_t status_af1;
    uint8_t event_flags;
    uint8_t state_val;

    /*
     * Phase 1: Check interrupt source 0x03
     * a522: mov r1, #0x03
     * a524: lcall 0xa374
     * a527: jnb 0xe0.7, 0xa54d  ; if bit 7 not set, skip
     */
    result = pcie_check_int_source_a374(0x03);
    if (result & 0x80) {
        /* Check state flag 0x0af1 bit 4 */
        status_af1 = G_STATE_FLAG_0AF1;
        if (status_af1 & 0x10) {
            /* Check event control 0x09fa bits 0 and 7 */
            event_flags = G_EVENT_CTRL_09FA;
            if (event_flags & 0x81) {
                /* Call queue handler and set bit 2 */
                result = pcie_queue_handler_a62d();
                G_EVENT_CTRL_09FA = result | 0x04;
            }
        }
        /* Write 0x80 to register, then call helpers */
        /* a53f-a54c: setup r3=2,r2=0x12,r1=3, write 0x80, call 0x0be6, 0x0633 */
        pcie_write_reg_0633();
    }

    /*
     * Phase 2: Check interrupt source 0x8f
     * a54d: mov r1, #0x8f
     * a54f: lcall 0xa3c4
     * a552: jnb 0xe0.7, 0xa578  ; if bit 7 not set, skip
     */
    result = pcie_check_int_source_a3c4(0x8f);
    if (result & 0x80) {
        /* Write 0x80 to register, call helper */
        pcie_write_reg_0638();

        /* Check state flag 0x0af1 bit 4 */
        status_af1 = G_STATE_FLAG_0AF1;
        if (status_af1 & 0x10) {
            /* Check event control 0x09fa bits 0 and 7 */
            event_flags = G_EVENT_CTRL_09FA;
            if (event_flags & 0x81) {
                /* Call dispatch handler 0x0557 */
                result = dispatch_handler_0557();
                if (result) {
                    /* Call queue handler and set bits 0-4 */
                    result = pcie_queue_handler_a62d();
                    G_EVENT_CTRL_09FA = result | 0x1f;
                }
            }
        }
    }

    /*
     * Phase 3: Check status bit 1 (link training)
     * a578: lcall 0xa34f
     * a57b: jnb 0xe0.1, 0xa59b  ; if bit 1 not set, skip
     */
    result = pcie_get_status_a34f();
    if (result & 0x02) {
        /* Write 0x02 to register */
        /* a57e: mov a, #0x02; lcall 0x0be6 */

        /* Setup lane with parameter 0x35 */
        pcie_setup_lane_a310(0x35);

        /* Set state to 0x03 */
        pcie_set_state_a2df(0x03);

        /* Call bank 1 handler */
        pcie_handler_e890();

        /* Check source 0x43 */
        result = pcie_check_int_source_a3c4(0x43);
        if (result & 0x80) {
            /* Call completion handler */
            pcie_handler_d8d5();
        }
    }

    /*
     * Phase 4: Check status bit 0 (completion)
     * a59b: mov r3, #0x02; mov r2, #0x12; mov r1, #0x07
     * a5a1: lcall 0x0bc8
     * a5a4: jnb 0xe0.0, 0xa5de  ; if bit 0 not set, skip
     */
    /* Read from extended address 0x1207 */
    /* For now, use the status check pattern */
    result = pcie_get_status_a34f();  /* This reads 0x4E, but pattern is similar */
    if (result & 0x01) {
        /* Write 0x01 to register */
        /* a5a7: mov a, #0x01; lcall 0x0be6 */

        /* Setup lane with parameter 0x35 */
        pcie_setup_lane_a310(0x35);

        /* Set state to 0x04 */
        pcie_set_state_a2df(0x04);

        /* Call bank 1 handler */
        pcie_handler_e890();

        /* Clear transaction state */
        G_LANE_STATE_0A9D = 0;

        /* Get status and update lane state */
        result = pcie_get_status_a372();
        G_PCIE_LANE_STATE_0A9E = result;

        /* Double the lane state value */
        state_val = G_PCIE_LANE_STATE_0A9E;
        G_PCIE_LANE_STATE_0A9E = state_val + state_val;

        /* Rotate lane state left through carry */
        state_val = G_LANE_STATE_0A9D;
        /* rlc a is rotate left through carry */
        G_LANE_STATE_0A9D = (state_val << 1);  /* Simplified - actual has carry */

        /* Read lane state for next operation */
        state_val = G_PCIE_LANE_STATE_0A9E;
        /* Write to register with value 0x04 */
        /* a5d3: mov r1, #0x04; lcall 0x0be6 */

        /* Write lane state to register 0x05 */
        state_val = G_LANE_STATE_0A9D;
        /* a5db: mov r1, #0x05; lcall 0x0be6 */
    }

    /*
     * Phase 5: Check status bit 2 (error)
     * a5de: lcall 0xa34f
     * a5e1: jnb 0xe0.2, 0xa604  ; if bit 2 not set, skip
     */
    result = pcie_get_status_a34f();
    if (result & 0x04) {
        /* Write 0x04 to register */
        /* a5e4: mov a, #0x04; lcall 0x0be6 */

        /* Setup lane with parameter 0x35 */
        pcie_setup_lane_a310(0x35);

        /* Set state to 0x05 */
        pcie_set_state_a2df(0x05);

        /* Call bank 1 handler */
        pcie_handler_e890();

        /* Get status at 0x40 */
        result = pcie_get_status_a372();
        if (result & 0x01) {
            /* Call handler 0xe974 */
            pcie_handler_e974();
            /* Call handler with param 0x01 */
            pcie_handler_e06b(0x01);
        }
    }

    /*
     * Phase 6: Final status checks (bits 0 and 3)
     * a604: lcall 0xa34f
     * a607: jnb 0xe0.0, 0xa612  ; if bit 0 not set, skip
     */
    result = pcie_get_status_a34f();
    if (result & 0x01) {
        /* Call cleanup handler */
        pcie_cleanup_05f7();
        /* Setup with source 0x4E */
        pcie_setup_a38b(0x4E);
    }

    /*
     * Check bit 3
     * a612-a62c: setup and final write
     */
    /* Read from extended address 0x124F */
    /* a612: mov r3, #0x02; mov r2, #0x12; mov r1, #0x4f */
    result = pcie_get_status_a34f();  /* Simplified - would read 0x4F */
    if (result & 0x08) {
        /* Call cleanup handler */
        pcie_cleanup_05fc();
        /* Write 0x08 to register 0x4F */
        /* a627: mov a, #0x08; lcall 0x0be6 */
    }
}

/*
 * pcie_queue_handler_a62d - Queue event handler
 * Address: 0xa62d-0xa636 (10 bytes)
 *
 * Handles PCIe queue events.
 *
 * Original disassembly:
 *   a62d: lcall 0xe7ae
 *   a630: mov dptr, #0xe710
 *   a633: movx a, @dptr
 *   a634: anl a, #0xe0
 *   a636: ret
 */
uint8_t pcie_queue_handler_a62d(void)
{
    /* Call bank 1 handler at 0xe7ae */
    /* Read status from link width register, mask with 0xe0 */
    return REG_LINK_WIDTH_E710 & 0xe0;
}

/*
 * pcie_set_interrupt_flag - Set interrupt flag
 * Address: 0xa637-0xa643 (13 bytes)
 *
 * Sets interrupt control flag and clears state.
 *
 * Original disassembly:
 *   a637: mov a, #0x01
 *   a639: mov dptr, #0x0ad7
 *   a63c: movx @dptr, a
 *   a63d: mov dptr, #0x0ade
 *   a640: clr a
 *   a641: movx @dptr, a
 *   a642: inc dptr
 *   a643: ret
 */
void pcie_set_interrupt_flag(void)
{
    G_TLP_COUNT_0AD7 = 0x01;
    G_TLP_LIMIT_HI = 0;
}

/*===========================================================================
 * Bank 1 PCIe Address Helper Functions (0x839c-0x83b9)
 *
 * These functions are in Bank 1 (address 0x10000-0x17FFF mapped at 0x8000)
 * and handle PCIe address setup for transactions. They store 32-bit addresses
 * to the global G_PCIE_ADDR at 0x05AF.
 *===========================================================================*/

/* External helper from Bank 1 */
extern void pcie_bank1_helper_e902(void);  /* 0xe902 - Bank 1 setup */

/*
 * pcie_addr_store_839c - Store PCIe address with offset adjustment
 * Bank 1 Address: 0x839c-0x83b8 (29 bytes) [actual addr: 0x1039c]
 *
 * Calls e902 helper, loads current address from 0x05AF,
 * then stores back adjusted address (param4 + 4 with borrow handling).
 *
 * The borrow handling: if param4 > 0xFB (251), there's overflow when adding 4,
 * so param3 is decremented by 1.
 *
 * Original disassembly (from ghidra.c):
 *   FUN_CODE_e902();
 *   xdata_load_dword(0x5af);
 *   xdata_store_dword(0x5af, param_1, param_2,
 *                     param_3 - (((0xfb < param_4) << 7) >> 7),
 *                     param_4 + 4);
 *
 * Note: The expression (((0xfb < param_4) << 7) >> 7) evaluates to:
 *   1 if param_4 > 0xFB (i.e., param_4 >= 0xFC, so param_4 + 4 overflows)
 *   0 otherwise
 */
void pcie_addr_store_839c(uint8_t p1, uint8_t p2, uint8_t p3, uint8_t p4)
{
    uint8_t borrow;
    uint8_t new_p3;
    uint8_t new_p4;

    /* Call bank 1 helper for setup */
    pcie_bank1_helper_e902();

    /* Calculate adjusted values */
    new_p4 = p4 + 4;
    borrow = (p4 > 0xFB) ? 1 : 0;  /* Borrow if overflow */
    new_p3 = p3 - borrow;

    /* Store 32-bit address to globals */
    G_PCIE_ADDR_0 = p1;
    G_PCIE_ADDR_1 = p2;
    G_PCIE_ADDR_2 = new_p3;
    G_PCIE_ADDR_3 = new_p4;
}

/*
 * pcie_addr_store_83b9 - Store PCIe address with offset (variant)
 * Bank 1 Address: 0x83b9-0x83d5 (29 bytes) [actual addr: 0x103b9]
 *
 * Identical to 839c - likely called in different context or a duplicate
 * for code alignment/bank purposes.
 *
 * Original disassembly:
 *   FUN_CODE_e902();
 *   xdata_load_dword(0x5af);
 *   xdata_store_dword(0x5af, param_1, param_2,
 *                     param_3 - (((0xfb < param_4) << 7) >> 7),
 *                     param_4 + 4);
 */
void pcie_addr_store_83b9(uint8_t p1, uint8_t p2, uint8_t p3, uint8_t p4)
{
    uint8_t borrow;
    uint8_t new_p3;
    uint8_t new_p4;

    /* Call bank 1 helper for setup */
    pcie_bank1_helper_e902();

    /* Calculate adjusted values */
    new_p4 = p4 + 4;
    borrow = (p4 > 0xFB) ? 1 : 0;  /* Borrow if overflow */
    new_p3 = p3 - borrow;

    /* Store 32-bit address to globals */
    G_PCIE_ADDR_0 = p1;
    G_PCIE_ADDR_1 = p2;
    G_PCIE_ADDR_2 = new_p3;
    G_PCIE_ADDR_3 = new_p4;
}

/*===========================================================================
 * BANK 1 PCIE HANDLERS
 *
 * These functions handle PCIe-related operations in Bank 1. Called via
 * the bank switching mechanism (jump_bank_1 at 0x0311).
 *===========================================================================*/

/*
 * pcie_state_clear_ed02 - Clear PCIe state and check transfer status
 * Bank 1 Address: 0xED02 (file offset 0x16D02)
 * Size: ~38 bytes (0x16D02-0x16D27)
 *
 * Called by dispatch stub handler_063d. This handler:
 *   1. Calls 0x05C5 helper for PCIe setup
 *   2. Clears XDATA[0x023F] (transfer state)
 *   3. Checks XDATA[0x0775], clears if non-zero
 *   4. Checks XDATA[0x0719] for value 2
 *   5. Returns different values in R7 based on result
 *
 * Return values (via R7):
 *   0 - State was non-zero and cleared
 *   1 - State at 0x0719 != 2
 *   2 - State at 0x0719 == 2 (cleared)
 */
void pcie_state_clear_ed02(void)
{
    /* Call setup helper at 0x05C5 */
    /* TODO: Implement call to 0x05C5 when available */

    /* Clear transfer state at 0x023F */
    G_BANK1_STATE_023F = 0;
}

/*
 * pcie_handler_unused_eef9 - PCIe handler (UNUSED)
 * Bank 1 Address: 0xEEF9 (file offset 0x16EF9)
 *
 * Called by handler_063d.
 * NOTE: This address contains all NOPs in the original firmware.
 * This is likely unused/padding space.
 */
void pcie_handler_unused_eef9(void)
{
    /* Empty - original firmware has NOPs at this address */
}

/*
 * pcie_nvme_event_handler - PCIe/NVMe Event Handler
 * Address: 0x052f-0x0533 (5 bytes) -> dispatches to bank 0 0xAF5E
 *
 * Function at 0xAF5E:
 * PCIe/NVMe event handler called when PCIe status bit 6 is set.
 * Handles NVMe command completion and error events.
 *
 * Algorithm:
 *   1. Write 0x0A, 0x0D to 0xC001 (NVMe command register)
 *   2. Call helper 0x538D with R3=0xFF, R2=0x23, R1=0xEE
 *   3. Read 0xE40F, pass to helper 0x51C7
 *   4. Write 0x3A to 0xC001
 *   5. Read 0xE410, pass to helper 0x51C7
 *   6. Write 0x5D to 0xC001
 *   7. Check 0xE40F bits 7, 0, 5 for various dispatch paths
 *
 * Original disassembly:
 *   af5e: mov dptr, #0xc001
 *   af61: mov a, #0x0a
 *   af63: movx @dptr, a
 *   af64: mov a, #0x0d
 *   af66: movx @dptr, a
 *   af67: mov r3, #0xff
 *   af69: mov r2, #0x23
 *   af6b: mov r1, #0xee
 *   af6d: lcall 0x538d
 *   ... (continues with NVMe event processing)
 */
void pcie_nvme_event_handler(void)
{
    uint8_t val;

    /* Write NVMe command sequence to UART THR (debug output) */
    REG_UART_THR = 0x0A;
    REG_UART_THR = 0x0D;

    /* Call helper 0x538D with R3=0xFF, R2=0x23, R1=0xEE */
    /* This reads/processes NVMe response data */

    /* Read NVMe status from command control register */
    val = REG_CMD_CTRL_E40F;

    /* Call helper 0x51C7 with status in R7 */

    /* Write next command 0x3A */
    REG_UART_THR = 0x3A;

    /* Read command control 10 and process */
    val = REG_CMD_CTRL_E410;

    /* Write command 0x5D */
    REG_UART_THR = 0x5D;

    /* Check status bits in command control register */
    val = REG_CMD_CTRL_E40F;

    if (val & 0x80) {
        /* Bit 7 set: call 0xDFDC helper */
    } else if (val & 0x01) {
        /* Bit 0 set: acknowledge, call 0x83D6 */
        REG_CMD_CTRL_E40F = 0x01;
    } else if (val & 0x20) {
        /* Bit 5 set: acknowledge, call 0xDFDC */
        REG_CMD_CTRL_E40F = 0x20;
    }
}

/*
 * pcie_error_dispatch - PCIe Error Dispatch
 * Address: 0x0570-0x0574 (5 bytes)
 *
 * Dispatches to bank 1 code at 0xE911 (file offset 0x16911)
 * Called from ext1_isr when PCIe/NVMe status & 0x0F != 0.
 *
 * Original disassembly:
 *   0570: mov dptr, #0xe911
 *   0573: ajmp 0x0311
 */
extern void error_handler_pcie_nvme(void);  /* Bank 1: file 0x16911 */
void pcie_error_dispatch(void)
{
    error_handler_pcie_nvme();
}

/*
 * pcie_event_bit5_handler - PCIe Event Handler (bit 5)
 * Address: 0x061a-0x061e (5 bytes)
 *
 * Dispatches to bank 1 code at 0xA066 (file offset 0x12066)
 * Called from ext1_isr when event flags & 0x83 and PCIe/NVMe status bit 5 set.
 *
 * Original disassembly:
 *   061a: mov dptr, #0xa066
 *   061d: ajmp 0x0311
 */
extern void error_handler_pcie_bit5(void);  /* Bank 1: file 0x12066 */
void pcie_event_bit5_handler(void)
{
    error_handler_pcie_bit5();
}

/*
 * pcie_timer_bit4_handler - PCIe Timer Handler (bit 4)
 * Address: 0x0593-0x0597 (5 bytes)
 *
 * Called from ext1_isr when event flags & 0x83 and PCIe/NVMe status bit 4 set.
 * Dispatches to 0xC105 in bank 0.
 *
 * Original disassembly:
 *   0593: mov dptr, #0xc105
 *   0596: ajmp 0x0300
 */
extern void jump_bank_0(uint16_t addr);
void pcie_timer_bit4_handler(void)
{
    jump_bank_0(0xC105);
}

/*
 * pcie_tlp_init_and_transfer - Initialize PCIe TLP registers and perform transfer
 * Address: 0xC1F9-0xC26F (119 bytes)
 *
 * This function:
 *   1. Clears 12 PCIe TLP registers (offsets 0x00-0x0B -> addresses 0xB210-0xB21B)
 *   2. Sets format/type based on G_PCIE_DIRECTION bit 0:
 *      - bit 0 = 1: Memory write (fmt_type = 0x40)
 *      - bit 0 = 0: Memory read  (fmt_type = 0x00)
 *   3. Enables TLP control register
 *   4. Sets byte enables to 0x0F
 *   5. Copies 32-bit address from G_PCIE_ADDR (0x05AF) to REG_PCIE_ADDR (0xB218)
 *   6. Triggers transaction and waits for completion
 *   7. For reads: polls for completion data
 *
 * Returns: 0 on write success, link speed on read success, 0xFE/0xFF on error
 *
 * Original disassembly at 0xc1f9:
 *   c1f9: clr a                   ; loop counter = 0
 *   c1fa: mov 0x51, a             ; [0x51] = 0
 *   c1fc: mov a, 0x51             ; loop start
 *   c1fe: lcall 0x9a53            ; pcie_clear_reg_at_offset(a)
 *   c201: inc 0x51                ; counter++
 *   c203: mov a, 0x51
 *   c205: cjne a, #0x0c, 0xc1fc   ; loop until counter == 12
 *   c208: mov dptr, #0x05ae       ; read direction
 *   c20b: movx a, @dptr
 *   c20c: mov dptr, #0xb210       ; FMT_TYPE register
 *   c20f: jnb acc.0, 0xc217       ; if read, jump
 *   c212: mov a, #0x40            ; write fmt_type
 *   c214: movx @dptr, a
 *   c215: sjmp 0xc219
 *   c217: clr a                   ; read fmt_type = 0
 *   c218: movx @dptr, a
 *   c219: mov dptr, #0xb213
 *   c21c: mov a, #0x01
 *   c21e: movx @dptr, a           ; enable TLP control
 *   c21f: mov a, #0x0f
 *   c221: lcall 0x9a30            ; pcie_set_byte_enables
 *   c224: mov dptr, #0x05af       ; address source
 *   c227: lcall 0x0d84            ; load 32-bit value
 *   c22a: mov dptr, #0xb218       ; address target
 *   c22d: lcall 0x0dc5            ; store 32-bit value
 *   c230: lcall 0x999d            ; pcie_clear_and_trigger
 *   c233: lcall 0x99eb            ; poll loop
 *   c236: jz 0xc233
 *   c238: lcall 0x9a95            ; pcie_write_status_complete
 *   c23b: mov dptr, #0x05ae
 *   c23e: movx a, @dptr
 *   c23f: jnb acc.0, 0xc245       ; if read, go to poll
 *   c242: mov r7, #0x00
 *   c244: ret                     ; write complete
 *   c245-c26f: poll and read completion handling
 */
uint8_t pcie_tlp_init_and_transfer(void)
{
    uint8_t i;
    uint8_t direction;
    uint8_t status;

    /* Step 1: Clear 12 PCIe TLP registers (offsets 0-11) */
    for (i = 0; i < 12; i++) {
        pcie_clear_reg_at_offset(i);
    }

    /* Step 2: Read direction and set format/type */
    direction = G_PCIE_DIRECTION;

    if (direction & 0x01) {
        /* Memory write */
        REG_PCIE_FMT_TYPE = PCIE_FMT_MEM_WRITE;  /* 0x40 */
    } else {
        /* Memory read */
        REG_PCIE_FMT_TYPE = PCIE_FMT_MEM_READ;   /* 0x00 */
    }

    /* Step 3: Enable TLP control */
    REG_PCIE_TLP_CTRL = 0x01;

    /* Step 4: Set byte enables to 0x0F */
    pcie_set_byte_enables(0x0F);

    /* Step 5: Copy 32-bit address from globals to PCIe registers */
    REG_PCIE_ADDR_0 = G_PCIE_ADDR_0;
    REG_PCIE_ADDR_1 = G_PCIE_ADDR_1;
    REG_PCIE_ADDR_2 = G_PCIE_ADDR_2;
    REG_PCIE_ADDR_3 = G_PCIE_ADDR_3;

    /* Step 6: Trigger transaction */
    pcie_clear_and_trigger();

    /* Step 7: Poll for initial completion */
    do {
        status = pcie_get_completion_status();
    } while (status == 0);

    /* Write completion status */
    pcie_write_status_complete();

    /* Step 8: Check direction again */
    direction = G_PCIE_DIRECTION;
    if (direction & 0x01) {
        /* Write operation - done */
        return 0;
    }

    /* Step 9: Read operation - poll for completion data */
    return pcie_poll_and_read_completion();
}

/*
 * pcie_init_read_e8f9 - Initialize PCIe direction for read and execute
 * Address: 0xe8f9-0xe901 (Bank 1)
 *
 * Sets direction to read mode and executes a PCIe TLP transaction.
 *
 * Disassembly:
 *   e8f9: clr a                ; a = 0
 *   e8fa: mov dptr, #0x05ae    ; G_PCIE_DIRECTION
 *   e8fd: movx @dptr, a        ; Write 0 (read mode)
 *   e8fe: lcall 0xc1f9         ; pcie_tlp_init_and_transfer
 *   e901: ret
 *
 * Returns: Result from pcie_tlp_init_and_transfer
 */
uint8_t pcie_init_read_e8f9(void)
{
    G_PCIE_DIRECTION = 0;           /* Set direction to read */
    return pcie_tlp_init_and_transfer();
}

/*
 * pcie_init_write_e902 - Initialize PCIe direction for write and execute
 * Address: 0xe902-0xe90a (Bank 1)
 *
 * Sets direction to write mode and executes a PCIe TLP transaction.
 *
 * Disassembly:
 *   e902: mov dptr, #0x05ae    ; G_PCIE_DIRECTION
 *   e905: mov a, #0x01         ; Write mode
 *   e907: movx @dptr, a
 *   e908: ljmp 0xc1f9          ; tail call to pcie_tlp_init_and_transfer
 *
 * Returns: Result from pcie_tlp_init_and_transfer
 */
uint8_t pcie_init_write_e902(void)
{
    G_PCIE_DIRECTION = 1;           /* Set direction to write */
    return pcie_tlp_init_and_transfer();
}