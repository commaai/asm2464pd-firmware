/*
 * ASM2464PD Firmware - DMA Driver
 *
 * DMA engine control for USB4/Thunderbolt to NVMe bridge.
 * Handles DMA transfers between USB, NVMe, and internal buffers.
 *
 * ============================================================================
 * ARCHITECTURE OVERVIEW
 * ============================================================================
 * The ASM2464PD has a sophisticated DMA engine that handles data movement
 * between multiple endpoints:
 *
 *   USB Host <---> USB Buffer <---> DMA Engine <---> NVMe Buffer <---> NVMe SSD
 *                      |                |
 *                      v                v
 *                 XRAM Buffers    SCSI/Mass Storage
 *
 * The DMA engine supports multiple channels and can perform:
 * - USB to Buffer transfers (host writes)
 * - Buffer to USB transfers (host reads)
 * - Buffer to NVMe transfers (SSD writes)
 * - NVMe to Buffer transfers (SSD reads)
 *
 * ============================================================================
 * REGISTER MAP
 * ============================================================================
 * DMA Engine Core (0xC8B0-0xC8DF):
 * 0xC8B0: REG_DMA_MODE          - DMA mode configuration
 * 0xC8B2: REG_DMA_CHAN_AUX      - Channel auxiliary config (2 bytes)
 * 0xC8B4-0xC8B5: Transfer count
 * 0xC8B6: REG_DMA_CHAN_CTRL2    - Channel control 2
 *         Bit 0: Start/busy
 *         Bit 1: Direction
 *         Bit 2: Enable
 *         Bit 7: Active
 * 0xC8B7: REG_DMA_CHAN_STATUS2  - Channel status 2
 * 0xC8B8: REG_DMA_TRIGGER       - Trigger register (poll bit 0)
 * 0xC8D4: REG_DMA_CONFIG        - Global DMA configuration
 * 0xC8D6: REG_DMA_STATUS        - DMA status
 *         Bit 2: Done flag
 *         Bit 3: Error flag
 * 0xC8D8: REG_DMA_STATUS2       - DMA status 2
 *
 * SCSI/Mass Storage DMA (0xCE40-0xCE6F):
 * 0xCE40: REG_SCSI_DMA_PARAM0   - SCSI parameter 0
 * 0xCE41: REG_SCSI_DMA_PARAM1   - SCSI parameter 1
 * 0xCE42: REG_SCSI_DMA_PARAM2   - SCSI parameter 2
 * 0xCE43: REG_SCSI_DMA_PARAM3   - SCSI parameter 3
 * 0xCE5C: REG_SCSI_DMA_COMPL    - Completion status
 *         Bit 0: Mode 0 complete
 *         Bit 1: Mode 0x10 complete
 * 0xCE66: REG_SCSI_DMA_TAG_COUNT - Tag count (5-bit, 0-31)
 * 0xCE67: REG_SCSI_DMA_QUEUE_STAT - Queue status (4-bit, 0-15)
 *
 * ============================================================================
 * WORK AREA GLOBALS (0x0200-0x07FF)
 * ============================================================================
 * 0x0203: G_DMA_MODE_SELECT     - Current DMA mode
 * 0x020D: G_DMA_PARAM1          - Transfer parameter 1
 * 0x020E: G_DMA_PARAM2          - Transfer parameter 2
 * 0x021A-0x021B: G_BUF_BASE     - Buffer base address (16-bit)
 * 0x0472-0x0473: G_DMA_LOAD_PARAM - Load parameters
 * 0x0564: G_EP_QUEUE_CTRL       - Endpoint queue control
 * 0x0565: G_EP_QUEUE_STATUS     - Endpoint queue status
 * 0x07E5: G_TRANSFER_ACTIVE     - Transfer active flag
 * 0x0AA3-0x0AA4: G_STATE_COUNTER - 16-bit state counter
 *
 * ============================================================================
 * TRANSFER SEQUENCE
 * ============================================================================
 * 1. Set transfer parameters in work area (G_DMA_MODE_SELECT, etc)
 * 2. Configure channel via dma_config_channel()
 * 3. Set buffer pointers and length
 * 4. Trigger transfer via REG_DMA_TRIGGER (write 0x01)
 * 5. Poll REG_DMA_TRIGGER bit 0 until clear
 * 6. Check REG_DMA_STATUS for errors
 * 7. Clear status via dma_clear_status()
 *
 * ============================================================================
 * IMPLEMENTATION STATUS
 * ============================================================================
 * Core functions:
 * [x] dma_clear_status (0x16f3)       - Clear DMA status flags
 * [x] dma_set_scsi_param3 (0x1709)    - Set SCSI param 3 to 0xFF
 * [x] dma_set_scsi_param1 (0x1713)    - Set SCSI param 1 to 0xFF
 * [x] dma_reg_wait_bit (0x16ff)       - Wait for register bit
 * [x] dma_load_transfer_params (0x171d) - Load transfer params
 * [x] dma_config_channel (0x4a57)     - Configure DMA channel
 * [x] dma_setup_transfer (0x523c)     - Setup transfer parameters
 * [x] dma_check_scsi_status (0x5260)  - Check SCSI completion
 * [x] dma_clear_state_counters (0x1795) - Clear state counters
 * [x] dma_init_ep_queue (0x17a9)      - Initialize endpoint queue
 * [x] scsi_get_tag_count_status (0x17b5) - Get tag count
 * [x] dma_check_state_counter (0x172c) - Check state counter threshold
 * [x] scsi_get_queue_status (0x17c1)  - Get queue status
 * [x] dma_shift_and_check (0x17cd)    - Shift and compare
 * [x] dma_start_transfer (0x4a94)     - Start DMA transfer with polling
 * [x] dma_set_error_flag (0x1787)     - Set error flag at 0x06E6
 * [x] dma_get_config_offset_05a8 (0x1743) - Get config offset in 0x05XX
 * [x] dma_calc_offset_0059 (0x1752)   - Calculate address + 0x59
 * [x] dma_init_channel_b8 (0x175d)    - Initialize channel with 0xB8
 * [x] dma_calc_addr_0478 (0x176b)     - Calculate index*4 + 0x0478
 * [x] dma_calc_addr_0479 (0x1779)     - Calculate index*4 + 0x0479
 * [x] dma_shift_rrc2_mask (0x17f3)    - Shift and mask utility
 * [x] dma_calc_addr_00c2 (0x179d)     - Calculate IDATA offset + 0xC2
 * [x] dma_store_to_0a7d (0x180d)      - Store to work area 0x0A7D
 * [x] dma_clear_dword_at (0x173b)     - Clear 32-bit value
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"
#include "utils.h"
#include "drivers/pcie.h"
#include "drivers/usb.h"

/* External protocol/state functions from app layer */
extern void helper_dd42(uint8_t param);
extern void system_state_update(void);
extern uint8_t pcie_read_status_a334(void);
extern void scsi_decrement_pending(void);
extern void scsi_csw_write_residue(void);
extern void scsi_buffer_threshold_config(uint8_t slot);
extern void helper_0421(uint8_t slot);

/* Forward declarations */
void dma_set_scsi_param3(void);
void dma_set_scsi_param1(void);
void transfer_func_1633(uint16_t addr);

/*
 * dma_copy_idata_6b_to_6f - Copy 32-bit value from IDATA[0x6B] to IDATA[0x6F]
 * Address: 0x1bcb-0x1bd4 (10 bytes)
 *
 * Copies a 4-byte value from IDATA[0x6B..0x6E] to IDATA[0x6F..0x72].
 * Used for saving transfer state parameters.
 *
 * Original disassembly:
 *   1bcb: mov r0, #0x6b
 *   1bcd: lcall 0x0d78     ; idata_load_dword -> R4-R7
 *   1bd0: mov r0, #0x6f
 *   1bd2: ljmp 0x0db9      ; idata_store_dword from R4-R7
 */
static void dma_copy_idata_6b_to_6f(void)
{
    uint32_t val;
    val = idata_load_dword((__idata uint8_t *)0x6B);
    idata_store_dword((__idata uint8_t *)0x6F, val);
}

/*
 * dma_clear_status - Clear DMA status flags
 * Address: 0x16f3-0x16fe (12 bytes)
 *
 * Clears bits 3 and 2 of DMA status register at 0xC8D6.
 * Bit 3 (0x08) and bit 2 (0x04) are error/done flags.
 *
 * Original disassembly:
 *   16f3: mov dptr, #0xc8d6
 *   16f6: movx a, @dptr
 *   16f7: anl a, #0xf7      ; clear bit 3
 *   16f9: movx @dptr, a
 *   16fa: movx a, @dptr
 *   16fb: anl a, #0xfb      ; clear bit 2
 *   16fd: movx @dptr, a
 *   16fe: ret
 */
void dma_clear_status(void)
{
    uint8_t val;

    val = REG_DMA_STATUS;
    val &= 0xF7;  /* Clear bit 3 */
    REG_DMA_STATUS = val;

    val = REG_DMA_STATUS;
    val &= 0xFB;  /* Clear bit 2 */
    REG_DMA_STATUS = val;
}

/*
 * dma_set_scsi_param3 - Set SCSI DMA parameter 3 to 0xFF
 * Address: 0x1709-0x1712 (10 bytes)
 *
 * Writes 0xFF to SCSI DMA parameter 3 register at 0xCE43,
 * then sets DPTR to 0xCE42 for subsequent operation.
 *
 * Original disassembly:
 *   1709: mov dptr, #0xce43
 *   170c: mov a, #0xff
 *   170e: movx @dptr, a
 *   170f: mov dptr, #0xce42
 *   1712: ret
 */
void dma_set_scsi_param3(void)
{
    REG_SCSI_DMA_PARAM3 = 0xFF;
    /* Note: Original also sets DPTR to 0xCE42 before return
     * for caller's use, but in C this is handled differently */
}

