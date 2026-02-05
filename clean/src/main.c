/*
 * Minimal "hello" firmware for ASM2464PD
 *
 * Initializes UART (disables parity) and prints "hello" in a loop
 */

#include "types.h"
#include "registers.h"

/* ISR stubs (required by crt0) */
void int0_isr(void) __interrupt(0) {}
void timer0_isr(void) __interrupt(1) {}
void int1_isr(void) __interrupt(2) {}
void timer1_isr(void) __interrupt(3) {}
void serial_isr(void) __interrupt(4) {}
void timer2_isr(void) __interrupt(5) {}

/*
 * uart_init - Initialize UART (disable parity)
 * Matches original firmware at 0xe309
 */
void uart_init(void)
{
    /* Clear bit 3 (Parity Enable) of LCR to disable parity */
    REG_UART_LCR &= 0xF7;
}

/*
 * uart_putc - Output a single character
 * Original firmware at 0x521c just writes directly (16-byte FIFO handles buffering)
 */
void uart_putc(uint8_t ch)
{
    REG_UART_THR = ch;
}

/*
 * uart_puts - Output a null-terminated string
 */
void uart_puts(__code const char *str)
{
    char ch;
    while ((ch = *str++) != '\0') {
        uart_putc(ch);
    }
}

/*
 * delay - Delay between messages
 */
void delay(void)
{
    volatile uint16_t i;
    for (i = 0; i < 60000; i++) {
        ;
    }
}

/*
 * main - Entry point
 */
void main(void)
{
    /* Initialize UART (disable parity) */
    uart_init();
    
    /* Print hello in a loop */
    while (1) {
        uart_puts("hello\n");
        delay();
    }
}
