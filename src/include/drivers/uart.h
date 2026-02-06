/*
 * uart.h - UART Debug Interface Driver
 *
 * Dedicated UART controller for debug output on the ASM2464PD USB4/Thunderbolt
 * to NVMe bridge. This is NOT the standard 8051 SBUF serial interface - it's a
 * separate hardware block based on ASMedia USB host controller UART design.
 * Similar to ASM1142 UART (documented at 0xF100-0xF10A).
 *
 * HARDWARE CONFIGURATION
 *   Baud rate: 921600 fixed (no configuration registers)
 *   Default format: 8O1 (LCR reset value = 0x0B)
 *   For 8N1: Set LCR to 0x03 (or LCR &= 0xF7 to just clear parity)
 *   TX pin: B21, RX pin: A21
 *   FIFO: 16-byte transmit/receive FIFOs
 *
 * REGISTER MAP (0xC000-0xC00F)
 *   0xC000  RBR (RO)     Receive Buffer Register - read received byte
 *   0xC001  THR (WO)     Transmit Holding Register - write byte to send
 *   0xC002  IER          Interrupt Enable Register
 *   0xC004  FCR (WO)     FIFO Control Register
 *           IIR (RO)     Interrupt Identification Register
 *   0xC005  RFBR (RO)    RX FIFO Bytes Received - count of bytes in RX FIFO
 *   0xC006  TFBF (RO)    TX FIFO Bytes Free - available space in TX FIFO
 *   0xC007  LCR          Line Control Register (reset=0x0B: 8O1)
 *   0xC008  MCR          Modem Control Register
 *   0xC009  LSR          Line Status Register
 *   0xC00A  MSR          Modem Status Register
 *
 * DATA FLOW
 *   TX: CPU writes THR -> TX FIFO (16 bytes) -> Shift Register -> TX Pin
 *       Check TFBF > 0 before writing to avoid overflow
 *   RX: RX Pin -> Shift Register -> RX FIFO -> RBR -> CPU reads
 *       Poll RFBR > 0 to check for received data, then read from RBR
 *
 * LCR BITS
 *   [1:0] Data bits: 0=5, 1=6, 2=7, 3=8
 *   [2]   Stop bits: 0=1 stop, 1=2 stop
 *   [5:3] Parity: XX0=None, 001=Odd, 011=Even, 101=Mark, 111=Space
 *   [7]   Loopback enable
 *
 * DEBUG OUTPUT FORMAT
 *   Trace messages: "\nXX:YY]" where XX:YY are hex register values
 *   Used for PCIe/NVMe command tracing from bank 1 debug routines (0xAF5E+)
 */
#ifndef _UART_H_
#define _UART_H_

#include "../types.h"

/* UART output functions */
void uart_putc(uint8_t ch);                     /* inline - writes to 0xC001 */
void uart_newline(void);                        /* 0xae89-0xae91 (part of debug handler) */
void uart_puthex(uint8_t val);                  /* 0x520c-0x5233 */
void uart_putdigit(uint8_t digit);              /* 0x522b-0x5233 (tail of puthex) */
void uart_puts(__code const char *str);         /* 0x53fa (called from 0xae98) */

/* Debug output handler */
void debug_output_handler(void);                /* 0xae89-0xaf5d */

/* Low-level UART functions */
uint8_t uart_read_byte_dace(void);              /* 0xdace-0xdaea */
uint8_t uart_write_byte_daeb(uint8_t b);        /* 0xdaeb-0xdafe */
uint8_t uart_write_daff(void);                  /* 0xdaff-0xdb0f */
void uart_wait_tx_ready(void);                  /* 0xdb10-0xdb1a */

/* Delay */
void delay_function(void);                      /* 0xe529-0xe52e (Bank 1) */

#endif /* _UART_H_ */