/*
 * dma_set_scsi_param1 - Set SCSI DMA parameter 1 to 0xFF
 * Address: 0x1713-0x171c (10 bytes)
 *
 * Writes 0xFF to SCSI DMA parameter 1 register at 0xCE41,
 * then sets DPTR to 0xCE40 for subsequent operation.
 *
 * Original disassembly:
 *   1713: mov dptr, #0xce41
 *   1716: mov a, #0xff
 *   1718: movx @dptr, a
 *   1719: mov dptr, #0xce40
 *   171c: ret
 */
void dma_set_scsi_param1(void)
{
    REG_SCSI_DMA_PARAM1 = 0xFF;
    /* Note: Original also sets DPTR to 0xCE40 before return */
}

/*
 * dma_reg_wait_bit - Wait for DMA register bit to be set
 * Address: 0x16ff-0x1708 (10 bytes)
 *
 * Reads value from DPTR, stores in R7, then calls reg_wait_bit_set
 * at 0x0ddd with address 0x045E and the read value.
 *
 * Original disassembly:
 *   16ff: movx a, @dptr     ; read value from caller's DPTR
 *   1700: mov r7, a         ; save to R7
 *   1701: mov dptr, #0x045e
 *   1704: lcall 0x0ddd      ; reg_wait_bit_set
 *   1707: mov a, r7
 *   1708: ret
 */
uint8_t dma_reg_wait_bit(__xdata uint8_t *ptr)
{
    uint8_t val;

    val = *ptr;
    /* Load triple from G_REG_WAIT_BIT (0x045E) - side effect */
    /* This reads 3 bytes from 0x045E..0x0460 but discards the result */
    (void)xdata_load_triple((__xdata uint8_t *)&G_REG_WAIT_BIT);
    return val;
}

/*
 * dma_load_transfer_params - Load DMA transfer parameters from XDATA
 * Address: 0x171d-0x172b (15 bytes)
 *
 * Loads parameters from 0x0472-0x0473 and calls flash_func_0c0f.
 *
 * Original disassembly:
 *   171d: mov dptr, #0x0472
 *   1720: movx a, @dptr     ; read param from 0x0472
 *   1721: mov r6, a
 *   1722: inc dptr
 *   1723: movx a, @dptr     ; read param from 0x0473
 *   1724: mov r7, a
 *   1725: ljmp 0x0c0f       ; flash_func_0c0f(0, R3, R6, R7)
 */
void dma_load_transfer_params(void)
{
    uint8_t param1, param2;

    /* Read transfer parameters from work area */
    param1 = G_DMA_LOAD_PARAM1;
    param2 = G_DMA_LOAD_PARAM2;

    /* Tail-calls to flash_div16 at 0x0c0f with R6=param1, R7=param2 */
    /* The division R4:R6 / R5 where R4=0 (cleared) */
    /* Result: quotient in R7, remainder in R5 */
    /* This is effectively a 16-bit division used for block calculations */
    (void)param1;
    (void)param2;
    /* Note: Actual division result used by caller, not stored here */
}

/*
 * dma_config_channel - Configure DMA channel with mode select
 * Address: 0x4a57-0x4a93 (61 bytes)
 *
 * Configures DMA channel based on R1 value:
 * - If R1 < 1: Use register 0xC8D8
 * - If R1 >= 1: Use register 0xC8D6
 * Then configures 0xC8B6 and 0xC8B7 registers.
 *
 * Original disassembly:
 *   4a57: mov r1, 0x07        ; R1 = R7
 *   4a59: mov a, r1
 *   4a5a: setb c              ; set carry
 *   4a5b: subb a, #0x01       ; compare with 1
 *   4a5d: jc 0x4a6a           ; if R1 < 1, jump
 *   ... (branches based on R1 value)
 *   4a78: mov dptr, #0xc8b7
 *   4a7b: clr a
 *   4a7c: movx @dptr, a       ; XDATA[0xC8B7] = 0
 *   4a7d: mov dptr, #0xc8b6
 *   4a80-4a93: Configure 0xC8B6 bits
 */
void dma_config_channel(uint8_t channel, uint8_t r4_param)
{
    uint8_t val;
    uint8_t mode;

    /* Suppress unused parameter warning */
    (void)r4_param;

    /* Calculate mode based on channel */
    if (channel >= 1) {
        mode = (channel - 2) * 2;  /* (channel - 2) << 1 */
        /* Configure DMA status register */
        val = REG_DMA_STATUS;
        val = (val & 0xFD) | mode;
        REG_DMA_STATUS = val;
    } else {
        mode = channel * 2;  /* channel << 1 */
        /* Configure DMA status 2 register */
        val = REG_DMA_STATUS2;
        val = (val & 0xFD) | mode;
        REG_DMA_STATUS2 = val;
    }

    /* Clear channel status 2 */
    REG_DMA_CHAN_STATUS2 = 0;

    /* Configure channel control 2: Set bit 2, clear bit 0, clear bit 1, set bit 7 */
    val = REG_DMA_CHAN_CTRL2;
    val = (val & 0xFB) | 0x04;  /* Set bit 2 */
    REG_DMA_CHAN_CTRL2 = val;

    val = REG_DMA_CHAN_CTRL2;
    val &= 0xFE;  /* Clear bit 0 */
    REG_DMA_CHAN_CTRL2 = val;

    val = REG_DMA_CHAN_CTRL2;
    val &= 0xFD;  /* Clear bit 1 */
    REG_DMA_CHAN_CTRL2 = val;

    val = REG_DMA_CHAN_CTRL2;
    val = (val & 0x7F) | 0x80;  /* Set bit 7 */
    REG_DMA_CHAN_CTRL2 = val;
}

/*
 * dma_setup_transfer - Setup DMA transfer parameters
 * Address: 0x523c-0x525f (36 bytes)
 *
 * Writes transfer parameters to DMA control registers and sets flag.
 *
 * Original disassembly:
 *   523c: mov dptr, #0x0203
 *   523f: mov a, r7
 *   5240: movx @dptr, a       ; XDATA[0x0203] = R7
 *   5241: mov dptr, #0x020d
 *   5244: mov a, r5
 *   5245: movx @dptr, a       ; XDATA[0x020D] = R5
 *   5246: inc dptr
 *   5247: mov a, r3
 *   5248: movx @dptr, a       ; XDATA[0x020E] = R3
 *   5249: mov dptr, #0x07e5
 *   524c: mov a, #0x01
 *   524e: movx @dptr, a       ; XDATA[0x07E5] = 1
 *   524f: mov dptr, #0x9000
 *   5252: movx a, @dptr
 *   5253: jb 0xe0.0, 0x525f   ; if bit 0 set, return
 *   5256: mov dptr, #0xd80c
 *   5259: mov a, #0x01
 *   525b: movx @dptr, a       ; XDATA[0xD80C] = 1
 *   525c: lcall 0x1bcb
 *   525f: ret
 */
void dma_setup_transfer(uint8_t r7_mode, uint8_t r5_param, uint8_t r3_param)
{
    uint8_t status;

    /* Write transfer parameters to work area */
    G_DMA_MODE_SELECT = r7_mode;
    G_DMA_PARAM1 = r5_param;
    G_DMA_PARAM2 = r3_param;

    /* Set transfer active flag in work area */
    G_TRANSFER_ACTIVE = 1;

    /* Check USB status register */
    status = REG_USB_STATUS;
    if (!(status & 0x01)) {
        /* Start transfer via buffer control */
        G_BUF_XFER_START = 1;
        /* Copy IDATA[0x6B..0x6E] to IDATA[0x6F..0x72] */
        dma_copy_idata_6b_to_6f();
    }
}

/*
 * dma_check_scsi_status - Check SCSI DMA completion status
 * Address: 0x5260-0x5283 (36 bytes)
 *
 * Checks SCSI DMA status at 0xCE5C and calls appropriate handler.
 * Returns 1 if operation succeeded, 0 if failed.
 *
 * Original disassembly:
 *   5260: mov a, r7
 *   5261: jnz 0x526f          ; if R7 != 0, skip
 *   5263: mov dptr, #0xce5c
 *   5266: movx a, @dptr
 *   5267: jnb 0xe0.0, 0x526f  ; if bit 0 clear, skip
 *   526a: lcall 0x1709        ; dma_set_scsi_param3
 *   526d: sjmp 0x527d
 *   526f: mov a, r7
 *   5270: cjne a, #0x10, 0x5281  ; if R7 != 0x10, return 0
 *   5273: mov dptr, #0xce5c
 *   5276: movx a, @dptr
 *   5277: jnb 0xe0.1, 0x5281  ; if bit 1 clear, return 0
 *   527a: lcall 0x1713        ; dma_set_scsi_param1
 *   527d: movx @dptr, a       ; write to current DPTR
 *   527e: mov r7, #0x01       ; return 1
 *   5280: ret
 *   5281: mov r7, #0x00       ; return 0
 *   5283: ret
 */
uint8_t dma_check_scsi_status(uint8_t mode)
{
    uint8_t status;

    if (mode == 0) {
        /* Check bit 0 of SCSI completion status */
        status = REG_SCSI_DMA_COMPL;
        if (status & SCSI_DMA_COMPL_MODE0) {
            dma_set_scsi_param3();
            return 1;
        }
    } else if (mode == 0x10) {
        /* Check bit 1 of SCSI completion status */
        status = REG_SCSI_DMA_COMPL;
        if (status & SCSI_DMA_COMPL_MODE10) {
            dma_set_scsi_param1();
            return 1;
        }
    }

    return 0;
}

/*
 * dma_clear_state_counters - Clear state counter registers
 * Address: 0x1795-0x179c (8 bytes)
 *
 * Clears 16-bit state counter at 0x0AA3-0x0AA4 to zero.
 *
 * Original disassembly:
 *   1795: clr a
 *   1796: mov dptr, #0x0aa3
 *   1799: movx @dptr, a      ; XDATA[0x0AA3] = 0
 *   179a: inc dptr
 *   179b: movx @dptr, a      ; XDATA[0x0AA4] = 0
 *   179c: ret
 */
void dma_clear_state_counters(void)
{
    /* Clear 16-bit state counter in work area */
    G_STATE_COUNTER_HI = 0;
    G_STATE_COUNTER_LO = 0;
}

