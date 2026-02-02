/*
 * Minimal firmware for ASM2464PD - "Do Nothing" test
 * 
 * Theory: The bootloader already enumerated as USB3.
 * Let's see if the device stays enumerated if we don't touch USB at all.
 */

#include "types.h"
#include "registers.h"

/* SFRs */
__sfr __at(0xA8) IE;
__sfr __at(0x88) TCON;
__sfr __at(0x96) DPX;

/* UART for debug output - direct write like original firmware */
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

/* Global ISR counters */
static volatile uint8_t isr_count = 0;
static volatile uint8_t int1_count = 0;

/* INT0 ISR - USB interrupt */
void usb_isr_handler(void) {
    uint8_t c800 = REG_INT_STATUS_C800;
    uint8_t c802 = REG_INT_USB_STATUS;
    uint8_t c806 = REG_INT_SYSTEM;
    
    isr_count++;
    
    /* Print first few interrupts */
    if (isr_count <= 5) {
        uart_puts("ISR:");
        uart_puthex(c800);
        uart_putc('/');
        uart_puthex(c802);
        uart_putc('/');
        uart_puthex(c806);
        uart_putc('\n');
    }
    
    /* Clear whatever triggered it */
    REG_INT_STATUS_C800 = c800;
    REG_INT_USB_STATUS = c802;
    REG_INT_SYSTEM = c806;
}

/* INT1 ISR - System/PCIe/NVMe interrupt (original at 0x44D7) */
void int1_isr_handler(void) {
    uint8_t c806 = REG_INT_SYSTEM;
    uint8_t cc33 = REG_CPU_EXEC_STATUS_2;
    uint8_t c80a = REG_INT_PCIE_NVME;
    uint8_t ec06 = REG_NVME_EVENT_STATUS;
    
    int1_count++;
    
    /* Print first 10 interrupts with full status */
    if (int1_count <= 10) {
        uart_putc('I');
        uart_puthex(c806);
        uart_puthex(cc33);
        uart_puthex(c80a);
        uart_puthex(ec06);
        uart_putc(' ');
    }
    
    /* 
     * Original firmware at 0x44D7 checks:
     * - C806 bit 0 -> calls 0x0507
     * - CC33 bit 2 -> writes 0x04 to CC33, calls 0x038B
     * - C80A bit 6 -> calls 0x0516
     * - 09F9 & 0x83 != 0 -> more checks
     *   - C80A bit 5 -> calls 0x05F2
     *   - C80A bit 4 -> calls 0x0570
     *   - EC06 bit 0 -> more processing
     */
    
    /* Clear interrupt status bits (write 1 to clear) */
    if (c806 & 0x01) {
        REG_INT_SYSTEM = 0x01;  /* Clear bit 0 */
    }
    if (cc33 & 0x04) {
        REG_CPU_EXEC_STATUS_2 = 0x04;  /* Clear bit 2 */
    }
    /* Clear any pending PCIe/NVMe interrupts */
    REG_INT_PCIE_NVME = c80a;
    REG_NVME_EVENT_STATUS = ec06;
}

void main(void) {
    volatile uint32_t i;
    uint8_t count = 0;
    
    /* UART init - do this FIRST to see early values */
    REG_UART_LCR &= 0x7F;
    REG_UART_LCR &= ~(1 << 3);
    REG_UART_FCR |= (1 << 1);
    
    /* Print register state BEFORE we touch anything */
    uart_puts("\n=== BOOT REGS (before write) ===\n");
    uart_puts("91D1="); uart_puthex(REG_USB_PHY_CTRL_91D1);
    uart_puts(" 9300="); uart_puthex(REG_BUF_CFG_9300);
    uart_puts(" 9301="); uart_puthex(REG_BUF_CFG_9301);
    uart_putc('\n');
    uart_puts("9091="); uart_puthex(REG_USB_CTRL_PHASE);
    uart_puts(" 91C0="); uart_puthex(REG_USB_PHY_CTRL_91C0);
    uart_puts(" 91C1="); uart_puthex(REG_USB_PHY_CTRL_91C1);
    uart_putc('\n');
    uart_puts("9000="); uart_puthex(REG_USB_STATUS);
    uart_puts(" 9101="); uart_puthex(REG_USB_EP_CFG1);
    uart_putc('\n');
    
    /* DON'T touch any registers - see if bootloader config stays */
    uart_puts("92C0="); uart_puthex(REG_POWER_ENABLE);
    uart_puts(" 91C0="); uart_puthex(REG_USB_PHY_CTRL_91C0);
    uart_putc('\n');
    
    /* Print register state AFTER write attempt */
    uart_puts("=== AFTER WRITE ATTEMPT ===\n");
    
    /* Enable interrupts and USB (like original at 0x1FB0-0x1FB8) */
    isr_count = 0;
    int1_count = 0;
    
    /* Show TCON before touching it */
    uart_puts("TCON="); uart_puthex(TCON); uart_putc('\n');
    
    /* Clear any pending external interrupts */
    TCON &= ~0x0A;  /* Clear IE0 and IE1 flags */
    
    /* Don't touch USB status - let bootloader config stay */
    /* REG_USB_STATUS |= 0x01; */
    REG_INT_ENABLE = 0x17;
    TCON |= 0x05;  /* IT0=1, IT1=1: Both edge-triggered */
    
    /* Show TCON after setup */
    uart_puts("TCON="); uart_puthex(TCON); uart_putc('\n');
    
    uart_puts("Enabling IRQs...\n");
    IE = 0x85;     /* EA + EX0 + EX1 (like original) */
    
    /* Check if "locked" registers were written */
    uart_puts("91D1="); uart_puthex(REG_USB_PHY_CTRL_91D1);  /* want 0x0F */
    uart_puts(" 9300="); uart_puthex(REG_BUF_CFG_9300);      /* want 0x0C */
    uart_puts(" 9301="); uart_puthex(REG_BUF_CFG_9301);      /* want 0xC0 */
    uart_putc('\n');
    uart_puts("9091="); uart_puthex(REG_USB_CTRL_PHASE);
    uart_puts(" 91C1="); uart_puthex(REG_USB_PHY_CTRL_91C1);
    uart_putc('\n');
    
    /* Main loop - Check for USB setup packets */
    while (1) {
        uint8_t c802 = REG_INT_USB_STATUS;
        uint8_t u9091 = REG_USB_CTRL_PHASE;  /* Control phase: bit 0=setup */
        uint8_t u9000 = REG_USB_STATUS;
        
        /* Check if setup packet received (9091 bit 0) */
        if (u9091 & 0x01) {
            uart_puts("SETUP! bReq=");
            uart_puthex(REG_USB_SETUP_REQUEST);   /* 0x9E01 */
            uart_puts(" wVal=");
            uart_puthex(REG_USB_SETUP_VALUE_H);   /* 0x9E03 */
            uart_puthex(REG_USB_SETUP_VALUE_L);   /* 0x9E02 */
            uart_putc('\n');
        }
        
        /* Check for any USB activity */
        if (c802 || (u9091 & 0x03)) {
            uart_puts("C802="); uart_puthex(c802);
            uart_puts(" 9091="); uart_puthex(u9091);
            uart_puts(" 9000="); uart_puthex(u9000);
            uart_putc('\n');
        }
        
        /* Slow down periodic status */
        for (i = 0; i < 50000; i++);
        count++;
        if ((count & 0x3F) == 0) {
            uart_putc('.');
        }
    }
}
