/*
 * uart.h - UART Debug Interface Driver
 *
 * The UART subsystem provides serial debug output for firmware
 * development and diagnostics. It uses the 8051's built-in serial
 * port for character-based output.
 *
 * HARDWARE CONFIGURATION:
 *   - Baud rate: Typically 115200 (Timer 1 driven)
 *   - Data format: 8N1 (8 data bits, no parity, 1 stop bit)
 *   - Mode: TX-only debug output (no RX handling)
 *
 * OUTPUT FUNCTIONS:
 *   uart_putc()     - Single character output
 *   uart_puthex()   - Byte as 2-digit hex (e.g., "A5")
 *   uart_puts()     - Null-terminated string from CODE memory
 *   uart_newline()  - CR+LF sequence
 *
 * DEBUG OUTPUT FORMAT:
 *   Debug messages are typically formatted as:
 *   [SUBSYSTEM] message: value
 *
 *   Example: "[PCIE] Link speed: 03"
 *
 * KEY REGISTERS (8051 SFRs):
 *   SBUF: Serial buffer (write to transmit)
 *   SCON: Serial control (TI flag for TX complete)
 *
 * USAGE:
 *   uart_puts("Status: ");
 *   uart_puthex(status_byte);
 *   uart_newline();
 *
 * NOTE: UART output is synchronous and will block until
 * the transmit buffer is ready. Use sparingly in
 * performance-critical code paths.
 */
#ifndef _UART_H_
#define _UART_H_

#include "../types.h"

/* UART output functions */
void uart_putc(uint8_t ch);
void uart_newline(void);
void uart_puthex(uint8_t val);
void uart_putdigit(uint8_t digit);
void uart_puts(__code const char *str);

/* Debug output */
void debug_output_handler(void);

/* Low-level UART functions */
uint8_t uart_read_byte_dace(void);
uint8_t uart_write_byte_daeb(uint8_t b);
uint8_t uart_write_daff(void);
void uart_wait_tx_ready(void);

/* Delay */
void delay_function(void);

#endif /* _UART_H_ */
