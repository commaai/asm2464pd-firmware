/*
 * flash.c - SPI Flash Driver
 *
 * See drivers/flash.h for hardware documentation.
 */

#include "drivers/flash.h"
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
    return REG_FLASH_ADDR_LEN & FLASH_ADDR_LEN_MASK;
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
    REG_FLASH_MODE &= ~FLASH_MODE_ENABLE;

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

/*===========================================================================
 * Bank 1 Flash Dispatch Stub Functions (0x873a-0x8d6e)
 *
 * These functions are in Bank 1 (address 0x10000-0x17FFF mapped at 0x8000)
 * and serve as dispatch stubs that call flash_func_0bc8 with 0xFF parameter.
 * These are error/fallback handlers that jump to flash error recovery.
 *
 * flash_func_0bc8 is a core dispatcher at 0x0bc8 that handles state machine
 * transitions and error recovery. The 0xFF parameter typically indicates
 * an error condition or reset state.
 *===========================================================================*/

/* External flash dispatcher function */
extern void flash_func_0bc8(uint8_t param);  /* 0x0bc8 - Main flash dispatcher */

/*
 * flash_dispatch_stub_873a - Flash error dispatch stub
 * Bank 1 Address: 0x873a-0x8742 (9 bytes) [actual addr: 0x1073a]
 *
 * Calls flash_func_0bc8(0xff) which does not return.
 * This is an error fallback - jumps to flash error recovery.
 *
 * Original disassembly (from ghidra.c):
 *   // WARNING: Subroutine does not return
 *   flash_func_0bc8(0xff);
 */
void flash_dispatch_stub_873a(void)
{
    flash_func_0bc8(0xFF);
}

/*
 * flash_dispatch_stub_8743 - Flash error dispatch stub
 * Bank 1 Address: 0x8743-0x874b (9 bytes) [actual addr: 0x10743]
 *
 * Identical to 873a - possibly for different state machine entry point.
 *
 * Original disassembly:
 *   // WARNING: Subroutine does not return
 *   flash_func_0bc8(0xff);
 */
void flash_dispatch_stub_8743(void)
{
    flash_func_0bc8(0xFF);
}

/*
 * flash_dispatch_stub_874c - Flash error dispatch stub
 * Bank 1 Address: 0x874c-0x8754 (9 bytes) [actual addr: 0x1074c]
 *
 * Identical to 873a - possibly for different state machine entry point.
 *
 * Original disassembly:
 *   // WARNING: Subroutine does not return
 *   flash_func_0bc8(0xff);
 */
void flash_dispatch_stub_874c(void)
{
    flash_func_0bc8(0xFF);
}

/*
 * flash_dispatch_stub_8d6e - Flash error dispatch stub
 * Bank 1 Address: 0x8d6e-0x8d76 (9 bytes) [actual addr: 0x10d6e]
 *
 * Identical to 873a - possibly for different state machine entry point.
 * This one is at the end of the Bank 1 function group before the debug init.
 *
 * Original disassembly:
 *   // WARNING: Subroutine does not return
 *   flash_func_0bc8(0xff);
 */
void flash_dispatch_stub_8d6e(void)
{
    flash_func_0bc8(0xFF);
}

/*
 * flash_command_handler - Flash Command Handler
 * Address: 0x0525-0x0529 (5 bytes) -> dispatches to bank 0 0xBAA0
 *
 * Function at 0xBAA0:
 * Flash command processor. Reads commands from SPI flash buffer at 0x7000
 * and dispatches to appropriate handlers.
 *
 * Command types (from 0x7000):
 *   0x3A: Command type 1 - set flag 0x07BC=1, 0x07B8=1, call 0xE4B4, 0x538D
 *   0x3B: Command type 2 - set flag 0x07BC=2, call 0x538D
 *   0x3C: Command type 3 - set flag 0x07BC=3, call 0x538D
 *   Other: Configure 0xCC98 with (val & 0xF8) | 0x06, call 0x95B6
 *
 * Original disassembly:
 *   baa0: mov dptr, #0xe795
 *   baa3: movx a, @dptr
 *   baa4: jb 0xe0.5, 0xbaaa       ; if bit 5 set, continue
 *   baa7: ljmp 0xbb36             ; else exit
 *   baaa: lcall 0xae87            ; helper
 *   baad: lcall 0xb8c3            ; helper
 *   bab0: clr a
 *   bab1: mov r7, a
 *   bab2: lcall 0xdd42            ; helper
 *   bab5: lcall 0xe6e7            ; helper
 *   bab8: mov dptr, #0x7000       ; SPI flash buffer
 *   babb: movx a, @dptr           ; read command byte
 *   babc: cjne a, #0x3a, 0xbada   ; check for command 0x3A
 *   ... (command dispatch logic)
 */