/*
 * dma_init_ep_queue - Initialize endpoint queue
 * Address: 0x17a9-0x17b4 (12 bytes)
 *
 * Sets endpoint queue control to 0x08 and status to 0.
 *
 * Original disassembly:
 *   17a9: clr a
 *   17aa: mov dptr, #0x0565
 *   17ad: movx @dptr, a      ; XDATA[0x0565] = 0
 *   17ae: mov dptr, #0x0564
 *   17b1: mov a, #0x08
 *   17b3: movx @dptr, a      ; XDATA[0x0564] = 0x08
 *   17b4: ret
 */
void dma_init_ep_queue(void)
{
    /* Initialize endpoint queue in work area */
    G_EP_QUEUE_STATUS = 0;
    G_EP_QUEUE_CTRL = 0x08;
}

/*
 * scsi_get_tag_count_status - Get SCSI tag count and check threshold
 * Address: 0x17b5-0x17c0 (12 bytes)
 *
 * Reads tag count from 0xCE66, masks to 5 bits, stores to IDATA 0x40,
 * and returns carry set if count >= 16.
 *
 * Original disassembly:
 *   17b5: mov dptr, #0xce66
 *   17b8: movx a, @dptr      ; read tag count
 *   17b9: anl a, #0x1f       ; mask to 5 bits (0-31)
 *   17bb: mov 0x40, a        ; store to IDATA[0x40]
 *   17bd: clr c
 *   17be: subb a, #0x10      ; compare with 16
 *   17c0: ret                ; carry set if count < 16
 */
uint8_t scsi_get_tag_count_status(void)
{
    uint8_t count;

    count = REG_SCSI_DMA_TAG_COUNT & SCSI_DMA_TAG_MASK;
    I_WORK_40 = count;

    /* Return 1 if count >= 16, 0 otherwise */
    return (count >= 0x10) ? 1 : 0;
}

/*
 * dma_check_state_counter - Check if state counter reached threshold
 * Address: 0x172c-0x173a (15 bytes)
 *
 * Reads 16-bit state counter from 0x0AA3-0x0AA4 and compares with 40.
 * Returns with carry set if counter < 40.
 *
 * Original disassembly:
 *   172c: mov dptr, #0x0aa3
 *   172f: movx a, @dptr       ; R4 = [0x0AA3]
 *   1730: mov r4, a
 *   1731: inc dptr
 *   1732: movx a, @dptr       ; R5 = [0x0AA4]
 *   1733: mov r5, a
 *   1734: clr c
 *   1735: subb a, #0x28       ; compare low with 40
 *   1737: mov a, r4
 *   1738: subb a, #0x00       ; compare high with borrow
 *   173a: ret
 */
uint8_t dma_check_state_counter(void)
{
    uint16_t counter;

    counter = ((uint16_t)G_STATE_COUNTER_HI << 8) | G_STATE_COUNTER_LO;

    /* Return 1 if counter >= 40, 0 otherwise */
    return (counter >= 40) ? 1 : 0;
}

/*
 * dma_clear_dword - Clear 32-bit value at DPTR
 * Address: 0x173b-0x1742 (8 bytes)
 *
 * Clears R4-R7 to 0 and calls xdata_store_dword.
 *
 * Original disassembly:
 *   173b: clr a
 *   173c: mov r7, a
 *   173d: mov r6, a
 *   173e: mov r5, a
 *   173f: mov r4, a
 *   1740: ljmp 0x0dc5         ; xdata_store_dword
 */
void dma_clear_dword(__xdata uint8_t *ptr)
{
    ptr[0] = 0;
    ptr[1] = 0;
    ptr[2] = 0;
    ptr[3] = 0;
}

/*
 * scsi_get_queue_status - Get SCSI queue status and check threshold
 * Address: 0x17c1-0x17cc (12 bytes)
 *
 * Reads queue status from 0xCE67, masks to 4 bits, stores to IDATA 0x40,
 * and returns carry set if count >= 8.
 *
 * Original disassembly:
 *   17c1: mov dptr, #0xce67
 *   17c4: movx a, @dptr       ; read queue status
 *   17c5: anl a, #0x0f        ; mask to 4 bits (0-15)
 *   17c7: mov 0x40, a         ; store to IDATA[0x40]
 *   17c9: clr c
 *   17ca: subb a, #0x08       ; compare with 8
 *   17cc: ret                 ; carry set if count < 8
 */
uint8_t scsi_get_queue_status(void)
{
    uint8_t status;

    status = REG_SCSI_DMA_QUEUE_STAT & SCSI_DMA_QUEUE_MASK;
    I_WORK_40 = status;

    /* Return 1 if status >= 8, 0 otherwise */
    return (status >= 0x08) ? 1 : 0;
}

/*
 * dma_shift_and_check - Right shift by 3 and compare with 3
 * Address: 0x17cd-0x17d7 (11 bytes)
 *
 * Shifts input right 3 bits, masks to 5 bits, compares with 3.
 * Returns shifted value, carry set if value > 3.
 *
 * Original disassembly:
 *   17cd: rrc a
 *   17ce: rrc a
 *   17cf: rrc a               ; A >>= 3
 *   17d0: anl a, #0x1f        ; mask to 5 bits
 *   17d2: mov r7, a
 *   17d3: clr c
 *   17d4: mov a, #0x03
 *   17d6: subb a, r7          ; compare: 3 - R7
 *   17d7: ret                 ; carry set if R7 > 3
 */
uint8_t dma_shift_and_check(uint8_t val)
{
    return (val >> 3) & 0x1F;
}

/*
 * dma_start_transfer - Start DMA transfer with channel parameters
 * Address: 0x4a94-0x4abe (43 bytes)
 *
 * Sets up DMA channel auxiliary registers, triggers the transfer,
 * polls for completion, then clears the busy flag.
 *
 * Parameters:
 *   r4: Channel auxiliary byte 0
 *   r5: Channel auxiliary byte 1 (used to calculate count high)
 *   r2: Transfer count high
 *   r3: Transfer count low
 *
 * Original disassembly:
 *   4a94: mov r7, 0x05         ; R7 = R5
 *   4a96: mov dptr, #0xc8b2    ; DMA channel aux register
 *   4a99: mov a, r4
 *   4a9a: movx @dptr, a        ; XDATA[0xC8B2] = R4
 *   4a9b: inc dptr
 *   4a9c: mov a, r7
 *   4a9d: movx @dptr, a        ; XDATA[0xC8B3] = R7
 *   4a9e: mov a, r3
 *   4a9f: add a, #0xff         ; count_lo - 1
 *   4aa1: mov r6, a
 *   4aa2: mov a, r2
 *   4aa3: addc a, #0xff        ; count_hi - 1 + carry
 *   4aa5: inc dptr
 *   4aa6: movx @dptr, a        ; XDATA[0xC8B4] = count_hi
 *   4aa7: inc dptr
 *   4aa8: xch a, r6
 *   4aa9: movx @dptr, a        ; XDATA[0xC8B5] = count_lo
 *   4aaa: mov dptr, #0xc8b8    ; DMA trigger register
 *   4aad: mov a, #0x01
 *   4aaf: movx @dptr, a        ; trigger transfer
 *   4ab0: mov dptr, #0xc8b8    ; poll loop start
 *   4ab3: movx a, @dptr
 *   4ab4: jb 0xe0.0, 0x4ab0    ; wait while bit 0 set (busy)
 *   4ab7: mov dptr, #0xc8b6    ; channel control 2
 *   4aba: movx a, @dptr
 *   4abb: anl a, #0x7f         ; clear bit 7 (active)
 *   4abd: movx @dptr, a
 *   4abe: ret
 */
void dma_start_transfer(uint8_t aux0, uint8_t aux1, uint8_t count_hi, uint8_t count_lo)
{
    uint8_t val;
    uint16_t count;

    /* Set channel auxiliary registers */
    REG_DMA_CHAN_AUX = aux0;
    REG_DMA_CHAN_AUX1 = aux1;

    /* Calculate count - 1 (16-bit subtraction) */
    count = ((uint16_t)count_hi << 8) | count_lo;
    count -= 1;

    /* Write transfer count */
    REG_DMA_XFER_CNT_HI = (uint8_t)(count >> 8);
    REG_DMA_XFER_CNT_LO = (uint8_t)(count & 0xFF);

    /* Trigger DMA transfer */
    REG_DMA_TRIGGER = DMA_TRIGGER_START;

    /* Poll for completion (wait while bit 0 is set) */
    while (REG_DMA_TRIGGER & DMA_TRIGGER_START) {
        /* Busy wait */
    }

    /* Clear active bit (bit 7) in channel control 2 */
    val = REG_DMA_CHAN_CTRL2;
    val &= ~DMA_CHAN_CTRL2_ACTIVE;
    REG_DMA_CHAN_CTRL2 = val;
}

/*
 * dma_set_error_flag - Set error flag at 0x06E6 to 1
 * Address: 0x1787-0x178d (7 bytes)
 *
 * Sets the processing complete/error flag in transfer work area.
 *
 * Original disassembly:
 *   1787: mov dptr, #0x06e6
 *   178a: mov a, #0x01
 *   178c: movx @dptr, a
 *   178d: ret
 */
void dma_set_error_flag(void)
{
    G_STATE_FLAG_06E6 = 1;
}

/*
 * dma_get_config_offset_05xx - Get config offset in 0x05XX region
 * Address: 0x1743-0x1751 (15 bytes)
 *
 * Reads value from 0x0464, adds 0xA8, returns pointer to 0x05XX region.
 *
 * Original disassembly:
 *   1743: mov dptr, #0x0464
 *   1746: movx a, @dptr        ; A = XDATA[0x0464]
 *   1747: add a, #0xa8         ; A = A + 0xA8
 *   1749: mov 0x82, a          ; DPL = A
 *   174b: clr a
 *   174c: addc a, #0x05        ; DPH = 0x05 + carry
 *   174e: mov 0x83, a
 *   1750: movx a, @dptr        ; read from result address
 *   1751: ret
 */
