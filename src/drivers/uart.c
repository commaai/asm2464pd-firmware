/*
 * uart.c - UART Debug Interface Driver
 *
 * See drivers/uart.h for hardware documentation.
 */

#include "drivers/uart.h"
#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*
 * uart_putc - Print a single character
 * Address: 0x5398-0x53a0 (inline in uart_puts loop)
 *
 * Writes a single character to the UART transmit register.
 * No FIFO check - relies on baud rate being fast enough.
 *
 * Original disassembly (inline in uart_puts):
 *   5398: mov dptr, #0xc001   ; UART THR
 *   539b: mov a, r7           ; get character
 *   539c: movx @dptr, a       ; write to THR
 */
void uart_putc(uint8_t ch)
{
    REG_UART_THR = ch;
}

/*
 * uart_newline - Print carriage return and line feed
 * Address: 0xae89-0xae91 (9 bytes, part of debug_output_handler)
 *
 * Outputs standard LF+CR sequence for newline.
 *
 * Original disassembly:
 *   ae89: mov dptr, #0xc001   ; UART THR
 *   ae8c: mov a, #0x0a        ; LF
 *   ae8e: movx @dptr, a       ; write LF
 *   ae8f: mov a, #0x0d        ; CR
 *   ae91: movx @dptr, a       ; write CR
 */
void uart_newline(void)
{
    REG_UART_THR = '\n';  /* LF = 0x0A */
    REG_UART_THR = '\r';  /* CR = 0x0D */
}

/*
 * uart_puthex - Print a byte as two hex characters
 * Address: 0x520c-0x5233 (40 bytes)
 *
 * Outputs the hex representation of a byte to the UART.
 * Uses '0'-'9' for values 0-9, 'A'-'F' for 10-15.
 * The trick: 'A' - 10 = '7', so base + nibble gives correct char.
 *
 * Register usage:
 *   R7: val parameter, later reused for second nibble base
 *   R6: nibble value (both iterations)
 *   R5: first nibble base character
 *
 * Original disassembly:
 *   520c: mov a, r7           ; get val
 *   520d: swap a              ; high nibble to low
 *   520e: anl a, #0x0f        ; mask
 *   5210: mov r6, a           ; save nibble
 *   5211: clr c
 *   5212: subb a, #0x0a       ; compare to 10
 *   5214: mov r5, #0x37       ; '7' for >= 10
 *   5216: jnc 521a            ; skip if >= 10
 *   5218: mov r5, #0x30       ; '0' for < 10
 *   521a: mov a, r5           ; get base
 *   521b: add a, r6           ; add nibble
 *   521c: mov dptr, #0xc001   ; UART THR
 *   521f: movx @dptr, a       ; write char
 *   5220: mov a, r7           ; get val again
 *   5221: anl a, #0x0f        ; low nibble
 *   5223: mov r6, a           ; save nibble
 *   5224: clr c
 *   5225: subb a, #0x0a       ; compare
 *   5227: mov r7, #0x37       ; '7' (reuses val's register!)
 *   5229: jnc 522d            ; skip if >= 10
 *   522b: mov r7, #0x30       ; '0'
 *   522d: mov a, r7           ; get base
 *   522e: add a, r6           ; add nibble
 *   522f: mov dptr, #0xc001   ; UART THR
 *   5232: movx @dptr, a       ; write char
 *   5233: ret
 */
void uart_puthex(uint8_t val)
{
    uint8_t n, b;

    /* Print high nibble */
    n = (val >> 4) & 0xF;
    b = '7';
    if (n < 10) b = '0';
    REG_UART_THR = b + n;

    /* Print low nibble - reuse val register for base */
    n = val & 0xF;
    val = '7';
    if (n < 10) val = '0';
    REG_UART_THR = val + n;
}

/*
 * uart_putdigit - Print a single digit character
 * Address: 0x51e6-0x51ee (9 bytes)
 *
 * From ghidra.c FUN_CODE_51e6:
 *   REG_UART_THR_RBR = param_1 + '0';
 *
 * Original disassembly:
 *   51e6: ef             mov a, r7
 *   51e7: 24 30          add a, #0x30     ; add '0'
 *   51e9: 90 c0 01       mov dptr, #0xc001
 *   51ec: f0             movx @dptr, a
 *   51ed: 22             ret
 */
void uart_putdigit(uint8_t digit)
{
    REG_UART_THR = digit + '0';
}