void flash_command_handler(void)
{
    uint8_t val;
    uint8_t cmd;

    /* Check bit 5 of REG_FLASH_READY_STATUS - if not set, exit early */
    val = REG_FLASH_READY_STATUS;
    if ((val & 0x20) == 0) {
        return;
    }

    /* Read command from SPI flash buffer at 0x7000 */
    cmd = XDATA8(FLASH_BUFFER_BASE);

    if (cmd == 0x3A) {
        /* Command 0x3A: Set flags and process */
        G_FLASH_CMD_TYPE = 0x01;
        G_FLASH_CMD_FLAG = 0x01;
        /* Call helper 0xE4B4 for flash operation */
        /* Call helper 0x538D with R3=0xFF, R2=0x21, R1=0xFB */
    } else if (cmd == 0x3B) {
        /* Command 0x3B: Set flag and process */
        G_FLASH_CMD_TYPE = 0x02;
        /* Call helper 0x538D with R3=0xFF, R2=0x22, R1=0x0B */
    } else if (cmd == 0x3C) {
        /* Command 0x3C: Set flag and process */
        G_FLASH_CMD_TYPE = 0x03;
        /* Call helper 0x538D with R3=0xFF, R2=0x22, R1=0x25 */
    }

    /* Configure CPU DMA ready register - set bits 0-2 to 6 */
    val = REG_CPU_DMA_READY;
    val = (val & 0xF8) | 0x06;
    REG_CPU_DMA_READY = val;

    /* Write state 0x04 to event control */
    G_EVENT_CTRL_09FA = 0x04;
}

/* External helper functions declared for use in system_init_from_flash */
extern void uart_write_byte_daeb(void);
extern void uart_write_daff(void);
extern uint8_t uart_read_byte_dace(uint8_t offset);
extern void sys_event_dispatch_05e8(void);
extern void sys_init_helper_bbc7(void);
extern void sys_timer_handler_e957(void);

/*
 * system_init_from_flash - Initialize system from flash configuration
 * Bank 1 Address: 0x8d77-0x8fe0+ (~617 bytes) [actual addr: 0x10d77]
 *
 * Complex initialization function that reads configuration from flash buffer
 * (0x70xx), validates checksum, and sets up system parameters.
 *
 * Key operations:
 *   1. Initialize default mode flags (0x09F4-0x09F8)
 *   2. Set retry counter (IDATA[0x22])
 *   3. Loop up to 6 times checking flash header
 *   4. Validate header marker at 0x707E (must be 0xA5)
 *   5. Compute checksum over 0x7004-0x707E
 *   6. If valid, parse configuration:
 *      - Vendor strings from 0x7004
 *      - Serial strings from 0x702C
 *      - Configuration bytes from 0x7054
 *      - Device IDs from 0x705C-0x707F
 *   7. Set event flags based on mode configuration
 *   8. Call system init helpers
 */