uint8_t dma_get_config_offset_05a8(void)
{
    uint8_t val = G_SYS_STATUS_PRIMARY;
    uint16_t addr = 0x0500 + val + 0xA8;
    return *(__xdata uint8_t *)addr;
}

/*
 * dma_calc_offset_0059 - Calculate address in 0x00XX region
 * Address: 0x1752-0x175c (11 bytes)
 *
 * Adds 0x59 to input and returns pointer.
 *
 * Original disassembly:
 *   1752: mov a, #0x59
 *   1754: add a, r7            ; A = 0x59 + R7
 *   1755: mov 0x82, a          ; DPL = A
 *   1757: clr a
 *   1758: addc a, #0x00        ; DPH = carry
 *   175a: mov 0x83, a
 *   175c: ret
 */
__xdata uint8_t *dma_calc_offset_0059(uint8_t offset)
{
    return (__xdata uint8_t *)(0x0059 + offset);
}

/*
 * dma_init_channel_b8 - Initialize DMA channel with R7=0x04, R4=0xB8
 * Address: 0x175d-0x176a (14 bytes)
 *
 * Calls dma_config_channel(0x04, 0xB8) then returns R3=0x80, R2=0x00, R4=0xBC.
 *
 * Original disassembly:
 *   175d: mov r2, #0x04
 *   175f: mov r4, #0xb8
 *   1761: lcall 0x4a57         ; dma_config_channel
 *   1764: mov r3, #0x80
 *   1766: mov r2, #0x00
 *   1768: mov r4, #0xbc
 *   176a: ret
 */
void dma_init_channel_b8(void)
{
    dma_config_channel(0x04, 0xB8);
    /* Note: Original returns R3=0x80, R2=0x00, R4=0xBC for caller use */
}

/*
 * dma_calc_addr_0478 - Calculate address from A*4 + 0x0478
 * Address: 0x176b-0x1778 (14 bytes)
 *
 * Multiplies input by 4 (add a, acc twice) and adds 0x0478.
 *
 * Original disassembly:
 *   176b: add a, 0xe0          ; A = A * 2 (add A to itself)
 *   176d: add a, 0xe0          ; A = A * 2 again = A * 4
 *   176f: add a, #0x78         ; A = A*4 + 0x78
 *   1771: mov 0x82, a          ; DPL = A
 *   1773: clr a
 *   1774: addc a, #0x04        ; DPH = 0x04 + carry
 *   1776: mov 0x83, a
 *   1778: ret
 */
__xdata uint8_t *dma_calc_addr_0478(uint8_t index)
{
    uint16_t addr = 0x0478 + ((uint16_t)index * 4);
    return (__xdata uint8_t *)addr;
}

/*
 * dma_calc_addr_0479 - Calculate address from A*4 + 0x0479
 * Address: 0x1779-0x1786 (14 bytes)
 *
 * Same as above but base is 0x0479.
 *
 * Original disassembly:
 *   1779: add a, 0xe0          ; A = A * 2
 *   177b: add a, 0xe0          ; A = A * 4
 *   177d: add a, #0x79         ; A = A*4 + 0x79
 *   177f: mov 0x82, a          ; DPL
 *   1781: clr a
 *   1782: addc a, #0x04        ; DPH = 0x04 + carry
 *   1784: mov 0x83, a
 *   1786: ret
 */
__xdata uint8_t *dma_calc_addr_0479(uint8_t index)
{
    uint16_t addr = 0x0479 + ((uint16_t)index * 4);
    return (__xdata uint8_t *)addr;
}

/*
 * dma_shift_rrc2_mask - Shift right twice via carry and mask to 6 bits
 * Address: 0x17f3-0x17fc (10 bytes)
 *
 * Performs two RRC operations (rotate right through carry) and masks.
 * Then ORs with 0x20 to set bit 5.
 *
 * Original disassembly:
 *   17f3: rrc a
 *   17f4: rrc a                 ; A >>= 2 (with carry rotation)
 *   17f5: anl a, #0x3f          ; mask to 6 bits
 *   17f7: mov r7, a
 *   17f8: mov a, r7
 *   17f9: orl a, #0x20          ; set bit 5
 *   17fb: mov r7, a
 *   17fc: ret
 */
uint8_t dma_shift_rrc2_mask(uint8_t val)
{
    uint8_t result;

    /* Shift right by 2 (approximating RRC behavior without carry) */
    result = (val >> 2) & 0x3F;

    /* OR with 0x20 to set bit 5 */
    return result | 0x20;
}

/*
 * dma_calc_addr_with_carry - Calculate 16-bit address with carry
 * Address: 0x178e-0x1794 (7 bytes)
 *
 * Stores A to R1, adds carry to R2, returns 0xFF.
 *
 * Original disassembly:
 *   178e: mov r1, a             ; R1 = A
 *   178f: clr a
 *   1790: addc a, r2            ; A = carry + R2
 *   1791: mov r2, a             ; R2 = A
 *   1792: mov a, #0xff
 *   1794: ret
 */
/* Note: This is a helper used in multi-byte arithmetic, difficult to express in C */

/*
 * dma_calc_addr_00c2 - Calculate address from IDATA[0x52] + 0xC2
 * Address: 0x179d-0x17a8 (12 bytes)
 *
 * Reads IDATA[0x52], adds 0xC2, returns pointer to that address.
 *
 * Original disassembly:
 *   179d: mov a, #0xc2
 *   179f: add a, 0x52           ; A = 0xC2 + IDATA[0x52]
 *   17a1: mov 0x82, a           ; DPL = A
 *   17a3: clr a
 *   17a4: addc a, #0x00         ; DPH = carry
 *   17a6: mov 0x83, a
 *   17a8: ret
 */
__xdata uint8_t *dma_calc_addr_00c2(void)
{
    uint8_t offset = I_WORK_52;
    return (__xdata uint8_t *)(0x00C2 + offset);
}

/*
 * dma_store_to_0a7d - Store R7 to 0x0A7D and check if == 1
 * Address: 0x180d-0x1819 (varies based on flow)
 *
 * Stores R7 to XDATA[0x0A7D], then checks if R7 XOR 0x01 == 0.
 *
 * Original disassembly:
 *   180d: mov dptr, #0x0a7d
 *   1810: mov a, r7
 *   1811: movx @dptr, a         ; XDATA[0x0A7D] = R7
 *   1812: xrl a, #0x01          ; A = R7 ^ 1
 *   1814: jz 0x1819             ; if R7 == 1, jump
 *   ...
 */
void dma_store_to_0a7d(uint8_t val)
{
    G_EP_DISPATCH_VAL3 = val;
}

/*
 * dma_calc_scsi_index - Calculate SCSI DMA register index
 * Address: 0x1602-0x1619 (24 bytes)
 *
 * Calculates (3 - IDATA[0x40]) and uses it to index into SCSI DMA
 * registers at 0xCE40+offset, then writes 0xFF.
 *
 * Original disassembly:
 *   1602: clr c
 *   1603: mov a, #0x03
 *   1605: subb a, 0x40         ; A = 3 - IDATA[0x40]
 *   1607: mov r7, a
 *   1608: clr a
 *   1609: subb a, #0x00        ; R6 = -carry
 *   160b: mov r6, a
 *   160c: mov a, #0x40
 *   160e: add a, r7            ; A = 0x40 + R7
 *   160f: mov 0x82, a          ; DPL
 *   1611: mov a, #0xce
 *   1613: addc a, r6           ; DPH = 0xCE + carry
 *   1614: mov 0x83, a
 *   1616: mov a, #0xff
 *   1618: movx @dptr, a        ; Write 0xFF to 0xCE40+index
 *   1619: ret
 */
void dma_calc_scsi_index(void)
{
    uint8_t idx = I_WORK_40;
    uint8_t offset = 3 - idx;
    __xdata uint8_t *reg = (__xdata uint8_t *)(0xCE40 + offset);
    *reg = 0xFF;
}

/*
 * dma_init_channel_with_config - Initialize DMA channel and write config
 * Address: 0x161a-0x1639 (32 bytes)
 *
 * Saves A to R4, initializes DMA channel 0 with config, then
 * writes to 0x045E and sets bit 0 in 0xC8D8.
 *
 * Original disassembly:
 *   161a: mov r4, a
 *   161b: mov r3, #0x40
 *   161d: mov r2, #0x00
 *   161f: clr a
 *   1620: mov r7, a            ; R7 = 0
 *   1621: lcall 0x4a57         ; dma_config_channel(0)
 *   1624: mov r3, #0x01
 *   1626: mov r2, #0xa0
 *   1628: mov r1, #0x00
 *   162a: mov dptr, #0x045e
 *   162d: lcall 0x0de6         ; xdata_store_triple
 *   1630: mov dptr, #0xc8d8
 *   1633: movx a, @dptr
 *   1634: anl a, #0xfe         ; clear bit 0
 *   1636: orl a, #0x01         ; set bit 0
 *   1638: movx @dptr, a
 *   1639: ret
 */
void dma_init_channel_with_config(uint8_t config)
{
    (void)config;  /* Used by caller context */

    /* Configure DMA channel 0 */
    dma_config_channel(0, 0x40);

    /* Write to config area at 0x045E (3 bytes: 0x01, 0xA0, 0x00) */
    G_REG_WAIT_BIT = 0x00;
    *(__xdata uint8_t *)(0x045F) = 0xA0;
    *(__xdata uint8_t *)(0x0460) = 0x01;

    /* Set bit 0 in DMA status 2 */
    REG_DMA_STATUS2 = (REG_DMA_STATUS2 & ~DMA_STATUS2_TRIGGER) | DMA_STATUS2_TRIGGER;
}

