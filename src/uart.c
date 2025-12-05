/*
 * ASM2464PD Firmware - UART Driver
 *
 * Dedicated UART controller for debug output
 * Fixed at 921600 baud, 8N1
 * Pins: A21 (RX), B21 (TX)
 *
 * UART registers are at 0xC000-0xC00F
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"

/*
 * uart_puthex - Print a byte as two hex characters
 * Address: 0x51c7-0x51e5 (31 bytes)
 *
 * From ghidra.c FUN_CODE_51c7:
 *   Prints high nibble then low nibble, using '0'-'9' for 0-9 and 'A'-'F' for 10-15.
 *   Actually uses '7' + value for A-F (since 'A' - 10 = '7')
 *
 * Original disassembly:
 *   51c7: mov a, r7           ; get param
 *   51c8: swap a              ; get high nibble
 *   51c9: anl a, #0x0f        ; mask to 4 bits
 *   51cb: mov r6, a           ; save
 *   51cc: clr c
 *   51cd: subb a, #0x0a       ; compare to 10
 *   51cf: mov r5, #0x37       ; assume >= 10, use '7' as base
 *   51d1: jnc 51d5            ; if >= 10, skip
 *   51d3: mov r5, #0x30       ; < 10, use '0' as base
 *   51d5: mov a, r5           ; get base char
 *   51d6: add a, r6           ; add nibble value
 *   51d7: mov dptr, #0xc001   ; UART THR
 *   51da: movx @dptr, a       ; write char
 *   ... repeat for low nibble ...
 */
void uart_puthex(uint8_t val)
{
    uint8_t nibble;
    uint8_t base;

    /* Print high nibble */
    nibble = (val >> 4) & 0x0F;
    base = (nibble >= 10) ? '7' : '0';  /* '7' + 10 = 'A' */
    REG_UART_THR = base + nibble;

    /* Print low nibble */
    nibble = val & 0x0F;
    base = (nibble >= 10) ? '7' : '0';
    REG_UART_THR = base + nibble;
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
 * uart_putc - Send a single byte via UART
 * Address: Not directly in original firmware as standalone function
 *          (may be inlined or part of other functions)
 *
 * Writes directly to UART THR register at 0xC001
 */
void uart_putc(uint8_t c)
{
    /* Note: Original firmware doesn't seem to check FIFO status before write */
    /* in the hot path - hardware likely handles flow control */
    REG_UART_THR = c;
}

/*
 * uart_puts - Send a null-terminated string via UART
 * Address: Not identified in original firmware
 *          (may not exist or be inlined)
 */
void uart_puts(const char __code *str)
{
    while (*str) {
        uart_putc(*str++);
    }
}

/*
 * uart_puthex16 - Print a 16-bit value as 4 hex characters
 * Address: Not identified as standalone in original firmware
 */
void uart_puthex16(uint16_t val)
{
    uart_puthex(HIBYTE(val));
    uart_puthex(LOBYTE(val));
}

/*
 * uart_puthex32 - Print a 32-bit value as 8 hex characters
 * Address: Not identified as standalone in original firmware
 */
void uart_puthex32(uint32_t val)
{
    uart_puthex((uint8_t)(val >> 24));
    uart_puthex((uint8_t)(val >> 16));
    uart_puthex((uint8_t)(val >> 8));
    uart_puthex((uint8_t)val);
}

/*
 * uart_rx_ready - Check if UART has received data
 * Address: Not identified as standalone in original firmware
 *
 * Returns non-zero if data is available (bit 0 of LSR)
 */
uint8_t uart_rx_ready(void)
{
    return REG_UART_LSR & 0x01;
}

/*
 * uart_getc - Read a byte from UART
 * Address: Not identified as standalone in original firmware
 *
 * Reads from UART RBR register at 0xC001
 * Note: Does not wait - caller should check uart_rx_ready first
 */
uint8_t uart_getc(void)
{
    return REG_UART_RBR;
}
