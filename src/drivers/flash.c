/*
 * ASM2464PD Firmware - Flash Driver
 *
 * SPI Flash controller interface for USB4/Thunderbolt to NVMe bridge.
 * Handles reading/writing to external SPI flash for firmware storage.
 *
 * ============================================================================
 * ARCHITECTURE OVERVIEW
 * ============================================================================
 * The ASM2464PD uses an external SPI flash chip to store firmware and
 * configuration data. The flash controller provides hardware-accelerated
 * SPI transactions with a 4KB buffer at 0x7000-0x7FFF.
 *
 * Data Flow:
 *   CPU <-> Flash Controller <-> SPI Bus <-> External Flash Chip
 *
 * ============================================================================
 * REGISTER MAP (0xC89F-0xC8AF)
 * ============================================================================
 * 0xC89F: REG_FLASH_CON      - Control register (transaction setup)
 * 0xC8A1: REG_FLASH_ADDR_LO  - Flash address low byte (A7:A0)
 * 0xC8A2: REG_FLASH_ADDR_MD  - Flash address middle byte (A15:A8)
 * 0xC8A3: REG_FLASH_DATA_LEN - Data length for transaction
 * 0xC8A4: (reserved)
 * 0xC8A6: REG_FLASH_DIV      - SPI clock divisor
 * 0xC8A9: REG_FLASH_CSR      - Control/Status register
 *                              Bit 0: Busy (poll until clear)
 *                              Write 0x01 to start transaction
 * 0xC8AA: REG_FLASH_CMD      - SPI command byte (0x03=read, 0x02=write, etc)
 * 0xC8AB: REG_FLASH_ADDR_HI  - Flash address high byte (A23:A16)
 * 0xC8AC: REG_FLASH_ADDR_LEN - Address length (typically 3 for 24-bit)
 * 0xC8AD: REG_FLASH_MODE     - Mode register
 *                              Bit 0: Enable mode
 *                              Bit 4: DMA mode
 *                              Bit 5: Write enable
 *                              Bit 6: Reserved
 * 0xC8AE: REG_FLASH_BUF_OFFSET - Buffer offset within 0x7000 region
 *
 * ============================================================================
 * TRANSACTION SEQUENCE
 * ============================================================================
 * 1. Clear REG_FLASH_CON to 0x00 (reset controller)
 * 2. Set up REG_FLASH_MODE (clear bits, set enable)
 * 3. Write flash address to ADDR_LO, ADDR_MD, ADDR_HI
 * 4. Write command to REG_FLASH_CMD
 * 5. Write data length to REG_FLASH_DATA_LEN
 * 6. Write 0x01 to REG_FLASH_CSR to start
 * 7. Poll REG_FLASH_CSR bit 0 until clear (transaction complete)
 * 8. Clear mode bits in REG_FLASH_MODE
 *
 * ============================================================================
 * FLASH BUFFER (0x7000-0x7FFF)
 * ============================================================================
 * The 4KB buffer is used for data transfer:
 * - For reads: Flash data is DMA'd into buffer, CPU reads from buffer
 * - For writes: CPU writes to buffer, flash controller DMA's to flash
 *
 * Global variables in buffer region (0x07xx):
 * 0x07B7-0x07B8: Flash operation status
 * 0x07BD:        Flash operation counter
 * 0x07C1-0x07C7: Flash state/config
 * 0x07DF:        Flash completion flag
 * 0x07E3:        Flash error code
 *
 * ============================================================================
 * IMPLEMENTATION STATUS
 * ============================================================================
 * Core functions:
 * [x] flash_poll_busy (0xbe70)       - Poll CSR until not busy
 * [x] flash_set_cmd (0xb845)         - Set command register
 * [x] flash_set_addr_md (0xb865)     - Set middle address byte
 * [x] flash_set_addr_hi (0xb873)     - Set high address byte
 * [x] flash_set_data_len (0xb888)    - Set data length
 * [x] flash_set_mode_enable (0xb8ae) - Enable flash mode
 * [x] flash_run_transaction (0xbe36) - Full flash transaction
 * [ ] flash_read                     - Read from flash to buffer
 * [ ] flash_write                    - Write from buffer to flash
 * [ ] flash_erase_sector             - Erase flash sector
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*
 * flash_div16 - Divide 16-bit value by 8-bit divisor
 * Address: 0x0c0f-0x0c1c (14 bytes) - when R4=0, R6=0
 *
 * Simple case: R7/R5 division when upper bytes are zero.
 * Returns quotient in R7, remainder in R5.
 *
 * Original disassembly (simple path):
 *   0c0f: cjne r4, #0x00, 0x0c1d  ; check R4
 *   0c12: cjne r6, #0x00, 0x0c3e  ; check R6
 *   0c15: mov a, r7               ; dividend low
 *   0c16: mov 0xf0, r5            ; divisor to B
 *   0c18: div ab                  ; A = quotient, B = remainder
 *   0c19: mov r7, a               ; store quotient
 *   0c1a: mov r5, 0xf0            ; store remainder
 *   0c1c: ret
 */
