/*
 * uart.h - UART Debug Interface Driver
 *
 * Dedicated UART controller for debug output on the ASM2464PD USB4/Thunderbolt
 * to NVMe bridge. This is NOT the standard 8051 SBUF serial interface - it's a
 * separate hardware block based on ASMedia USB host controller UART design.
 *
 * HARDWARE CONFIGURATION
 *   Baud rate: 921600 fixed (no configuration registers)
 *   Format: 8N1 (8 data bits, no parity, 1 stop bit)
 *   TX pin: B21, RX pin: A21
 *   FIFO: 16-byte transmit FIFO
 *
 * REGISTER MAP (0xC000-0xC00F)
 *   0xC000  UART_BASE    Base/control register
 *   0xC001  THR (WO)     Transmit Holding Register - write byte to send
 *           RBR (RO)     Receive Buffer Register - read received byte
 *   0xC002  IER          Interrupt Enable Register
 *   0xC004  FCR (WO)     FIFO Control Register
 *           IIR (RO)     Interrupt Identification Register
 *   0xC006  TFBF         Transmit FIFO Buffer Full - check before write
 *   0xC007  LCR          Line Control Register
 *   0xC008  MCR          Modem Control Register
 *   0xC009  LSR          Line Status Register
 *   0xC00A  MSR          Modem Status Register
 *
 * DATA FLOW
 *   TX: CPU writes THR -> TX FIFO (16 bytes) -> Shift Register -> TX Pin
 *       Check TFBF before writing to avoid overflow
 *   RX: RX Pin -> Shift Register -> RX FIFO -> RBR -> CPU reads
 *       (RX not used in stock firmware - debug output only)
 *
 * DEBUG OUTPUT FORMAT
 *   Trace messages: "\nXX:YY]" where XX:YY are hex register values
 *   Used for PCIe/NVMe command tracing from bank 1 debug routines (0xAF5E+)
 */
#ifndef _UART_H_
#define _UART_H_

#include "../types.h"

/* UART output functions */
void uart_putc(uint8_t ch);                     /* 0x5398-0x53a0 */
void uart_newline(void);                        /* 0xaf5e-0xaf66 (Bank 1) */
void uart_puthex(uint8_t val);                  /* 0x51c7-0x51e5 */
void uart_putdigit(uint8_t digit);              /* 0x51e6-0x51ee */
void uart_puts(__code const char *str);         /* 0x538d-0x53a6 */

/* Debug output handler */
void debug_output_handler(void);                /* 0xaf5e-0xb030 (Bank 1) */

/* Low-level UART functions */
uint8_t uart_read_byte_dace(void);              /* 0xdace-0xdaea */
uint8_t uart_write_byte_daeb(uint8_t b);        /* 0xdaeb-0xdafe */
uint8_t uart_write_daff(void);                  /* 0xdaff-0xdb0f */
void uart_wait_tx_ready(void);                  /* 0xdb10-0xdb1a */

/* Delay */
void delay_function(void);                      /* 0xe529-0xe52e (Bank 1) */

#endif /* _UART_H_ */