/*
 * dma_write_to_scsi_ce96 - Write to SCSI register 0xCE96
 * Address: 0x163a-0x1645 (12 bytes)
 *
 * Writes IDATA[0x41] to 0xCE96, reads 0xCE97, compares with IDATA[0x47].
 *
 * Original disassembly:
 *   163a: mov dptr, #0xce96
 *   163d: mov a, 0x41          ; IDATA[0x41]
 *   163f: movx @dptr, a        ; Write to 0xCE96
 *   1640: inc dptr             ; 0xCE97
 *   1641: movx a, @dptr
 *   1642: clr c
 *   1643: subb a, 0x47         ; Compare with IDATA[0x47]
 *   1645: ret
 */
uint8_t dma_write_to_scsi_ce96(void)
{
    uint8_t val41 = I_WORK_41;
    uint8_t val47 = I_WORK_47;
    uint8_t reg_val;

    REG_SCSI_DMA_CMD_REG = val41;
    reg_val = REG_SCSI_DMA_RESP_REG;

    /* Return comparison result (carry if reg_val < val47) */
    return (reg_val >= val47) ? 1 : 0;
}

/*
 * dma_calc_ep_config_ptr - Calculate endpoint config pointer
 * Address: 0x1646-0x1657 (18 bytes)
 *
 * Reads 0x0465, multiplies by 0x14, adds 0x4E, gives pointer in 0x05XX.
 *
 * Original disassembly:
 *   1646: mov dptr, #0x0465
 *   1649: movx a, @dptr        ; A = [0x0465]
 *   164a: mov 0xf0, #0x14      ; B = 0x14
 *   164d: mul ab               ; A*B
 *   164e: add a, #0x4e         ; A = result + 0x4E
 *   1650: mov 0x82, a          ; DPL
 *   1652: clr a
 *   1653: addc a, #0x05        ; DPH = 0x05 + carry
 *   1655: mov 0x83, a
 */
__xdata uint8_t *dma_calc_ep_config_ptr(void)
{
    uint8_t val = G_SYS_STATUS_SECONDARY;
    uint16_t addr = 0x0500 + (val * 0x14) + 0x4E;
    return (__xdata uint8_t *)addr;
}

/*
 * dma_write_to_scsi_ce6e - Write to SCSI register 0xCE6E twice
 * Address: 0x16ae-0x16b6 (9 bytes)
 *
 * Writes IDATA[0x41] to 0xCE6E, then writes IDATA[0x41]+1 to same register.
 *
 * Original disassembly:
 *   16ae: mov a, 0x41          ; A = IDATA[0x41]
 *   16b0: mov dptr, #0xce6e
 *   16b3: movx @dptr, a        ; Write IDATA[0x41]
 *   16b4: inc a
 *   16b5: movx @dptr, a        ; Write IDATA[0x41]+1
 *   16b6: ret
 */
void dma_write_to_scsi_ce6e(void)
{
    uint8_t val = I_WORK_41;
    REG_SCSI_DMA_STATUS = val;
    REG_SCSI_DMA_STATUS = val + 1;
}

/*
 * dma_calc_addr_046x - Calculate address 0x046X + R7
 * Address: 0x16b7-0x16c2 (12 bytes)
 *
 * Returns pointer to 0x046A + R7.
 *
 * Original disassembly:
 *   16b7: movx @dptr, a        ; Write A to caller's DPTR
 *   16b8: mov a, #0x6a         ; 0x6A
 *   16ba: add a, r7            ; A = 0x6A + R7
 *   16bb: mov 0x82, a          ; DPL
 *   16bd: clr a
 *   16be: addc a, #0x04        ; DPH = 0x04 + carry
 *   16c0: mov 0x83, a
 *   16c2: ret
 */
__xdata uint8_t *dma_calc_addr_046x(uint8_t offset)
{
    return (__xdata uint8_t *)(0x046A + offset);
}

/*
 * dma_calc_addr_0466 - Calculate address 0x0466 + R7
 * Address: 0x16de-0x16e8 (11 bytes)
 *
 * Returns pointer to 0x0466 + R7.
 *
 * Original disassembly:
 *   16de: mov a, #0x66
 *   16e0: add a, r7            ; A = 0x66 + R7
 *   16e1: mov 0x82, a          ; DPL
 *   16e3: clr a
 *   16e4: addc a, #0x04        ; DPH = 0x04 + carry
 *   16e6: mov 0x83, a
 *   16e8: ret
 */
__xdata uint8_t *dma_calc_addr_0466(uint8_t offset)
{
    return (__xdata uint8_t *)(0x0466 + offset);
}

/*
 * dma_calc_addr_0456 - Calculate address 0x0456 + A
 * Address: 0x16e9-0x16f2 (10 bytes)
 *
 * Returns pointer to 0x0456 + A.
 *
 * Original disassembly:
 *   16e9: add a, #0x56         ; A = A + 0x56
 *   16eb: mov 0x82, a          ; DPL
 *   16ed: clr a
 *   16ee: addc a, #0x04        ; DPH = 0x04 + carry
 *   16f0: mov 0x83, a
 *   16f2: ret
 */
__xdata uint8_t *dma_calc_addr_0456(uint8_t offset)
{
    return (__xdata uint8_t *)(0x0456 + offset);
}

/*
 * dma_write_idata_to_dptr - Write IDATA[0x41]+2 and +3 to consecutive DPTR
 * Address: 0x17d8-0x17e2 (11 bytes)
 *
 * Writes IDATA[0x41]+2 to DPTR, then IDATA[0x41]+3 to DPTR+1.
 *
 * Original disassembly:
 *   17d8: mov a, 0x41          ; IDATA[0x41]
 *   17da: add a, #0x02         ; A = IDATA[0x41] + 2
 *   17dc: movx @dptr, a
 *   17dd: mov a, 0x41
 *   17df: add a, #0x03         ; A = IDATA[0x41] + 3
 *   17e1: movx @dptr, a
 *   17e2: ret
 */
void dma_write_idata_to_dptr(__xdata uint8_t *ptr)
{
    uint8_t val = I_WORK_41;
    *ptr = val + 2;
    *ptr = val + 3;  /* Note: Same address, second write */
}

/*
 * dma_config_channel_0x10 - Configure DMA channel with R2=0x10
 * Address: 0x17e3-0x17ec (10 bytes)
 *
 * Calls dma_config_channel with specific parameters.
 *
 * Original disassembly:
 *   17e3: mov r2, #0x10
 *   17e5: lcall 0x4a57         ; dma_config_channel
 *   17e8: mov r2, #0x02
 *   17ea: mov r4, #0xb0
 *   17ec: ret
 */
void dma_config_channel_0x10(void)
{
    dma_config_channel(0x10, 0);
    /* Note: Returns R2=0x02, R4=0xB0 for caller use */
}

/*
 * dma_read_0461 - Read from 0x0461 and call reg_wait_bit_set
 * Address: 0x17ed-0x17f2 (6 bytes)
 *
 * Jumps to 0x0ddd (reg_wait_bit_set) with DPTR=0x0461.
 *
 * Original disassembly:
 *   17ed: mov dptr, #0x0461
 *   17f0: ljmp 0x0ddd          ; reg_wait_bit_set
 */
void dma_read_0461(void)
{
    /* Reads triple from 0x0461-0x0463 as a wait/sync operation */
    (void)xdata_load_triple((__xdata uint8_t *)0x0461);
}

/*
 * dma_calc_addr_002c - Calculate address 0x002C + offset
 * Address: 0x17fd-0x1803 (7 bytes)
 *
 * Adds 0x2C to A, stores in R1, adds carry to R2.
 *
 * Original disassembly:
 *   17fd: add a, #0x2c         ; A = A + 0x2C
 *   17ff: mov r1, a
 *   1800: clr a
 *   1801: addc a, r2           ; R2 = R2 + carry
 *   1802: mov r2, a
 *   1803: ret
 */
uint16_t dma_calc_addr_002c(uint8_t offset, uint8_t high)
{
    uint16_t result = offset + 0x2C;
    result |= (high + (result > 0xFF ? 1 : 0)) << 8;
    return result;
}

/*
 * dma_store_and_dispatch - Store to 0x0A7D and dispatch based on value
 * Address: 0x180d-0x181d (17 bytes)
 *
 * Stores R7 to 0x0A7D, if R7==1, reads 0x000A and dispatches.
 *
 * Original disassembly:
 *   180d: mov dptr, #0x0a7d
 *   1810: mov a, r7
 *   1811: movx @dptr, a        ; [0x0A7D] = R7
 *   1812: xrl a, #0x01         ; Check if R7 == 1
 *   1814: jz 0x1819            ; If equal, continue
 *   1816: ljmp 0x19fa          ; Else jump to dispatcher
 *   1819: mov dptr, #0x000a
 *   181c: movx a, @dptr        ; Read [0x000A]
 *   181d: jnz 0x182f           ; If non-zero, jump
 */
void dma_store_and_dispatch(uint8_t val)
{
    G_EP_DISPATCH_VAL3 = val;

    if (val != 0x01) {
        /* Would dispatch elsewhere - not implemented here */
        return;
    }

    /* Check flag at 0x000A */
    if (G_EP_CHECK_FLAG != 0) {
        return;
    }

    /* Additional dispatch logic follows in original */
}

/*
 * ============================================================================
 * TRANSFER HELPER FUNCTIONS (0x1602-0x16CC)
 * ============================================================================
 * These small helper functions compute addresses for DMA transfer registers
 * and perform status checks. They work with IDATA work area and SCSI DMA
 * registers at 0xCE40-0xCE9F.
 */

/* NOTE: Functions at 0x1602-0x1658 are implemented above as:
 *   - dma_calc_scsi_index (0x1602-0x1619)
 *   - dma_init_channel_with_config (0x161A-0x1639)
 *   - dma_write_to_scsi_ce96 (0x163A-0x1645)
 *   - dma_calc_ep_config_ptr (0x1646-0x1657)
 */

/*
 * transfer_set_dptr_0464_offset - Set DPTR based on [0x0464] + 0x4E
 * Address: 0x1659-0x1667 (15 bytes) - Note: 0x1659 is DPTR write entry point
 *
 * This actually appears to be called with DPTR already set by caller.
 * Writes A to @DPTR, then reads [0x0464], adds 0x4E, forms DPTR in 0x04XX.
 * The movx @dptr at 1659 is part of previous function flow.
 *
 * Full function from 0x165A:
 *   165a: mov dptr, #0x0464
 *   165d: movx a, @dptr
 *   165e: add a, #0x4e
 *   1660: mov DPL, a
 *   1662: clr a
 *   1663: addc a, #0x04
 *   1665: mov DPH, a
 *   1667: ret
 */