uint8_t flash_div8(uint8_t dividend, uint8_t divisor)
{
    if (divisor == 0) return 0;
    return dividend / divisor;
}

/*
 * flash_mod8 - Get remainder of 8-bit division
 * Address: 0x0c0f-0x0c1c (part of div function)
 *
 * Returns remainder from dividend/divisor.
 */
uint8_t flash_mod8(uint8_t dividend, uint8_t divisor)
{
    if (divisor == 0) return 0;
    return dividend % divisor;
}

/*
 * flash_add_to_xdata16 - Add value to 16-bit XDATA location
 * Address: 0x0c64-0x0c79 (22 bytes)
 *
 * Adds a 16-bit value (in A:B) to 16-bit value at DPTR, DPTR+1.
 * With carry propagation.
 *
 * Original disassembly:
 *   0c64: xch a, 0xf0        ; swap A and B
 *   0c66: mov r0, a          ; R0 = low byte of addend
 *   0c67: inc dptr           ; point to high byte
 *   0c68: movx a, @dptr      ; read high byte
 *   0c69: add a, r0          ; add low addend to high
 *   0c6a: movx @dptr, a      ; store
 *   0c6b: xch a, 0xf0        ; restore A (high addend)
 *   0c6d: mov r0, a
 *   0c6e-0c70: dec dptr      ; point back to low byte
 *   0c76: movx a, @dptr      ; read low byte
 *   0c77: addc a, r0         ; add with carry
 *   0c78: movx @dptr, a
 *   0c79: ret
 */
void flash_add_to_xdata16(__xdata uint8_t *ptr, uint16_t val)
{
    uint16_t curr;
    curr = ptr[0] | ((uint16_t)ptr[1] << 8);
    curr += val;
    ptr[0] = (uint8_t)(curr & 0xFF);
    ptr[1] = (uint8_t)(curr >> 8);
}

/*
 * flash_write_byte - Write single byte to flash address
 * Address: 0x0c7a-0x0c86 (13 bytes) - when R3=1
 *
 * Writes byte in A to XDATA address in R2:R1.
 *
 * Original disassembly:
 *   0c7a: cjne r3, #0x01, 0x0c87
 *   0c7d: mov 0x82, r1        ; DPL = R1
 *   0c7f: mov 0x83, r2        ; DPH = R2
 *   0c81: movx @dptr, a       ; write byte
 *   0c82: mov a, 0xf0         ; get high byte from B
 *   0c84: inc dptr
 *   0c85: movx @dptr, a       ; write high byte
 *   0c86: ret
 */
void flash_write_word(__xdata uint8_t *ptr, uint16_t val)
{
    ptr[0] = (uint8_t)(val & 0xFF);
    ptr[1] = (uint8_t)(val >> 8);
}

