/*
 * Minimal USB Firmware for ASM2464PD - Debugging EP0 IN
 */

#include "types.h"
#include "registers.h"

__sfr __at(0xA8) IE;
__sfr __at(0x88) TCON;

/* Setup packet at 0x9104-0x910B */
#define USB_SETUP(n)  XDATA8(0x9104 + (n))

void uart_putc(uint8_t ch) { REG_UART_THR = ch; }

void uart_puts(__code const char *str) {
    while (*str) uart_putc(*str++);
}

void uart_puthex(uint8_t val) {
    static __code const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[val >> 4]);
    uart_putc(hex[val & 0x0F]);
}

/* 18-byte device descriptor */
__code uint8_t dev_desc[] = {
    0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
    0xD1, 0xAD, 0x01, 0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x01
};

void usb_send_ep0(uint8_t len) {
    uint8_t i;
    
    /* Copy descriptor to EP0 buffer at 0x9E00 */
    for (i = 0; i < len; i++) {
        XDATA8(0x9E00 + i) = dev_desc[i];
    }
    
    /* 
     * Minimal EP0 IN trigger sequence that produces STALL response.
     * STALL proves hardware is responding - now need to figure out
     * how to send DATA instead of STALL.
     */
    
    /* Try adding DMA source address - maybe this changes STALL to DATA */
    REG_USB_EP_BUF_HI = 0x9E;     /* 0x905B: DMA source high byte */
    REG_USB_EP_BUF_LO = 0x00;     /* 0x905C: DMA source low byte */
    
    REG_USB_CONFIG &= ~0x02;      /* 0x9002: Clear bit 1 - required for STALL */
    REG_USB_CTRL_PHASE = 0x01;    /* 0x9091: Write 1 - required for STALL */
    REG_USB_EP0_STATUS = 0x00;    /* 0x9003: Clear status */
    REG_USB_EP0_LEN_L = len;      /* 0x9004: Set transfer length */
    REG_USB_EP_CTRL_905F &= 0xFE; /* 0x905F: Clear bit 0 - required for STALL */
    REG_USB_EP_CTRL_905D &= 0xFE; /* 0x905D: Clear bit 0 - required for STALL */
    REG_USB_EP_STATUS_90E3 = 0x01;/* 0x90E3: Trigger EP status */
    REG_USB_CTRL_90A0 = 0x01;     /* 0x90A0: Additional trigger */
    REG_USB_DMA_TRIGGER = 0x01;   /* 0x9092: DMA trigger */
    
    uart_putc('T');
    uart_puthex(len);
}

void main(void) {
    uint8_t prev_9091 = 0;
    uint16_t loop = 0;
    
    /* UART init - wait for stability */
    {
        uint16_t i;
        for (i = 0; i < 1000; i++);
    }
    REG_UART_LCR &= 0x7F;
    REG_UART_LCR &= ~(1 << 3);
    REG_UART_FCR |= 0x02;
    
    uart_puts("\n=== MIN ===\n");
    
    /* Interrupt controller - required for USB interrupt */
    REG_INT_STATUS_C800 = 0x05;
    REG_INT_ENABLE = 0x10;
    REG_INT_AUX_STATUS = 0x02;
    XDATA8(0xC807) = 0x04;
    REG_INT_CTRL = 0x20;
    XDATA8(0xC8A1) = 0x80;
    XDATA8(0xC8A4) = 0x80;
    
    /* USB core config */
    REG_POWER_ENABLE = 0x81;
    REG_BUF_CFG_9301 = 0xC0;
    REG_USB_CTRL_PHASE = 0x1F;
    REG_USB_PHY_CTRL_91C1 = 0xF0;
    REG_USB_CONFIG = 0xE0;
    REG_USB_PHY_CONFIG_9241 = 0xD0;
    REG_USB_PHY_CTRL_91C0 = 0x00;
    
    /* Unmask all USB interrupts */
    REG_USB_EP_READY = 0xFF;
    REG_USB_EP_CTRL_9097 = 0xFF;
    REG_USB_EP_MODE_9098 = 0xFF;
    REG_USB_EP_CFG_9099 = 0xFF;
    REG_USB_EP_CFG_909A = 0xFF;
    REG_USB_EP_CFG_909B = 0xFF;
    REG_USB_EP_CFG_909C = 0xFF;
    REG_USB_EP_CFG_909D = 0xFF;
    REG_USB_STATUS_909E = 0x03;
    
    TCON = 0x05;
    IE = 0x85;
    
    uart_puts("OK\n");
    
    while (1) {
        uint8_t s9091 = REG_USB_CTRL_PHASE;
        
        /* Detect new setup packet */
        if ((s9091 & 0x01) && !(prev_9091 & 0x01)) {
            uart_putc('\n');
            uart_puthex(USB_SETUP(0));
            uart_puthex(USB_SETUP(1));
            uart_puthex(USB_SETUP(2));
            uart_puthex(USB_SETUP(3));
            uart_puthex(USB_SETUP(4));
            uart_puthex(USB_SETUP(5));
            uart_puthex(USB_SETUP(6));
            uart_puthex(USB_SETUP(7));
            uart_putc(' ');
            
            /* GET_DESCRIPTOR Device? */
            if (USB_SETUP(0) == 0x80 && USB_SETUP(1) == 0x06 && USB_SETUP(3) == 0x01) {
                usb_send_ep0(18);
            }
        }
        prev_9091 = s9091;
        
        if (++loop == 0) uart_putc('.');
    }
}

void usb_isr_handler(void) { uart_putc('U'); }
void timer0_isr_handler(void) { uart_putc('T'); }
void int1_isr_handler(void) { uart_putc('I'); }