/*
 * uart_puts - Print a null-terminated string from code memory
 * Address: 0x538e-0x53a6 (25 bytes)
 *
 * Outputs characters from a code memory pointer until null terminator.
 * Uses generic memory read helper at 0x0bc8 to read from code space.
 *
 * Parameters:
 *   str - pointer to string in code memory (__code)
 *
 * Original disassembly:
 *   538d: lcall 0x0bc8       ; read byte from code memory (R3:R2:R1 = ptr)
 *   5390: mov r7, a          ; save character
 *   5391: mov r5, a          ; copy for zero check
 *   5392: rlc a              ; get sign bit
 *   5393: subb a, 0xe0       ; compare with ACC (trick for zero check)
 *   5395: orl a, r5          ; combine
 *   5396: jz 53a6            ; if zero, done
 *   5398: mov dptr, #0xc001  ; UART THR
 *   539b: mov a, r7          ; get character
 *   539c: movx @dptr, a      ; write to THR
 *   539d: mov a, #0x01       ; increment pointer
 *   539f: add a, r1          ; low byte
 *   53a0: mov r1, a
 *   53a1: clr a
 *   53a2: addc a, r2         ; high byte
 *   53a3: mov r2, a
 *   53a4: sjmp 538d          ; loop
 *   53a6: ret
 */
void uart_puts(__code const char *str)
{
    char ch;

    while ((ch = *str++) != '\0') {
        REG_UART_THR = ch;
    }
}

/*
 * debug_output_handler - Main debug output handler
 * Address: 0xAE89-0xAF5D (213 bytes)
 *
 * This is the main debug output function that prints debug trace messages
 * to the UART in the format: "\nXX:YY]" followed by flag-specific handlers.
 *
 * The function reads debug status from 0xE40F and 0xE410 registers,
 * outputs them as hex, then dispatches to specific handlers based on
 * which status bits are set.
 *
 * Debug Output Format:
 *   - Newline (LF + CR)
 *   - Debug string from code memory
 *   - Value from 0xE40F as hex
 *   - ":"
 *   - Value from 0xE410 as hex
 *   - "]"
 *   - Handler-specific output based on status bits
 *
 * Status bit handlers (0xE40F):
 *   bit 7: Call 0xDFDC, jump to 0xB002
 *   bit 0: Write 0x01 to 0xE40F, call 0x83D6
 *   bit 5: Write 0x20 to 0xE40F, call 0xE419
 *
 * Status bit handlers (0xE410):
 *   bit 0: Write 0x01 to 0xE410
 *   bit 3: Write 0x08 to 0xE410
 *   bit 4: Write 0x10 to 0xE410
 *   bit 5: Write 0x20 to 0xE410, call 0xE876
 *   bit 6: Write 0x40 to 0xE410, call 0xE439
 *   bit 7: Write 0x80 to 0xE410
 *
 * Final checks (0xE314):
 *   bit 0: Write 0x01 to 0xE314, return
 *   bit 1: Write 0x02 to 0xE314, return
 *   bit 2: Write 0x04 to 0xE314, return
 *
 * Check (0xE661):
 *   bit 7: Write 0x80 to 0xE661
 *
 * Original disassembly:
 *   af5e: mov dptr, #0xc001   ; UART THR
 *   af61: mov a, #0x0a        ; LF
 *   af63: movx @dptr, a
 *   af64: mov a, #0x0d        ; CR
 *   af66: movx @dptr, a
 *   af67: mov r3, #0xff       ; string pointer high
 *   af69: mov r2, #0x23       ; string pointer mid
 *   af6b: mov r1, #0xee       ; string pointer low
 *   af6d: lcall 0x538d        ; uart_puts
 *   af70: mov dptr, #0xe40f   ; debug status 0
 *   af73: movx a, @dptr
 *   af74: mov r7, a
 *   af75: lcall 0x51c7        ; uart_puthex
 *   ... (continues with ':' output, 0xe410 hex output, ']' output)
 *   ... (then flag checking and handler dispatch)
 */