/*
 * flash_write_idata - Write to IDATA via R1
 * Address: 0x0c87-0x0c8e (8 bytes) - when R3=0
 *
 * Writes word in A:B to IDATA address in R1.
 *
 * Original disassembly (R3 < 1 path):
 *   0c87: jnc 0x0c8f          ; check carry from compare
 *   0c89: mov @r1, a          ; store low byte
 *   0c8a: inc r1
 *   0c8b: mov @r1, 0xf0       ; store high byte from B
 *   0c8d: dec r1
 *   0c8e: ret
 */
void flash_write_idata_word(__idata uint8_t *ptr, uint16_t val)
{
    ptr[0] = (uint8_t)(val & 0xFF);
    ptr[1] = (uint8_t)(val >> 8);
}

/*
 * flash_write_r1_xdata - Write word to XDATA via R1 (indirect)
 * Address: 0x0c8f-0x0c98 (10 bytes) - when R3=0xFE
 *
 * Uses R1 as indirect XDATA pointer.
 *
 * Original disassembly:
 *   0c8f: cjne r3, #0xfe, 0x0c99
 *   0c92: movx @r1, a         ; store low byte via R1
 *   0c93: mov a, 0xf0         ; get high byte
 *   0c95: inc r1
 *   0c96: movx @r1, a         ; store high byte
 *   0c97: dec r1
 *   0c98: ret
 */
void flash_write_r1_xdata_word(uint8_t r1_addr, uint16_t val)
{
    /* Uses R1 as XDATA pointer - specific to 8051 indirect mode */
    __xdata uint8_t *ptr = (__xdata uint8_t *)(uint16_t)r1_addr;
    ptr[0] = (uint8_t)(val & 0xFF);
    ptr[1] = (uint8_t)(val >> 8);
}

/*
 * flash_poll_busy - Poll flash CSR until not busy
 * Address: 0xbe70-0xbe76 (7 bytes)
 *
 * Waits for flash controller to complete current operation.
 * Polls bit 0 of REG_FLASH_CSR until it clears.
 *
 * Original disassembly:
 *   be70: mov dptr, #0xc8a9   ; REG_FLASH_CSR
 *   be73: movx a, @dptr       ; read CSR
 *   be74: jb 0xe0.0, 0xbe70   ; loop while bit 0 set (busy)
 *   be77: ...                 ; continue
 */
void flash_poll_busy(void)
{
    while (REG_FLASH_CSR & FLASH_CSR_BUSY) {
        /* Wait for busy bit to clear */
    }
}

/*
 * flash_set_cmd - Set flash command register
 * Address: 0xb845-0xb84f (11 bytes)
 *
 * Sets REG_FLASH_CMD and reads back address length register,
 * masking off lower 2 bits.
 *
 * Original disassembly:
 *   b845: mov dptr, #0xc8aa   ; REG_FLASH_CMD
 *   b848: movx @dptr, a       ; write command
 *   b849: mov dptr, #0xc8ac   ; REG_FLASH_ADDR_LEN
 *   b84c: movx a, @dptr       ; read addr len
 *   b84d: anl a, #0xfc        ; mask bits 1:0
 *   b84f: ret
 */
uint8_t flash_set_cmd(uint8_t cmd)
{
    REG_FLASH_CMD = cmd;
    return REG_FLASH_ADDR_LEN & 0xFC;
}

/*
 * flash_set_addr_md - Set flash middle address byte
 * Address: 0xb865-0xb872 (14 bytes)
 *
 * Reads 32-bit address from XDATA, shifts right 8 bits,
 * and writes result to REG_FLASH_ADDR_MD.
 *
 * Original disassembly:
 *   b865: lcall 0x0d84        ; read 32-bit from DPTR to R4-R7
 *   b868: mov r0, #0x08       ; shift count
 *   b86a: lcall 0x0d33        ; shift right R4-R7 by R0 bits
 *   b86d: mov dptr, #0xc8a2   ; REG_FLASH_ADDR_MD
 *   b870: mov a, r7
 *   b871: movx @dptr, a
 *   b872: ret
 */
