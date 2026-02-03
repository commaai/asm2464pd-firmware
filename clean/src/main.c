/*
 * Minimal Firmware for ASM2464PD
 *
 * Starting fresh - building up subsystems one at a time.
 * Current: UART + Timer (interrupt routing not yet working)
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
    
    /*
     * Timer init - from original firmware
     * CC32 bit 0: CPU execution active
     * Timer4: DIV=4, threshold=0xC700 (199 in high byte)
     */
    REG_CPU_EXEC_STATUS = (REG_CPU_EXEC_STATUS & 0xFE) | 0x01;
    REG_TIMER4_DIV = (REG_TIMER4_DIV & 0xF8) | 4;  /* Match original firmware */
    REG_TIMER4_THRESHOLD_LO = 0x00;
    REG_TIMER4_THRESHOLD_HI = 0xC7;  /* 0xC700 - match original firmware */
    REG_TIMER4_CSR = 0x04;  /* Clear */
    REG_TIMER4_CSR = 0x01;  /* Enable */
    uart_puts("TIM4=");
    uart_puthex(REG_TIMER4_CSR);
    uart_putc('\n');
    
    /*
     * MMIO Interrupt controller
     * NOTE: Interrupt routing to INT0/INT1 pins requires
     * additional PHY/system init we haven't implemented yet.
     * The original firmware does extensive init before interrupts work.
     */
    REG_INT_ENABLE = 0x17;  /* Global(0), USB(1), PCIe(2), System(4) */
    
    /* Configure 8051 interrupts */
    TCON = TCON_IT0 | TCON_IT1;   /* Edge triggered for INT0/INT1 */
    IE = IE_EA | IE_EX0 | IE_EX1; /* Enable global, INT0, INT1 */
    
    uart_puts("Ready\n");
    
    while (1) {
        /* Poll timer4 expired flag */
        uint8_t csr = REG_TIMER4_CSR;
        if (csr & 0x02) {
            uart_putc('T');
            /* Clear sequence from original firmware:
             * Write 0x04 (clear command), then 0x02 (write-1-to-clear expired bit) */
            REG_TIMER4_CSR = 0x04;  /* Clear command */
            REG_TIMER4_CSR = 0x02;  /* Write-1-to-clear expired flag */
            REG_TIMER4_CSR = 0x01;  /* Re-enable timer */
        }
        
        if (++loop == 0) {
            uart_putc('.');
        }
    }
}