uint16_t transfer_set_dptr_0464_offset(void)
{
    uint8_t val = G_SYS_STATUS_PRIMARY;  /* 0x0464 */
    return 0x044E + val;
}

/*
 * transfer_calc_work43_offset - Calculate address 0x007C + IDATA 0x43
 * Address: 0x1668-0x1676 (15 bytes)
 *
 * Writes IDATA 0x41 to @DPTR (caller set), then calculates
 * DPTR = 0x007C + IDATA 0x43.
 *
 * Original disassembly:
 *   1668: mov a, 0x41
 *   166a: movx @dptr, a      ; write to caller's DPTR
 *   166b: mov a, #0x7c
 *   166d: add a, 0x43
 *   166f: mov DPL, a
 *   1671: clr a
 *   1672: addc a, #0x00
 *   1674: mov DPH, a
 *   1676: ret
 */
uint16_t transfer_calc_work43_offset(__xdata uint8_t *dptr)
{
    /* Write IDATA 0x41 to caller's DPTR first */
    *dptr = I_WORK_41;
    /* Return computed address */
    return 0x007C + I_WORK_43;
}

/*
 * transfer_calc_work53_offset - Calculate address 0x0477 + (IDATA 0x53 * 4)
 * Address: 0x1677-0x1686 (16 bytes)
 *
 * Reads IDATA 0x53, doubles it twice (x4), adds 0x77,
 * forms DPTR = 0x0400 + result.
 *
 * Original disassembly:
 *   1677: mov a, 0x53
 *   1679: add a, acc         ; A = A * 2
 *   167b: add a, acc         ; A = A * 2 again (total x4)
 *   167d: add a, #0x77
 *   167f: mov DPL, a
 *   1681: clr a
 *   1682: addc a, #0x04
 *   1684: mov DPH, a
 *   1686: ret
 */
uint16_t transfer_calc_work53_offset(void)
{
    uint8_t val;
    uint16_t addr;

    val = I_WORK_53;
    addr = 0x0477 + (val * 4);
    return addr;
}

/*
 * transfer_get_ep_queue_addr - Get endpoint queue address from [0x0464]
 * Address: 0x1687-0x1695 (15 bytes)
 *
 * Reads [0x0464] into R7, adds 0x5A, forms DPTR = 0x0400 + result.
 *
 * Original disassembly:
 *   1687: mov dptr, #0x0464
 *   168a: movx a, @dptr
 *   168b: mov r7, a
 *   168c: add a, #0x5a
 *   168e: mov DPL, a
 *   1690: clr a
 *   1691: addc a, #0x04
 *   1693: mov DPH, a
 *   1695: ret
 */
uint16_t transfer_get_ep_queue_addr(void)
{
    uint8_t idx;
    uint16_t addr;

    idx = G_SYS_STATUS_PRIMARY;  /* 0x0464 */
    addr = 0x045A + idx;
    return addr;
}

/*
 * transfer_calc_work55_offset - Calculate address 0x04B7 + IDATA 0x55
 * Address: 0x1696-0x16A1 (12 bytes)
 *
 * Adds 0xB7 to IDATA 0x55, forms DPTR = 0x0400 + result.
 *
 * Original disassembly:
 *   1696: mov a, #0xb7
 *   1698: add a, 0x55
 *   169a: mov DPL, a
 *   169c: clr a
 *   169d: addc a, #0x04
 *   169f: mov DPH, a
 *   16a1: ret
 */
uint16_t transfer_calc_work55_offset(void)
{
    uint16_t addr;

    addr = 0x04B7 + I_WORK_55;
    return addr;
}

/*
 * dma_setup_usb_rx - Setup DMA to receive data from USB host
 * Address: TODO - reverse engineer from original firmware
 *
 * Configures DMA engine to receive 'len' bytes from USB host
 * into the flash buffer (0x7000).
 *
 * Parameters:
 *   len: Number of bytes to receive
 */
void dma_setup_usb_rx(uint16_t len)
{
    uint8_t val;

    /* Configure DMA for USB to buffer transfer */
    dma_config_channel(0, 0);

    /* Set transfer length */
    REG_DMA_XFER_CNT_LO = (uint8_t)(len & 0xFF);
    REG_DMA_XFER_CNT_HI = (uint8_t)((len >> 8) & 0xFF);

    /* Set direction: USB -> Buffer (RX) */
    val = REG_DMA_CHAN_CTRL2;
    val &= ~0x02;  /* Clear direction bit for RX */
    REG_DMA_CHAN_CTRL2 = val;

    /* Trigger DMA */
    REG_DMA_TRIGGER = DMA_TRIGGER_START;
}

/*
 * dma_setup_usb_tx - Setup DMA to send data to USB host
 * Address: TODO - reverse engineer from original firmware
 *
 * Configures DMA engine to send 'len' bytes to USB host
 * from the flash/SCSI buffer.
 *
 * Parameters:
 *   len: Number of bytes to send
 */
void dma_setup_usb_tx(uint16_t len)
{
    uint8_t val;

    /* Configure DMA for buffer to USB transfer */
    dma_config_channel(0, 0);

    /* Set transfer length */
    REG_DMA_XFER_CNT_LO = (uint8_t)(len & 0xFF);
    REG_DMA_XFER_CNT_HI = (uint8_t)((len >> 8) & 0xFF);

    /* Set direction: Buffer -> USB (TX) */
    val = REG_DMA_CHAN_CTRL2;
    val |= 0x02;  /* Set direction bit for TX */
    REG_DMA_CHAN_CTRL2 = val;

    /* Trigger DMA */
    REG_DMA_TRIGGER = DMA_TRIGGER_START;
}

/*
 * dma_wait_complete - Wait for DMA transfer to complete
 * Address: TODO - reverse engineer from original firmware
 *
 * Blocks until current DMA transfer is complete.
 * Polls the DMA trigger register until busy bit clears.
 */
void dma_wait_complete(void)
{
    /* Poll until DMA transfer completes (bit 0 clears) */
    while (REG_DMA_TRIGGER & DMA_TRIGGER_START) {
        /* Busy wait */
    }

    /* Clear active flag */
    REG_DMA_CHAN_CTRL2 &= ~DMA_CHAN_CTRL2_ACTIVE;
}

/* NOTE: Function at 0x16AE-0x16B6 is implemented above as dma_write_to_scsi_ce6e */

/*
 * transfer_calc_r4_offset - Add R4 to accumulator for multi-byte add
 * Address: 0x16C3-0x16CC (10 bytes)
 *
 * This is a helper for 16-bit address calculations.
 * Adds R4 with carry to A, stores in R2, then clears A and adds carry.
 *
 * Original disassembly:
 *   16c3: addc a, r4
 *   16c4: mov r2, a
 *   16c5: clr a
 *   16c6: addc a, r5
 *   16c7: mov r3, a
 *   16c8: mov a, r2
 *   16c9: add a, #0x4e
 *   16cb: mov DPL, a
 *   ... continues to 16d2
 */
/* This is an internal calculation helper, handled inline */

/*
 * transfer_func_16b0 - Write value to SCSI DMA status register
 * Address: 0x16b0-0x16b6 (7 bytes)
 *
 * Disassembly:
 *   16b0: mov dptr, #0xce6e  ; REG_SCSI_DMA_STATUS
 *   16b3: movx @dptr, a      ; Write param
 *   16b4: inc a              ; param + 1
 *   16b5: movx @dptr, a      ; Write param + 1
 *   16b6: ret
 *
 * Writes param to REG_SCSI_DMA_STATUS_L, then writes param+1 to same location.
 * Moved from stubs.c.
 */
void transfer_func_16b0(uint8_t param)
{
    REG_SCSI_DMA_STATUS_L = param;
    REG_SCSI_DMA_STATUS_L = param + 1;
}

/* transfer_func_1633 and usb_calc_indexed_addr are forward declarations from this file and usb.h */

/*
 * dma_interrupt_handler - DMA/Queue endpoint handler
 * Address: 0x2608-0x2809 (514 bytes)
 *
 * Complex state machine that handles DMA transfers between PCIe queues
 * and USB endpoints. Manages queue indices, buffer states, and transfer
 * completion.
 */