void flash_set_addr_md(__xdata uint8_t *addr_ptr)
{
    uint32_t addr;

    /* Read 32-bit address from XDATA (little-endian) */
    addr = addr_ptr[0];
    addr |= ((uint32_t)addr_ptr[1]) << 8;
    addr |= ((uint32_t)addr_ptr[2]) << 16;
    addr |= ((uint32_t)addr_ptr[3]) << 24;

    /* Shift right 8 and write middle byte */
    REG_FLASH_ADDR_MD = (uint8_t)(addr >> 8);
}

/*
 * flash_set_addr_hi - Set flash high address byte
 * Address: 0xb873-0xb880 (14 bytes)
 *
 * Reads 32-bit address from XDATA, shifts right 16 bits,
 * and writes result to REG_FLASH_ADDR_HI.
 *
 * Original disassembly:
 *   b873: lcall 0x0d84        ; read 32-bit from DPTR to R4-R7
 *   b876: mov r0, #0x10       ; shift count (16)
 *   b878: lcall 0x0d33        ; shift right R4-R7 by R0 bits
 *   b87b: mov dptr, #0xc8ab   ; REG_FLASH_ADDR_HI
 *   b87e: mov a, r7
 *   b87f: movx @dptr, a
 *   b880: ret
 */
void flash_set_addr_hi(__xdata uint8_t *addr_ptr)
{
    uint32_t addr;

    /* Read 32-bit address from XDATA (little-endian) */
    addr = addr_ptr[0];
    addr |= ((uint32_t)addr_ptr[1]) << 8;
    addr |= ((uint32_t)addr_ptr[2]) << 16;
    addr |= ((uint32_t)addr_ptr[3]) << 24;

    /* Shift right 16 and write high byte */
    REG_FLASH_ADDR_HI = (uint8_t)(addr >> 16);
}

/*
 * flash_set_data_len - Set flash data length register
 * Address: 0xb888-0xb894 (13 bytes)
 *
 * Reads 16-bit length from XDATA and writes to data length registers.
 *
 * Original disassembly:
 *   b888: movx a, @dptr       ; read low byte
 *   b889: mov r7, a
 *   b88a: inc dptr
 *   b88b: movx a, @dptr       ; read high byte
 *   b88c: mov dptr, #0xc8a3   ; REG_FLASH_DATA_LEN
 *   b88f: xch a, r7           ; swap to write low first
 *   b890: movx @dptr, a       ; write low byte
 *   b891: inc dptr
 *   b892: mov a, r7
 *   b893: movx @dptr, a       ; write high byte
 *   b894: ret
 */
void flash_set_data_len(__xdata uint8_t *len_ptr)
{
    uint8_t lo = len_ptr[0];
    uint8_t hi = len_ptr[1];

    REG_FLASH_DATA_LEN = lo;
    REG_FLASH_DATA_LEN_HI = hi;
}

/*
 * flash_set_mode_enable - Enable flash mode with bit 0 set
 * Address: 0xb8ae-0xb8b8 (11 bytes)
 *
 * Reads REG_FLASH_MODE, clears bit 0, sets bit 0, writes back.
 * Returns with DPTR pointing to next register (0xC8AE).
 *
 * Original disassembly:
 *   b8ae: mov dptr, #0xc8ad   ; REG_FLASH_MODE
 *   b8b1: movx a, @dptr       ; read mode
 *   b8b2: anl a, #0xfe        ; clear bit 0
 *   b8b4: orl a, #0x01        ; set bit 0
 *   b8b6: movx @dptr, a       ; write back
 *   b8b7: inc dptr            ; DPTR = 0xC8AE
 *   b8b8: ret
 */
void flash_set_mode_enable(void)
{
    uint8_t val = REG_FLASH_MODE;
    val = (val & 0xFE) | 0x01;
    REG_FLASH_MODE = val;
}

/*
 * flash_set_mode_bit4 - Set DMA mode bit in flash mode register
 * Address: 0xb85b-0xb864 (10 bytes)
 *
 * Reads REG_FLASH_MODE, clears bit 4, sets bit 4, writes back.
 *
 * Original disassembly:
 *   b85b: mov dptr, #0xc8ad   ; REG_FLASH_MODE
 *   b85e: movx a, @dptr
 *   b85f: anl a, #0xef        ; clear bit 4
 *   b861: orl a, #0x10        ; set bit 4
 *   b863: movx @dptr, a
 *   b864: ret
 */
