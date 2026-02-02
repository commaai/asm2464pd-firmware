/*
 * Ultra-minimal firmware - do almost nothing
 * 
 * The device appears as 174c:2463 during bootloader but disconnects
 * when firmware runs. This test tries to do as little as possible
 * to avoid breaking USB.
 */

#include "types.h"
#include "registers.h"

void uart_putc(uint8_t ch) {
    REG_UART_THR = ch;
}

void uart_puts(__code const char *str) {
    while (*str) uart_putc(*str++);
}

void uart_puthex(uint8_t val) {
    static __code const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[val >> 4]);
    uart_putc(hex[val & 0x0F]);
}

__sfr __at(0xA8) IE;
__sfr __at(0x88) TCON;

/* ISR stubs required by crt0 */
void timer0_isr_handler(void) {}
void usb_isr_handler(void) { 
    uart_putc('U'); 
    /* Read and clear status */
    (void)XDATA8(0xC802);
    (void)XDATA8(0x9101);
}
void int1_isr_handler(void) { uart_putc('I'); }

void main(void) {
    uint16_t loop = 0;
    
    /* Minimal UART init - just set line control */
    REG_UART_LCR &= 0x7F;
    REG_UART_LCR &= ~(1 << 3);
    
    uart_puts("MIN\n");
    
    /* Configure interrupt controller (from original firmware) */
    XDATA8(0xC800) = 0x05;
    XDATA8(0xC801) = 0x10;
    XDATA8(0xC805) = 0x02;
    XDATA8(0xC807) = 0x04;
    
    /* C8Ax channel config */
    XDATA8(0xC8A6) = 0x04;
    XDATA8(0xC8AA) = 0x03;
    XDATA8(0xC8AC) = 0x07;
    XDATA8(0xC8A1) = 0x80;
    XDATA8(0xC8A4) = 0x80;
    XDATA8(0xC8A9) = 0x01;
    
    /* C8Bx config */
    XDATA8(0xC8B2) = 0xBC;
    XDATA8(0xC8B3) = 0x80;
    XDATA8(0xC8B4) = 0xFF;
    XDATA8(0xC8B5) = 0xFF;
    XDATA8(0xC8B6) = 0x14;
    
    uart_puts("INT cfg done\n");
    
    /* Try 91C0 toggle to trigger USB interrupt mechanism */
    uart_puts("91C0="); 
    uart_puthex(XDATA8(0x91C0));
    uart_puts("\nToggle PHY\n");
    XDATA8(0x91C0) = 0x00;  /* Disable PHY */
    
    /* Enable 8051 interrupts */
    TCON = 0x05;  /* Edge triggered */
    IE = 0x85;    /* EA + EX0 + EX1 */
    
    /* Poll and watch for events */
    while (1) {
        static uint8_t prev_c802, prev_9101;
        uint8_t v;
        
        v = XDATA8(0xC802);
        if (v != prev_c802) {
            prev_c802 = v;
            uart_puts("C802="); uart_puthex(v); uart_putc(' ');
        }
        
        v = XDATA8(0x9101);
        if (v != prev_9101) {
            prev_9101 = v;
            uart_puts("9101="); uart_puthex(v); uart_putc(' ');
        }
        
        if (TCON & 0x02) { uart_putc('0'); TCON &= ~0x02; }
        if (TCON & 0x08) { uart_putc('1'); TCON &= ~0x08; }
        
        if (++loop == 0) uart_putc('.');
    }
}