void debug_output_handler(void)
{
    uint8_t status0, status1, final_status;

    /* Output newline */
    REG_UART_THR = '\n';  /* LF */
    REG_UART_THR = '\r';  /* CR */

    /* Output debug string from code memory at 0xFF23EE */
    /* uart_puts uses code ptr - we output the prefix string */
    /* The original calls uart_puts with R3:R2:R1 = 0xFF:0x23:0xEE */

    /* Read and output status0 (0xE40F) */
    status0 = REG_CMD_CTRL_E40F;
    uart_puthex(status0);

    /* Output separator */
    REG_UART_THR = ':';

    /* Read and output status1 (0xE410) */
    status1 = REG_CMD_CTRL_E410;
    uart_puthex(status1);

    /* Output closing bracket */
    REG_UART_THR = ']';

    /* Re-read status0 for bit checking */
    status0 = REG_CMD_CTRL_E40F;

    /* Check status0 bit 7 - if set, call special handler and jump */
    if (status0 & 0x80) {
        /* Call 0xDFDC handler - prints more debug, polls 0xCC89 */
        /* Then jumps to write 0x80 to 0xE410 */
        goto write_e410_80;
    }

    /* Check status0 bit 0 */
    status0 = REG_CMD_CTRL_E40F;
    if (status0 & 0x01) {
        /* Write 0x01 to clear bit 0 */
        REG_CMD_CTRL_E40F = 0x01;
        /* Call 0x83D6 handler */
        goto end_checks;
    }

    /* Check status0 bit 5 */
    status0 = REG_CMD_CTRL_E40F;
    if (status0 & 0x20) {
        /* Write 0x20 to clear bit 5 */
        REG_CMD_CTRL_E40F = 0x20;
        /* Call 0xE419 handler */
        goto end_checks;
    }

    /* Check status1 (0xE410) bits */
    status1 = REG_CMD_CTRL_E410;
    if (status1 & 0x01) {
        /* Write 0x01 to clear bit 0 */
        REG_CMD_CTRL_E410 = 0x01;
        goto end_checks;
    }

    status1 = REG_CMD_CTRL_E410;
    if (status1 & 0x08) {
        /* Write 0x08 to clear bit 3 */
        REG_CMD_CTRL_E410 = 0x08;
        goto end_checks;
    }

    status1 = REG_CMD_CTRL_E410;
    if (status1 & 0x10) {
        /* Write 0x10 to clear bit 4 */
        REG_CMD_CTRL_E410 = 0x10;
        goto end_checks;
    }

    status1 = REG_CMD_CTRL_E410;
    if (status1 & 0x20) {
        /* Write 0x20 to clear bit 5 */
        REG_CMD_CTRL_E410 = 0x20;
        /* Call 0xE876 handler */
        goto end_checks;
    }

    status1 = REG_CMD_CTRL_E410;
    if (status1 & 0x40) {
        /* Write 0x40 to clear bit 6 */
        REG_CMD_CTRL_E410 = 0x40;
        /* Call 0xE439 handler */
        goto end_checks;
    }

    status1 = REG_CMD_CTRL_E410;
    if (status1 & 0x80) {
write_e410_80:
        /* Write 0x80 to clear bit 7 */
        REG_CMD_CTRL_E410 = 0x80;
    }

end_checks:
    /* Final status checks on 0xE314 */
    final_status = REG_DEBUG_STATUS_E314;
    if (final_status & 0x01) {
        /* Write 0x01 to acknowledge */
        REG_DEBUG_STATUS_E314 = 0x01;
        return;
    }

    final_status = REG_DEBUG_STATUS_E314;
    if (final_status & 0x02) {
        /* Write 0x02 to acknowledge */
        REG_DEBUG_STATUS_E314 = 0x02;
        return;
    }

    final_status = REG_DEBUG_STATUS_E314;
    if (final_status & 0x04) {
        /* Write 0x04 to acknowledge */
        REG_DEBUG_STATUS_E314 = 0x04;
        return;
    }

    /* Check 0xE661 bit 7 */
    if (REG_DEBUG_INT_E661 & DEBUG_INT_E661_FLAG) {
        /* Write 0x80 to acknowledge */
        REG_DEBUG_INT_E661 = 0x80;
    }
}

/*
 * delay_function - Timer-based delay
 * Address: 0xE529-0xE52E (calls 0xE50D helper)
 *
 * Implements a timer-based delay using Timer 0 CSR register.
 * Sets up timer parameters and polls until complete.
 *
 * From ghidra.c:
 *   FUN_CODE_e50d();
 *   do {
 *     bVar1 = REG_TIMER0_CSR;
 *   } while ((bVar1 >> 1 & 1) == 0);
 *   REG_TIMER0_CSR = 2;
 */