void flash_set_mode_bit4(void)
{
    uint8_t val = REG_FLASH_MODE;
    val = (val & 0xEF) | 0x10;
    REG_FLASH_MODE = val;
}

/*
 * flash_start_transaction - Start flash transaction and poll until complete
 * Address: 0xbe6a-0xbe76 (part of 0xbe36)
 *
 * Writes 0x01 to CSR to start, then polls until done.
 */
void flash_start_transaction(void)
{
    REG_FLASH_CSR = FLASH_CSR_BUSY;
    flash_poll_busy();
}

/*
 * flash_clear_mode_bits - Clear mode register bits 4 and 5
 * Address: 0xbe77-0xbe81 (part of 0xbe36)
 *
 * Clears bit 4 (DMA mode) and bit 5 (write enable) in REG_FLASH_MODE.
 *
 * Original disassembly:
 *   be77: mov dptr, #0xc8ad
 *   be7a: movx a, @dptr
 *   be7b: anl a, #0xef        ; clear bit 4
 *   be7d: movx @dptr, a
 *   be7e: movx a, @dptr
 *   be7f: anl a, #0xdf        ; clear bit 5
 *   be81: movx @dptr, a
 */
void flash_clear_mode_bits(void)
{
    uint8_t val;

    val = REG_FLASH_MODE;
    val &= 0xEF;  /* Clear bit 4 */
    REG_FLASH_MODE = val;

    val = REG_FLASH_MODE;
    val &= 0xDF;  /* Clear bit 5 */
    REG_FLASH_MODE = val;
}

/*
 * flash_clear_mode_bits_6_7 - Clear mode register bits 6 and 7
 * Address: 0xbe82-0xbe8a (end of 0xbe36)
 *
 * Clears bit 6 and bit 7 in REG_FLASH_MODE.
 *
 * Original disassembly:
 *   be82: movx a, @dptr
 *   be83: anl a, #0xbf        ; clear bit 6
 *   be85: movx @dptr, a
 *   be86: movx a, @dptr
 *   be87: anl a, #0x7f        ; clear bit 7
 *   be89: movx @dptr, a
 *   be8a: ret
 */
void flash_clear_mode_bits_6_7(void)
{
    uint8_t val;

    val = REG_FLASH_MODE;
    val &= 0xBF;  /* Clear bit 6 */
    REG_FLASH_MODE = val;

    val = REG_FLASH_MODE;
    val &= 0x7F;  /* Clear bit 7 */
    REG_FLASH_MODE = val;
}

/*
 * flash_run_transaction - Run a complete flash transaction
 * Address: 0xbe36-0xbe8a (85 bytes)
 *
 * Sets up and executes a flash transaction with the given command.
 * Reads address from global 0x0AAD and length from 0x0AB1.
 *
 * Parameters:
 *   cmd: Flash command (e.g., 0x03 for read, 0x02 for write)
 *
 * Original disassembly:
 *   be36: mov dptr, #0xc89f   ; REG_FLASH_CON
 *   be39: clr a
 *   be3a: movx @dptr, a       ; clear control register
 *   be3b: mov dptr, #0xc8ad   ; REG_FLASH_MODE
 *   be3e: movx a, @dptr
 *   be3f: anl a, #0xfe        ; clear bit 0
 *   be41: movx @dptr, a
 *   be42: inc dptr            ; 0xC8AE
 *   be43: clr a
 *   be44: movx @dptr, a       ; clear buffer offset low
 *   be45: inc dptr            ; 0xC8AF
 *   be46: movx @dptr, a       ; clear buffer offset high
 *   be47: mov a, r1           ; command from R1
 *   be48: lcall 0xb845        ; flash_set_cmd
 *   be4b: orl a, r5           ; OR with address len mask
 *   be4c: movx @dptr, a       ; write back
 *   be4d: mov dptr, #0x0aad   ; address source
 *   be50: lcall 0x0d84        ; read 32-bit address
 *   be53: mov dptr, #0xc8a1   ; REG_FLASH_ADDR_LO
 *   be56: mov a, r7
 *   be57: movx @dptr, a       ; write address low
 *   be58: mov dptr, #0x0aad
 *   be5b: lcall 0xb865        ; flash_set_addr_md
 *   be5e: mov dptr, #0x0aad
 *   be61: lcall 0xb873        ; flash_set_addr_hi
 *   be64: mov dptr, #0x0ab1   ; length source
 *   be67: lcall 0xb888        ; flash_set_data_len
 *   be6a: mov dptr, #0xc8a9   ; REG_FLASH_CSR
 *   be6d: mov a, #0x01
 *   be6f: movx @dptr, a       ; start transaction
 *   be70: [poll loop]
 *   be77-be8a: [clear mode bits]
 */
