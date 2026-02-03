/*
 * Minimal UART-only Firmware for ASM2464PD
 *
 * Starting fresh - just UART output, no USB.
 * Will add subsystems one at a time.
 */

#include "types.h"
#include "registers.h"

/* 8051 SFRs */
__sfr __at(0x88) TCON;   /* Timer control */
__sfr __at(0xA8) IE;     /* Interrupt enable */

/* TCON bits */
#define TCON_IT0  0x01   /* INT0 edge triggered */
#define TCON_IT1  0x04   /* INT1 edge triggered */

/* IE bits */
#define IE_EA     0x80   /* Global interrupt enable */
#define IE_EX0    0x01   /* External interrupt 0 enable */
#define IE_ET0    0x02   /* Timer 0 interrupt enable */
#define IE_EX1    0x04   /* External interrupt 1 enable */

void uart_putc(uint8_t ch) { REG_UART_THR = ch; }

void uart_puts(__code const char *str) {
    while (*str) uart_putc(*str++);
}

void uart_puthex(uint8_t val) {
    static __code const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[val >> 4]);
    uart_putc(hex[val & 0x0F]);
}

void delay(uint16_t count) {
    while (count--) {
        __asm nop __endasm;
    }
}

void isr_handler(uint8_t which) {
    uart_putc('!');
    uart_puthex(which);
}

void main(void) {
    uint16_t loop = 0;
    
    /* Small delay for UART to stabilize */
    delay(1000);
    
    /* UART init - clear DLAB, disable parity, reset TX FIFO */
    REG_UART_LCR &= 0x7F;
    REG_UART_LCR &= ~(1 << 3);
    REG_UART_FCR |= 0x02;
    
    uart_puts("\n=== ASM2464PD Clean Firmware ===\n");
    
    /* Configure MMIO interrupt controller */
    uart_puts("INT: ");
    REG_INT_STATUS_C800 = 0x05;   /* Clear pending */
    REG_INT_ENABLE = 0x10;        /* Enable system interrupts */
    uart_puthex(REG_INT_STATUS_C800);
    uart_putc(' ');
    uart_puthex(REG_INT_ENABLE);
    uart_putc('\n');
    
    /* Configure 8051 interrupts */
    TCON = TCON_IT0 | TCON_IT1;   /* Edge triggered for INT0/INT1 */
    IE = IE_EA | IE_EX0 | IE_EX1; /* Enable global, INT0, INT1 */
    
    uart_puts("Ready\n");
    
    while (1) {
        if (++loop == 0) {
            uart_putc('.');
        }
    }
}