void system_init_from_flash(void)
{
    uint8_t retry_count;
    uint8_t header_marker;
    uint8_t checksum;
    uint8_t computed_checksum;
    uint8_t i;
    uint8_t mode_val;
    uint8_t tmp;

    /* Initialize default mode flags */
    G_FLASH_MODE_1 = 3;  /* Mode configuration 1 */
    G_FLASH_MODE_2 = 1;  /* Mode configuration 2 */
    G_FLASH_MODE_3 = 1;  /* Mode configuration 3 */
    G_FLASH_MODE_4 = 3;  /* Mode configuration 4 */
    G_FLASH_MODE_5 = 1;  /* Mode configuration 5 */
    G_FLASH_CONFIG_VALID = 0;  /* Flash config valid flag */
    retry_count = 0;     /* IDATA[0x22] = 0 */

    /* Flash read/validation retry loop */
    while (retry_count <= 5) {
        /* Set flash read trigger */
        G_FLASH_READ_TRIGGER = 1;

        /* Call timer/watchdog handler */
        sys_timer_handler_e957();

        if (retry_count != 0) {
            /* Check header marker at 0x707E */
            header_marker = G_FLASH_BUF_707E;
            if (header_marker == 0xA5) {
                /* Compute checksum from 0x7004 to 0x707E */
                computed_checksum = 0;
                for (i = 4; i < 0x7F; i++) {
                    computed_checksum += uart_read_byte_dace(0);
                }

                /* Get stored checksum from 0x707F */
                checksum = G_FLASH_BUF_707F;

                /* Validate checksum */
                if (checksum == computed_checksum) {
                    /* Checksum valid - mark flash config as valid */
                    G_FLASH_CONFIG_VALID = 1;

                    /* Parse vendor strings from 0x7004 if valid */
                    if (G_FLASH_BUF_7004 != 0xFF) {
                        /* Copy vendor string data */
                        for (i = 0; (&G_FLASH_BUF_7004)[i] != 0xFF && i < 0x28; i++) {
                            uart_write_byte_daeb();
                        }
                    }

                    /* Parse serial strings from 0x702C if valid */
                    if (G_FLASH_BUF_702C != 0xFF) {
                        for (i = 0; (&G_FLASH_BUF_702C)[i] != 0xFF && i < 0x28; i++) {
                            uart_write_daff();
                        }
                    }

                    /* Parse configuration bytes */
                    for (i = 0; i < 6; i++) {
                        tmp = uart_read_byte_dace(0x54);
                        if (tmp == 0xFF) break;
                        (&G_FLASH_CFG_0A41)[-5 + i] = uart_read_byte_dace(0x54);
                        if (i == 5) {
                            /* Mask lower nibble of 0x0A41 */
                            G_FLASH_CFG_0A41 = G_FLASH_CFG_0A41 & 0x0F;
                        }
                    }

                    /* Parse device IDs from 0x705C-0x705D */
                    if (G_FLASH_BUF_705C != 0xFF || G_FLASH_BUF_705D != 0xFF) {
                        G_FLASH_CFG_0A42 = G_FLASH_BUF_705C;
                        G_FLASH_CFG_0A43 = G_FLASH_BUF_705D;
                    }

                    /* Parse additional device info from 0x705E-0x705F */
                    if (G_FLASH_BUF_705E == 0xFF && G_FLASH_BUF_705F == 0xFF) {
                        /* Use defaults from 0x0A57-0x0A58 */
                        G_FLASH_CFG_0A44 = G_CMD_CTRL_PARAM;
                        G_FLASH_CFG_0A45 = G_CMD_TIMEOUT_PARAM;
                    } else {
                        G_FLASH_CFG_0A44 = G_FLASH_BUF_705E;
                        G_FLASH_CFG_0A45 = G_FLASH_BUF_705F;
                    }

                    /* Parse mode configuration from 0x7059-0x705A */
                    tmp = G_FLASH_BUF_7059;
                    G_FLASH_MODE_1 = (tmp >> 4) & 0x03;  /* Bits 5:4 */
                    G_FLASH_MODE_2 = (tmp >> 6) & 0x01;  /* Bit 6 */
                    G_FLASH_MODE_3 = tmp >> 7;          /* Bit 7 */

                    tmp = G_FLASH_BUF_705A;
                    G_FLASH_MODE_4 = tmp & 0x03;        /* Bits 1:0 */
                    G_FLASH_MODE_5 = (tmp >> 2) & 0x01; /* Bit 2 */

                    /* Set initialization flag */
                    G_SYS_FLAGS_07F7 = G_SYS_FLAGS_07F7 | 0x04;

                    goto set_event_flags;
                }
            }
        }

        retry_count++;
    }

set_event_flags:
    /* Set event flags based on mode configuration */
    mode_val = G_FLASH_MODE_1;
    if (mode_val == 3) {
        G_EVENT_FLAGS = 0x87;
        G_FLASH_STATUS_09FB = 3;
    } else if (mode_val == 2) {
        G_EVENT_FLAGS = 0x06;
        G_FLASH_STATUS_09FB = 1;
    } else {
        if (mode_val == 1) {
            G_EVENT_FLAGS = 0x85;
        } else {
            G_EVENT_FLAGS = 0xC1;
        }
        G_FLASH_STATUS_09FB = 2;
    }

    /* Check flash ready status bit 5 */
    if (((REG_FLASH_READY_STATUS >> 5) & 0x01) != 1) {
        G_EVENT_FLAGS = 0x04;
    }

    /* Call system init helper */
    sys_init_helper_bbc7();

    /* If flash config is valid, call event dispatcher */
    if (G_FLASH_CONFIG_VALID == 1) {
        sys_event_dispatch_05e8();
    }
}