void flash_run_transaction(uint8_t cmd)
{
    uint8_t addr_len_mask;
    uint32_t addr;

    /* Clear control register */
    REG_FLASH_CON = 0x00;

    /* Clear mode bit 0 */
    REG_FLASH_MODE &= 0xFE;

    /* Clear buffer offset */
    REG_FLASH_BUF_OFFSET = 0x0000;

    /* Set command and get address length mask */
    addr_len_mask = flash_set_cmd(cmd);

    /* OR result with R5 (from address length register) */
    /* Write to address length register with mask */
    REG_FLASH_ADDR_LEN |= addr_len_mask;

    /* Read 32-bit address from global G_FLASH_ADDR_0-3 */
    addr = G_FLASH_ADDR_0;
    addr |= ((uint32_t)G_FLASH_ADDR_1) << 8;
    addr |= ((uint32_t)G_FLASH_ADDR_2) << 16;
    addr |= ((uint32_t)G_FLASH_ADDR_3) << 24;

    /* Write address low byte */
    REG_FLASH_ADDR_LO = (uint8_t)(addr & 0xFF);

    /* Write address middle byte (bits 15:8) */
    REG_FLASH_ADDR_MD = (uint8_t)((addr >> 8) & 0xFF);

    /* Write address high byte (bits 23:16) */
    REG_FLASH_ADDR_HI = (uint8_t)((addr >> 16) & 0xFF);

    /* Set data length from global G_FLASH_LEN_LO/HI */
    REG_FLASH_DATA_LEN = G_FLASH_LEN_LO;
    REG_FLASH_DATA_LEN_HI = G_FLASH_LEN_HI;

    /* Start transaction */
    flash_start_transaction();

    /* Clear mode bits */
    flash_clear_mode_bits();
    flash_clear_mode_bits_6_7();
}

/*
 * flash_wait_and_poll - Start flash and poll with timeout check
 * Address: 0xb1a4-0xb1ca (39 bytes)
 *
 * Starts flash transaction, polls until complete with timeout.
 * Returns 1 on success, 0 on timeout/error.
 *
 * Original disassembly:
 *   b1a4: mov dptr, #0xc8a9   ; REG_FLASH_CSR
 *   b1a7: mov a, #0x01
 *   b1a9: movx @dptr, a       ; start
 *   b1aa: mov dptr, #0xc8a9   ; poll loop
 *   b1ad: movx a, @dptr
 *   b1ae: jb 0xe0.0, 0xb1aa   ; loop while busy
 *   b1b1: mov r7, #0x01
 *   b1b3: lcall 0xdf47        ; delay/timeout check
 *   b1b6: mov a, r7
 *   b1b7: jnz 0xb1bb          ; if timeout, check more
 *   b1b9: mov r7, a           ; return 0
 *   b1ba: ret
 *   b1bb: mov dptr, #0x0aa8
 *   b1be: movx a, @dptr       ; check error flag
 *   b1bf: jnz 0xb1c3
 *   b1c1: inc dptr
 *   b1c2: movx a, @dptr
 *   b1c3: jz 0xb1c8           ; if zero, success
 *   b1c5: ljmp 0xb10f         ; error path
 *   b1c8: mov r7, #0x01       ; return 1 (success)
 *   b1ca: ret
 */