void dma_interrupt_handler(void)
{
    __xdata uint8_t *ptr;
    uint8_t buf_state, flags_lo, queue_flags;
    uint8_t work51, work52, work53, work54, work56;
    uint8_t val, r6, r7, temp;

    /* 0x2608: Call transfer_get_ep_queue_addr, read endpoint index */
    ptr = (__xdata uint8_t *)transfer_get_ep_queue_addr();
    work53 = *ptr;
    I_WORK_53 = work53;

    /* 0x260e: Call dma_calc_addr_0466 with index, read result */
    ptr = dma_calc_addr_0466(G_SYS_STATUS_PRIMARY);
    buf_state = *ptr;
    G_BUFFER_STATE_0AA7 = buf_state;

loop_start:
    /* 0x2616: Check G_SYS_STATUS_PRIMARY */
    if (G_SYS_STATUS_PRIMARY != 0) {
        work56 = I_WORK_53 + 0x20;
    } else {
        work56 = I_WORK_53;
    }
    I_WORK_56 = work56;

    /* 0x2627: Set bit 0 of REG_DMA_STATUS */
    transfer_func_1633(0xC8D6);

    /* 0x262d: Write slot index to REG_DMA_QUEUE_IDX */
    REG_DMA_QUEUE_IDX = I_WORK_56;

    /* 0x2633: Read queue flags, mask bit 0 */
    flags_lo = REG_PCIE_QUEUE_FLAGS_LO & PCIE_QUEUE_FLAG_VALID;
    r7 = flags_lo;

    /* 0x263e: Compare with buffer state */
    if (G_BUFFER_STATE_0AA7 == r7) {
        REG_DMA_STATUS &= ~DMA_STATUS_TRIGGER;
        goto handle_match_end;
    }

    /* 0x264b: Read queue index low/high */
    work51 = REG_PCIE_QUEUE_INDEX_LO;
    I_WORK_51 = work51;
    work52 = REG_PCIE_QUEUE_INDEX_HI;
    I_WORK_52 = work52;

    /* 0x2655: Clear G_BUFFER_STATE_0AA6 */
    G_BUFFER_STATE_0AA6 = 0;

    /* 0x265a: Check combined flags */
    queue_flags = (REG_PCIE_QUEUE_FLAGS_LO & 0xFE) | REG_PCIE_QUEUE_FLAGS_HI;
    if (queue_flags == 0) {
        goto check_state;
    }

    /* 0x2666: Process queue flags */
    val = REG_PCIE_QUEUE_FLAGS_HI;
    val = (val >> 1) & 0x07;
    r7 = val;

    /* 0x266c: Calculate address = 0x04D7 + work51 */
    ptr = (__xdata uint8_t *)(0x04D7 + work51);
    *ptr = r7;

    /* 0x2679: Read B80F, process bits */
    val = REG_PCIE_QUEUE_FLAGS_HI;
    val = (val << 4) | (val >> 4);
    val = (val << 3);
    val &= 0x80;
    r7 = val;

    /* 0x2684: Read B80E, shift right, OR with r7 */
    val = REG_PCIE_QUEUE_FLAGS_LO;
    r6 = val;
    val = (val >> 1) | r7;
    r7 = val;

    /* 0x268d: Calculate address = 0x04F7 + work51 */
    ptr = (__xdata uint8_t *)(0x04F7 + work51);
    *ptr = r7;
    G_BUFFER_STATE_0AA6 = r7;

check_state:
    /* 0x269e: Check G_STATE_CTRL_0B3E */
    if (G_STATE_CTRL_0B3E == 0x01) {
        G_STATE_CTRL_0B3F++;
    }

    /* 0x26ab: Calculate address = 0x0108 + work52 */
    ptr = (__xdata uint8_t *)(0x0108 + work52);
    work54 = *ptr;
    I_WORK_54 = work54;

    r7 = 0;

    /* 0x26bb: Read from 0x009F + work52 */
    ptr = (__xdata uint8_t *)(0x009F + I_WORK_52);
    val = *ptr;

    if (val == 0x01) {
        if (I_WORK_54 & 0x10) {
            r7 = 1;
            goto process_transfer;
        }
        ptr = usb_calc_indexed_addr();
        *ptr = 0x01;
        goto process_transfer;
    }

    /* 0x26d3: Increment value at usb_calc_indexed_addr */
    ptr = usb_calc_indexed_addr();
    (*ptr)++;

    /* 0x26d9: Calculate address 0x00E5 + work52 */
    ptr = (__xdata uint8_t *)(0x00E5 + work52);
    r6 = *ptr;
    temp = G_BUFFER_STATE_0AA6;
    *ptr = r6 | temp;
    G_BUFFER_STATE_0AA6 = r6 | temp;

    if (!(I_WORK_54 & 0x10)) {
        goto process_transfer;
    }

    if (!(I_WORK_54 & 0x40)) {
        if (G_SCSI_CTRL > 0) {
            I_WORK_55 = 0;
            do {
                ptr = (__xdata uint8_t *)transfer_calc_work55_offset();
                val = *ptr;
                if (val == 0xFF) {
                    I_WORK_55++;
                    if (I_WORK_55 == 0x20) {
                        goto process_transfer;
                    }
                    continue;
                }
                ptr = (__xdata uint8_t *)transfer_calc_work55_offset();
                *ptr = I_WORK_51;
                r6 = G_NVME_STATE_053B;
                if (I_WORK_55 < r6) {
                    G_NVME_STATE_053B = I_WORK_55;
                }
                goto process_transfer;
            } while (1);
        }
        ptr = (__xdata uint8_t *)(0x00C2 + I_WORK_52);
        r6 = *ptr;
    } else {
        G_DMA_ENDPOINT_0578 = I_WORK_51;
        ptr = (__xdata uint8_t *)(0x009F + I_WORK_52);
        val = *ptr;
        if (val != r6) {
            goto process_transfer;
        }
        r7 = 1;
        goto process_transfer;
    }

    if (*ptr == r6) {
        r7 = 1;
    }

process_transfer:
    if (r7 == 0) {
        goto check_completion;
    }

    if (!(I_WORK_54 & 0x40)) {
        if (*(__idata uint8_t *)0x6A != 0x04) {
            goto check_completion;
        }
        if (G_BUFFER_STATE_0AA6 != 0) {
            dma_setup_transfer(0x03, 0x47, 0x0B);
        }
        scsi_csw_write_residue();
        REG_USB_SIGNAL_90A1 = 0x01;
        *(__idata uint8_t *)0x6A = 0x05;
        I_WORK_55 = 0;
        while (I_WORK_55 < G_NVME_STATE_053B) {
            ptr = (__xdata uint8_t *)transfer_calc_work55_offset();
            *ptr = 0xFF;
            I_WORK_55++;
        }
        goto check_completion;
    }

    if (G_BUFFER_STATE_0AA6 == 0) {
        val = REG_NVME_BUF_CFG;
        val = (val & 0xC0) | work52;
        REG_NVME_BUF_CFG = val;
        G_EP_DISPATCH_OFFSET = work52;
        scsi_decrement_pending();
    } else {
        dma_setup_transfer(0x03, 0x47, 0x0B);
    }

    ptr = (__xdata uint8_t *)(0x0071 + work52);
    *ptr = 0xFF;
    ptr = (__xdata uint8_t *)(0x0517 + work52);
    *ptr = 0;

check_completion:
    if (I_WORK_54 & 0x04) {
        scsi_buffer_threshold_config(I_WORK_52);
    }

    work53 = I_WORK_53;
    work53 = (work53 + 1) & 0x1F;
    I_WORK_53 = work53;

    if (work53 != 0) {
        goto loop_start;
    }

    G_BUFFER_STATE_0AA7 ^= 0x01;
    goto loop_start;

handle_match_end:
    val = G_SYS_STATUS_PRIMARY;
    ptr = (__xdata uint8_t *)(0x045A + val);
    val = *ptr;

    if (val == I_WORK_53) {
        return;
    }

    helper_0421(I_WORK_53);
    ptr = (__xdata uint8_t *)transfer_get_ep_queue_addr();
    *ptr = I_WORK_53;
    r6 = G_BUFFER_STATE_0AA7;
    ptr = dma_calc_addr_0466(G_SYS_STATUS_PRIMARY);
    *ptr = r6;
}



/* ============================================================
 * Functions moved from stubs.c
 * ============================================================ */

/*
 * transfer_func_1633 - Set bit 0 at specified register address
 * Address: 0x1633-0x1639 (7 bytes)
 *
 * Disassembly:
 *   1633: movx a, @dptr      ; Read current value (DPTR passed as param)
 *   1634: anl a, #0xfe       ; Clear bit 0
 *   1636: orl a, #0x01       ; Set bit 0
 *   1638: movx @dptr, a      ; Write back
 *   1639: ret
 *
 * Sets bit 0 of the register at the specified address.
 */
void transfer_func_1633(uint16_t addr)
{
    __xdata uint8_t *ptr = (__xdata uint8_t *)addr;
    uint8_t val = *ptr;
    val = (val & 0xFE) | 0x01;  /* Clear and set bit 0 */
    *ptr = val;
}

/*
 * dma_transfer_handler - PCIe lane configuration transfer handler
 * Address: 0xce23-0xce76 (84 bytes)
 *
 * Configures PCIe lane registers based on param:
 * - If param != 0: OR global status bytes with extended register values
 * - If param == 0: AND complement of global status bytes with ext reg values
 *
 * Then writes combined status back to register 0x35 and continues
 * PCIe initialization sequence.
 *
 * Disassembly:
 *   ce23: lcall 0xa334       ; status = pcie_read_status_a334()
 *   ce26: anl a, #0x3f       ; mask to bits 0-5
 *   ce28: mov r6, a          ; save in r6
 *   ce29: lcall 0xe890       ; pcie_handler_e890()
 *   ce2c: mov a, r7          ; check param (preserved in r7)
 *   ce2d-ce31: setup r3=0x02, r2=0x12, r1=0x40 for banked access
 *   ce33: jz 0xce4c          ; if param == 0, use AND-NOT path
 *   [ce35-ce4a: OR path - read ext regs, OR with globals, write to lane regs]
 *   [ce4c-ce64: AND-NOT path - read ext regs, AND with ~globals, write to lane regs]
 *   ce65: mov r1, #0x3f
 *   ce67: lcall 0x0be6       ; write accumulated value to ext reg 0x3f
 *   ce6a: lcall 0xe00c       ; get_pcie_status_flags_e00c()
 *   ce6d: lcall 0xa334       ; status = pcie_read_status_a334()
 *   ce70: anl a, #0xc0       ; keep bits 6-7 only
 *   ce72: orl a, r6          ; combine with saved bits 0-5
 *   ce73: lcall 0xe7f8       ; pcie_lane_init_e7f8() - writes combined to 0x35
 *   ce76: ljmp 0xe8cd        ; tail call clear_pcie_status_bytes_e8cd()
 *
 * PCIe extended registers (banked 0x02:0x12:xx -> XDATA 0xB2xx):
 *   0xB235: Link config status
 *   0xB23C-0xB23F: Lane config registers (write)
 *   0xB240-0xB243: Lane status registers (read)
 */