void delay_function(void)
{
    uint8_t status;

    /* Timer setup would be in FUN_CODE_e50d */
    /* Poll Timer 0 CSR until bit 1 is set (timer complete) */
    do {
        status = REG_TIMER0_DIV;  /* 0xCC10 - Timer 0 divisor/CSR */
    } while (!(status & 0x02));

    /* Acknowledge timer completion */
    REG_TIMER0_DIV = 0x02;
}

/*===========================================================================
 * UART Log Buffer Functions
 *===========================================================================*/

/*
 * uart_read_byte_dace - Read byte from log buffer
 * Address: 0xdace-0xdad8 (11 bytes)
 *
 * Disassembly:
 *   dace: add a, 0x21        ; A = A + I_LOG_INDEX
 *   dad0: mov 0x82, a        ; DPL = result
 *   dad2: clr a
 *   dad3: addc a, #0x70      ; DPH = 0x70 + carry
 *   dad5: mov 0x83, a        ; (gives DPTR = 0x7000 + offset)
 *   dad7: movx a, @dptr      ; Read byte
 *   dad8: ret
 *
 * Reads from the log buffer at 0x7000 + I_LOG_INDEX.
 * Returns: XDATA[0x7000 + I_LOG_INDEX]
 */
uint8_t uart_read_byte_dace(void)
{
    /* Read from log buffer at base 0x7000 + I_LOG_INDEX */
    uint16_t addr = 0x7000 + I_LOG_INDEX;
    return XDATA8(addr);
}

/*
 * uart_write_byte_daeb - Calculate log buffer write address
 * Address: 0xdaeb-0xdaf4 (10 bytes)
 *
 * Disassembly:
 *   daeb: mov a, #0xfc       ; Base offset
 *   daed: add a, 0x21        ; A = 0xFC + I_LOG_INDEX
 *   daef: mov 0x82, a        ; DPL = result
 *   daf1: clr a
 *   daf2: addc a, #0x09      ; DPH = 0x09 + carry
 *   daf4: ret
 *
 * Calculates address 0x09FC + I_LOG_INDEX for writing.
 * Returns: DPH (0x09 possibly + carry)
 */
uint8_t uart_write_byte_daeb(uint8_t b)
{
    /* Calculate DPTR = 0x09FC + I_LOG_INDEX
     * Original returns with DPTR set up for caller to use
     * The param b is the byte to write (in R7) */
    (void)b;
    /* In original, DPH is returned in A */
    uint8_t low = 0xFC + I_LOG_INDEX;
    uint8_t high = 0x09;
    if (low < 0xFC) high++;  /* Handle carry */
    return high;
}

/*
 * uart_write_daff - Calculate alternate log buffer address
 * Address: 0xdaff-0xdb08 (10 bytes)
 *
 * Disassembly:
 *   daff: mov a, #0x1c       ; Base offset
 *   db01: add a, 0x21        ; A = 0x1C + I_LOG_INDEX
 *   db03: mov 0x82, a        ; DPL = result
 *   db05: clr a
 *   db06: addc a, #0x0a      ; DPH = 0x0A + carry
 *   db08: ret
 *
 * Calculates address 0x0A1C + I_LOG_INDEX.
 * Returns: DPH (0x0A possibly + carry)
 */
uint8_t uart_write_daff(void)
{
    /* Calculate DPTR = 0x0A1C + I_LOG_INDEX */
    uint8_t low = 0x1C + I_LOG_INDEX;
    uint8_t high = 0x0A;
    if (low < 0x1C) high++;  /* Handle carry */
    return high;
}


/* ============================================================
 * UART Low-level Functions
 * ============================================================ */

/*
 * uart_wait_tx_ready - Wait for UART transmit buffer ready
 * Address: 0xe7ae-0xe7c0 (19 bytes)
 *
 * Two polling loops:
 * 1. Wait until (REG_UART_TFBF & 0x1F) == 0x10 (FIFO level)
 * 2. Wait until (REG_UART_STATUS & 0x07) == 0 (not busy)
 */
void uart_wait_tx_ready(void)
{
    /* Wait until UART FIFO level reaches 0x10 */
    while ((REG_UART_TFBF & 0x1F) != 0x10)
        ;

    /* Wait until UART not busy */
    while ((REG_UART_STATUS & 0x07) != 0)
        ;
}