uint8_t flash_wait_and_poll(void)
{
    /* Start transaction */
    REG_FLASH_CSR = FLASH_CSR_BUSY;

    /* Poll until not busy */
    while (REG_FLASH_CSR & FLASH_CSR_BUSY) {
        /* Wait */
    }

    /* Check error flags */
    if (G_FLASH_ERROR_0 != 0 || G_FLASH_ERROR_1 != 0) {
        return 0;  /* Error */
    }

    return 1;  /* Success */
}

/*
 * flash_read_status - Read flash status byte
 * Address: 0xe3f9-0xe418 (32 bytes)
 *
 * Reads a status byte from flash using command 0x01.
 * Sets up mode, issues command, polls for completion.
 *
 * Original disassembly:
 *   e3f9: lcall 0xb8ae        ; flash_set_mode_enable
 *   e3fc: clr a
 *   e3fd: movx @dptr, a       ; buffer offset = 0x0000
 *   e3fe: inc dptr
 *   e3ff: movx @dptr, a
 *   e400: inc a               ; A = 0x01 (command)
 *   e401: lcall 0xb845        ; flash_set_cmd(0x01)
 *   e404: movx @dptr, a       ; write addr_len result
 *   e405: mov dptr, #0xc8a3   ; REG_FLASH_DATA_LEN
 *   e408: clr a
 *   e409: movx @dptr, a       ; length low = 0
 *   e40a: inc dptr
 *   e40b: inc a               ; A = 1
 *   e40c: movx @dptr, a       ; length high = 1
 *   e40d: mov dptr, #0xc8a9   ; REG_FLASH_CSR
 *   e410: movx @dptr, a       ; start (A=1)
 *   e411: [poll loop until bit 0 clear]
 *   e418: ret
 */
void flash_read_status(void)
{
    uint8_t addr_len_mask;

    /* Enable flash mode */
    flash_set_mode_enable();

    /* Clear buffer offset */
    REG_FLASH_BUF_OFFSET = 0x0000;

    /* Set command 0x01 (read status) */
    addr_len_mask = flash_set_cmd(0x01);
    REG_FLASH_ADDR_LEN |= addr_len_mask;

    /* Set data length = 1 byte */
    REG_FLASH_DATA_LEN = 0x00;
    REG_FLASH_DATA_LEN_HI = 0x01;

    /* Start and poll */
    REG_FLASH_CSR = FLASH_CSR_BUSY;
    flash_poll_busy();
}

/*
 * flash_read_buffer_and_status - Read from buffer and call status
 * Address: 0xb895-0xb8a1 (13 bytes)
 *
 * Reads from flash buffer at 0x7000, masks bits, calls flash_read_status.
 *
 * Original disassembly:
 *   b895: mov dptr, #0x7000   ; flash buffer
 *   b898: movx a, @dptr       ; read first byte
 *   b899: anl a, #0x63        ; mask bits
 *   b89b: movx @dptr, a       ; write back
 *   b89c: lcall 0xe3f9        ; flash_read_status
 *   b89f: mov r7, #0x01       ; return 1
 *   b8a1: ret
 */
uint8_t flash_read_buffer_and_status(void)
{
    uint8_t val;

    /* Read from flash buffer */
    val = XDATA8(FLASH_BUFFER_BASE);
    val &= 0x63;
    XDATA8(FLASH_BUFFER_BASE) = val;

    /* Read status */
    flash_read_status();

    return 1;
}

/*
 * flash_get_buffer_byte - Get byte from flash buffer
 * Address: part of various functions
 *
 * Reads a byte from the flash buffer at specified offset.
 */
uint8_t flash_get_buffer_byte(uint16_t offset)
{
    return XDATA8(FLASH_BUFFER_BASE + offset);
}