void dma_transfer_handler(uint8_t param)
{
    uint8_t saved_status_lo;
    uint8_t reg_val;
    uint8_t combined;

    /* Save lower 6 bits of current status */
    saved_status_lo = pcie_read_status_a334() & 0x3F;

    /* Reset PCIe extended registers and clear lane config */
    pcie_handler_e890();

    if (param != 0) {
        /* Non-zero path: OR global values with extended register values */
        /* Read ext reg 0x40, OR with G_PCIE_WORK_0B34, write to 0x3C */
        reg_val = XDATA_REG8(0xB240);
        combined = G_PCIE_WORK_0B34 | reg_val;
        XDATA_REG8(0xB23C) = combined;

        /* Read ext reg 0x41, OR with G_PCIE_STATUS_0B35, write to 0x3D */
        reg_val = XDATA_REG8(0xB241);
        combined = G_PCIE_STATUS_0B35 | reg_val;
        XDATA_REG8(0xB23D) = combined;

        /* Read ext reg 0x42, OR with G_PCIE_STATUS_0B36, write to 0x3E */
        reg_val = XDATA_REG8(0xB242);
        combined = G_PCIE_STATUS_0B36 | reg_val;
        XDATA_REG8(0xB23E) = combined;

        /* Read ext reg 0x43, OR with G_PCIE_STATUS_0B37 */
        reg_val = XDATA_REG8(0xB243);
        combined = G_PCIE_STATUS_0B37 | reg_val;
    } else {
        /* Zero path: AND complement of globals with extended register values */
        /* Read ext reg 0x40, AND with ~G_PCIE_WORK_0B34, write to 0x3C */
        reg_val = XDATA_REG8(0xB240);
        combined = (~G_PCIE_WORK_0B34) & reg_val;
        XDATA_REG8(0xB23C) = combined;

        /* Read ext reg 0x41, AND with ~G_PCIE_STATUS_0B35, write to 0x3D */
        reg_val = XDATA_REG8(0xB241);
        combined = (~G_PCIE_STATUS_0B35) & reg_val;
        XDATA_REG8(0xB23D) = combined;

        /* Read ext reg 0x42, AND with ~G_PCIE_STATUS_0B36, write to 0x3E */
        reg_val = XDATA_REG8(0xB242);
        combined = (~G_PCIE_STATUS_0B36) & reg_val;
        XDATA_REG8(0xB23E) = combined;

        /* Read ext reg 0x43, AND with ~G_PCIE_STATUS_0B37 */
        reg_val = XDATA_REG8(0xB243);
        combined = (~G_PCIE_STATUS_0B37) & reg_val;
    }

    /* Write final combined value to lane config register 0x3F */
    XDATA_REG8(0xB23F) = combined;

    /* Build status flags from PCIe buffers */
    get_pcie_status_flags_e00c();

    /* Combine upper 2 bits of current status with saved lower 6 bits */
    combined = (pcie_read_status_a334() & 0xC0) | saved_status_lo;

    /* Write combined status to register 0x35 and continue init */
    XDATA_REG8(0xB235) = combined;

    /* Continue with pcie_lane_init_e7f8 logic:
     * - Read reg 0x37, clear bit 7, set bit 7, write back
     * - Trigger command via reg 0x38
     * - Poll for completion
     * - Clear lane config registers
     */
    reg_val = XDATA_REG8(0xB237);
    reg_val = (reg_val & 0x7F) | 0x80;
    XDATA_REG8(0xB237) = reg_val;

    /* Write 0x01 to command trigger register */
    XDATA_REG8(0xB238) = 0x01;

    /* Poll until bit 0 clears (command complete) */
    while (XDATA_REG8(0xB238) & 0x01) {
        /* Wait for hardware */
    }

    /* Read link config, keep only bits 6-7, write back */
    reg_val = XDATA_REG8(0xB235);
    reg_val &= 0xC0;
    XDATA_REG8(0xB235) = reg_val;

    /* Clear lane config registers 0x3C-0x3F */
    XDATA_REG8(0xB23C) = 0x00;
    XDATA_REG8(0xB23D) = 0x00;
    XDATA_REG8(0xB23E) = 0x00;
    XDATA_REG8(0xB23F) = 0x00;

    /* Clear PCIe status bytes (tail call) */
    clear_pcie_status_bytes_e8cd();
}

/*
 * transfer_continuation_d996 - PCIe transfer continuation after poll complete
 * Address: 0xd996-0xd9?? (large function)
 *
 * This is the continuation function called as a tail call from dma_poll_complete.
 * It performs extensive PCIe register configuration using banked memory access.
 *
 * Called functions:
 *   - 0xccac: helper_ccac
 *   - 0xe8a9: helper_e8a9
 *   - 0xe57d: timer_phy_setup_e57d
 *   - 0xd630: power_helper_d630
 *   - 0xd436: config_helper_d436
 *   - 0xe25e: state_update_e25e
 *   - 0x0bc8: banked_load_byte
 *   - 0x0be6: banked_store_byte
 *
 * TODO: Implement full logic once helper functions are available.
 * For now, this is a stub that performs minimal setup.
 */
void transfer_continuation_d996(void)
{
    uint8_t val;

    /* Extended memory: clear bit 6 of banked register memtype=0x02, addr=0x7041 */
    val = banked_load_byte(0x41, 0x70, 0x02);
    val &= 0xBF;  /* Clear bit 6 */
    banked_store_byte(0x41, 0x70, 0x02, val);

    /* Extended memory: clear bit 2 of banked register memtype=0x00, addr=0x1507 */
    val = banked_load_byte(0x07, 0x15, 0x00);
    val &= 0xFB;  /* Clear bit 2 */
    banked_store_byte(0x07, 0x15, 0x00, val);

    /* Note: Original has extensive register configuration with calls to:
     * helper_ccac, helper_e8a9(0x0F), timer_phy_setup_e57d,
     * power_helper_d630(0x01), config_helper_d436(0x0F), state_update_e25e
     * These will be added once the helper functions are implemented. */
}

/*
 * dma_poll_complete - Timer poll and transfer handler
 * Address: 0xceab-0xcece (36 bytes)
 *
 * Sets up timer, polls status registers until ready, then calls
 * continuation handlers for PCIe transfer completion.
 *
 * Called from bank1 (5 calls from 0x14xxx addresses).
 *
 * Original disassembly:
 *   ceab: mov r7, #0x03        ; Set timer divisor bits to 3
 *   cead: lcall 0xe50d         ; Call timer setup helper
 *   ; Poll loop:
 *   ceb0: mov dptr, #0xe712    ; REG_LINK_STATUS_E712
 *   ceb3: movx a, @dptr        ; Read status
 *   ceb4: jb 0xe0.0, 0xcec6    ; If bit 0 set (done), exit loop
 *   ceb7: movx a, @dptr        ; Re-read status
 *   ceb8: anl a, #0x02         ; Isolate bit 1
 *   ceba: mov r7, a            ; Save in r7
 *   cebb: clr c
 *   cebc: rrc a                ; Shift right (bit 1 -> bit 0)
 *   cebd: jnz 0xcec6           ; If bit 1 was set, exit loop
 *   cebf: mov dptr, #0xcc11    ; REG_TIMER0_CSR
 *   cec2: movx a, @dptr        ; Read timer status
 *   cec3: jnb 0xe0.1, 0xceb0   ; If bit 1 NOT set, continue polling
 *   ; Exit path:
 *   cec6: lcall 0xe8ef         ; pcie_trigger_cc11_e8ef
 *   cec9: clr a
 *   ceca: mov r7, a            ; r7 = 0
 *   cecb: lcall 0xdd42         ; helper_dd42(0)
 *   cece: ljmp 0xd996          ; Tail call to continuation
 */
void dma_poll_complete(void)
{
    uint8_t status;

    /* Set timer divisor bits to 3 and start timer */
    /* Note: e50d also uses r4/r5 for thresholds inherited from caller context,
     * but the key part is setting div_bits = 3 */
    timer0_configure(0x03, 0x00, 0x00);

    /* Poll loop: wait for link status or timer timeout */
    while (1) {
        /* Check link status register */
        status = REG_LINK_STATUS_E712;

        /* If bit 0 is set, transfer complete - exit */
        if (status & 0x01) {
            break;
        }

        /* Check bit 1 for error/alternate exit condition */
        if (status & 0x02) {
            break;
        }

        /* Check timer status - bit 1 indicates timeout */
        status = REG_TIMER0_CSR;
        if (status & 0x02) {
            /* Timeout - exit poll loop */
            break;
        }
    }

    /* Reset timer/trigger */
    pcie_trigger_cc11_e8ef();

    /* Call state handler with param 0 */
    helper_dd42(0);

    /* Continue with transfer completion (tail call) */
    transfer_continuation_d996();
}

/*
 * TODO: dma_buffer_store_result_e68f is complex as it uses
 * DPTR-returning helpers (0x9983, 0x99bc, 0x9980) that modify DPTR
 * for table indexing. These helpers compute DPTR = base + R7*34.
 *
 * The function:
 * 1. Sets DPTR=0x05b3, calls 0x9983 to offset, writes R5 to result
 * 2. Calls 0x99bc to offset further, writes R3 to result
 * 3. Sets DPTR=0x0a5f, saves value, calls 0x9980, restores value
 *
 * This requires careful assembly-level handling as C can't directly
 * express DPTR manipulation across function boundaries.
 */
void dma_buffer_store_result_e68f(void)
{
    /* TODO: Implement - requires inline assembly or restructuring */
    (void)0;
}

void dma_poll_link_ready(void)
{
    uint8_t status;

    /* Configure Timer0 with divisor bits = 3 */
    timer0_configure(3, 0, 0);

    /* Poll loop: wait for E712 ready or timer timeout */
    while (1) {
        /* Read poll status register */
        status = REG_LINK_STATUS_E712;

        /* Check if bit 0 is set (ready) */
        if (status & 0x01) {
            break;
        }

        /* Check if bit 1 is set (alternate ready) */
        if (status & 0x02) {
            break;
        }

        /* Check timer status - if bit 1 of CC11 is set, timer expired */
        if (REG_TIMER0_CSR & 0x02) {
            break;
        }
    }

    /* Reset timer */
    pcie_trigger_cc11_e8ef();

    /* Update state to 0 */
    helper_dd42(0);

    /* Tail call to state handler */
    system_state_update();
}
