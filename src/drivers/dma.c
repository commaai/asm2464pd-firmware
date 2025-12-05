/*
 * ASM2464PD Firmware - DMA Driver
 *
 * DMA engine control for USB4/Thunderbolt to NVMe bridge.
 * Handles DMA transfers between USB, NVMe, and internal buffers.
 *
 * DMA Engine Registers: 0xC800-0xC9FF
 * SCSI/Mass Storage DMA: 0xCE40-0xCE6E
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

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
    /* Call reg_wait_bit_set(0x045E, val) */
    /* TODO: Implement reg_wait_bit_set */
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

    /* TODO: Call flash_func_0c0f(0, BANK0_R3, param1, param2) */
    (void)param1;
    (void)param2;
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
        /* lcall 0x1bcb - TODO: implement */
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
        if (status & 0x01) {
            dma_set_scsi_param3();
            return 1;
        }
    } else if (mode == 0x10) {
        /* Check bit 1 of SCSI completion status */
        status = REG_SCSI_DMA_COMPL;
        if (status & 0x02) {
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
    XDATA8(0x0AA3) = 0;  /* State counter high */
    XDATA8(0x0AA4) = 0;  /* State counter low */
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
    XDATA8(0x0565) = 0;     /* Endpoint queue status */
    XDATA8(0x0564) = 0x08;  /* Endpoint queue control */
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

    count = REG_SCSI_DMA_TAG_COUNT & 0x1F;
    *(__idata uint8_t *)0x40 = count;

    /* Return 1 if count >= 16, 0 otherwise */
    return (count >= 0x10) ? 1 : 0;
}

/* Additional DMA functions will be added as they are reversed */