/*
 * flash_set_buffer_byte - Set byte in flash buffer
 * Address: part of various functions
 *
 * Writes a byte to the flash buffer at specified offset.
 */
void flash_set_buffer_byte(uint16_t offset, uint8_t val)
{
    XDATA8(FLASH_BUFFER_BASE + offset) = val;
}

/*
 * flash_write_enable - Enable flash write operations
 * Address: TODO - reverse engineer from original firmware
 *
 * Sends write enable (WREN) command to SPI flash.
 * Must be called before any write/erase operation.
 */
void flash_write_enable(void)
{
    /* Enable mode bit for write operations */
    uint8_t val = REG_FLASH_MODE;
    val |= 0x20;  /* Set bit 5 (write enable) */
    REG_FLASH_MODE = val;

    /* Send WREN command (0x06) to flash */
    flash_set_mode_enable();
    REG_FLASH_CMD = 0x06;  /* WREN command */
    REG_FLASH_DATA_LEN = 0;
    REG_FLASH_DATA_LEN_HI = 0;
    flash_start_transaction();
}

/*
 * flash_write_page - Write data from buffer to flash
 * Address: TODO - reverse engineer from original firmware
 *
 * Writes len bytes from flash buffer (0x7000) to flash at addr.
 * Flash write enable must be called first.
 *
 * Parameters:
 *   addr: 24-bit flash address
 *   len: Number of bytes to write (max 256 for page program)
 */
void flash_write_page(uint32_t addr, uint8_t len)
{
    /* Set up flash address globals */
    G_FLASH_ADDR_0 = (uint8_t)(addr & 0xFF);
    G_FLASH_ADDR_1 = (uint8_t)((addr >> 8) & 0xFF);
    G_FLASH_ADDR_2 = (uint8_t)((addr >> 16) & 0xFF);
    G_FLASH_ADDR_3 = 0;

    /* Set length */
    G_FLASH_LEN_LO = len;
    G_FLASH_LEN_HI = 0;

    /* Run page program transaction (command 0x02) */
    flash_run_transaction(0x02);
}

/*
 * flash_read - Read data from flash to buffer
 * Address: TODO - reverse engineer from original firmware
 *
 * Reads len bytes from flash at addr to flash buffer (0x7000).
 *
 * Parameters:
 *   addr: 24-bit flash address
 *   len: Number of bytes to read
 */
void flash_read(uint32_t addr, uint8_t len)
{
    /* Set up flash address globals */
    G_FLASH_ADDR_0 = (uint8_t)(addr & 0xFF);
    G_FLASH_ADDR_1 = (uint8_t)((addr >> 8) & 0xFF);
    G_FLASH_ADDR_2 = (uint8_t)((addr >> 16) & 0xFF);
    G_FLASH_ADDR_3 = 0;

    /* Set length */
    G_FLASH_LEN_LO = len;
    G_FLASH_LEN_HI = 0;

    /* Run read transaction (command 0x03) */
    flash_run_transaction(0x03);
}

/*
 * flash_erase_sector - Erase a 4KB flash sector
 * Address: TODO - reverse engineer from original firmware
 *
 * Erases the 4KB sector containing addr.
 * Flash write enable must be called first.
 *
 * Parameters:
 *   addr: 24-bit flash address (any address within sector)
 */
void flash_erase_sector(uint32_t addr)
{
    /* Enable write first */
    flash_write_enable();

    /* Set up flash address globals (sector aligned) */
    addr &= 0xFFFFF000;  /* Align to 4KB boundary */
    G_FLASH_ADDR_0 = (uint8_t)(addr & 0xFF);
    G_FLASH_ADDR_1 = (uint8_t)((addr >> 8) & 0xFF);
    G_FLASH_ADDR_2 = (uint8_t)((addr >> 16) & 0xFF);
    G_FLASH_ADDR_3 = 0;

    /* No data length for erase */
    G_FLASH_LEN_LO = 0;
    G_FLASH_LEN_HI = 0;

    /* Run sector erase transaction (command 0x20) */
    flash_run_transaction(0x20);
}

